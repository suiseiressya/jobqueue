-- S4 retry fields (D-019). Idempotent for `make migrate`.
-- max_retries: per-row retry budget; the failure after the last retry moves the job to the DLQ.
-- scheduled_at: when a PENDING job is due. A retry sets it to now() + backoff (DB clock).
--   Existing rows get the migration time, so they are due at once.
-- error_message: last failure's message; NULL until the job fails once.
ALTER TABLE jobs ADD COLUMN IF NOT EXISTS max_retries INT NOT NULL DEFAULT 3
    CHECK (max_retries >= 0);
ALTER TABLE jobs ADD COLUMN IF NOT EXISTS scheduled_at TIMESTAMPTZ NOT NULL DEFAULT now();
ALTER TABLE jobs ADD COLUMN IF NOT EXISTS error_message TEXT;

-- The scheduler tick asks for "PENDING and due": WHERE job_status = 'PENDING' AND scheduled_at <= now().
CREATE INDEX IF NOT EXISTS jobs_status_scheduled_at_idx ON jobs (job_status, scheduled_at);
