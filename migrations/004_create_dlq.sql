CREATE TABLE IF NOT EXISTS dead_letter_jobs (
    job_id UUID PRIMARY KEY REFERENCES jobs (id),
    moved_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
