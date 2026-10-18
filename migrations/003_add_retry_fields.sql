ALTER TABLE jobs ADD COLUMN IF NOT EXISTS max_retries INT NOT NULL DEFAULT 3
    CHECK (max_retries >= 0);
ALTER TABLE jobs ADD COLUMN IF NOT EXISTS scheduled_at TIMESTAMPTZ NOT NULL DEFAULT now();
ALTER TABLE jobs ADD COLUMN IF NOT EXISTS error_message TEXT;

CREATE INDEX IF NOT EXISTS jobs_status_scheduled_at_idx ON jobs (job_status, scheduled_at);
