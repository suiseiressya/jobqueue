-- Spread each event counter over 64 rows (D-026). Idempotent for `make migrate`.
-- With one row per event, every transaction that counted an event held that row's lock until
-- its commit (WAL flush), so all transitions of a kind committed one at a time: about 100
-- POST /jobs per second however many clients (k6, I-010). Each backend now counts
-- into its own row, `pg_backend_pid() % 64`; two connections share a row only by pid collision.
-- Totals are sum(n) per event (metrics/jobqueue.collector.yml). Existing counts become shard 0.
ALTER TABLE job_event_counts ADD COLUMN IF NOT EXISTS shard SMALLINT NOT NULL DEFAULT 0;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_index
        WHERE indrelid = 'job_event_counts'::regclass AND indisprimary AND indnatts = 2
    ) THEN
        ALTER TABLE job_event_counts DROP CONSTRAINT job_event_counts_pkey;
        ALTER TABLE job_event_counts ADD PRIMARY KEY (event, shard);
    END IF;
END $$;

-- Replaces 005's version. `make migrate` reapplies 005 first, which briefly restores the
-- single-row version (its ON CONFLICT (event) fails against this key): migrate with the app
-- stopped.
CREATE OR REPLACE FUNCTION count_job_event(event_name TEXT) RETURNS void AS $$
    INSERT INTO job_event_counts (event, shard, n) VALUES (event_name, pg_backend_pid() % 64, 1)
    ON CONFLICT (event, shard) DO UPDATE SET n = job_event_counts.n + 1;
$$ LANGUAGE sql;
