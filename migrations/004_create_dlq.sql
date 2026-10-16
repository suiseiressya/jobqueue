-- S4 dead letter queue (D-019). Idempotent for `make migrate`.
-- A job that used up its retries keeps its row in `jobs` as FAILED (with error_message and
-- retry_count); this table only marks it as dead-lettered. MoveToDlq writes both in one
-- transaction; RetryFromDlq deletes the row here and sets the job PENDING in one transaction.
-- The FK means `TRUNCATE jobs` alone is refused: truncate both tables together.
CREATE TABLE IF NOT EXISTS dead_letter_jobs (
    job_id UUID PRIMARY KEY REFERENCES jobs (id),
    moved_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
