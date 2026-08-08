CREATE TABLE IF NOT EXISTS jobs (
    id UUID PRIMARY KEY,
    payload JSONB NOT NULL,
    job_status VARCHAR(50) NOT NULL
        CHECK (job_status IN ('PENDING', 'RUNNING', 'DONE', 'FAILED')),
    retry_count INT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
