-- started_at: set when a worker claims the job (PENDING -> RUNNING); cleared by restart recovery.
-- completed_at: set when the job reaches DONE or FAILED.
-- Nullable: a job that has not reached that point has no value. Idempotent for `make migrate`.
ALTER TABLE jobs ADD COLUMN IF NOT EXISTS started_at TIMESTAMPTZ;
ALTER TABLE jobs ADD COLUMN IF NOT EXISTS completed_at TIMESTAMPTZ;
