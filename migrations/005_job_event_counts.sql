-- S5 metrics from the DB (D-023). Idempotent for `make migrate`.
-- Triggers count every job transition here, in the same transaction as the transition: a
-- rolled-back or 0-row write counts nothing, so a count never leads the state it reports.
-- sql_exporter (metrics/sql_exporter.yml) reads this table as Prometheus counters.
-- Counts survive app restarts; TRUNCATE (tests, crash script) resets them.
CREATE TABLE IF NOT EXISTS job_event_counts (
    event TEXT PRIMARY KEY,
    n BIGINT NOT NULL DEFAULT 0
);

CREATE OR REPLACE FUNCTION count_job_event(event_name TEXT) RETURNS void AS $$
    INSERT INTO job_event_counts (event, n) VALUES (event_name, 1)
    ON CONFLICT (event) DO UPDATE SET n = job_event_counts.n + 1;
$$ LANGUAGE sql;

-- Event names follow the app's transitions (ARCHITECTURE "Current flow"):
--   enqueued     INSERT (POST /jobs)
--   claimed      PENDING -> RUNNING (worker claim)
--   completed    RUNNING -> DONE
--   retried      RUNNING -> PENDING with retry_count + 1 (Retry)
--   recovered    RUNNING -> PENDING otherwise (startup recovery, D-010)
--   failed       RUNNING -> FAILED (Fail; the DLQ row is counted as `dlq`)
--   dlq_retried  FAILED -> PENDING (POST /dlq/:id/retry)
--   cancelled    DELETE (DELETE /jobs/:id)
-- Anything else is counted as 'OLD->NEW' so an unexpected transition still shows up.
CREATE OR REPLACE FUNCTION count_job_transition() RETURNS trigger AS $$
BEGIN
    IF TG_OP = 'INSERT' THEN
        PERFORM count_job_event('enqueued');
    ELSIF TG_OP = 'DELETE' THEN
        PERFORM count_job_event('cancelled');
    ELSE
        PERFORM count_job_event(CASE
            WHEN OLD.job_status = 'PENDING' AND NEW.job_status = 'RUNNING' THEN 'claimed'
            WHEN OLD.job_status = 'RUNNING' AND NEW.job_status = 'DONE' THEN 'completed'
            WHEN OLD.job_status = 'RUNNING' AND NEW.job_status = 'PENDING'
                 AND NEW.retry_count > OLD.retry_count THEN 'retried'
            WHEN OLD.job_status = 'RUNNING' AND NEW.job_status = 'PENDING' THEN 'recovered'
            WHEN OLD.job_status = 'RUNNING' AND NEW.job_status = 'FAILED' THEN 'failed'
            WHEN OLD.job_status = 'FAILED' AND NEW.job_status = 'PENDING' THEN 'dlq_retried'
            ELSE OLD.job_status || '->' || NEW.job_status
        END);
    END IF;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION count_dlq_insert() RETURNS trigger AS $$
BEGIN
    PERFORM count_job_event('dlq');
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE TRIGGER jobs_count_insert
    AFTER INSERT ON jobs
    FOR EACH ROW EXECUTE FUNCTION count_job_transition();

CREATE OR REPLACE TRIGGER jobs_count_status_change
    AFTER UPDATE OF job_status ON jobs
    FOR EACH ROW WHEN (OLD.job_status IS DISTINCT FROM NEW.job_status)
    EXECUTE FUNCTION count_job_transition();

CREATE OR REPLACE TRIGGER jobs_count_delete
    AFTER DELETE ON jobs
    FOR EACH ROW EXECUTE FUNCTION count_job_transition();

CREATE OR REPLACE TRIGGER dead_letter_jobs_count_insert
    AFTER INSERT ON dead_letter_jobs
    FOR EACH ROW EXECUTE FUNCTION count_dlq_insert();
