#!/usr/bin/env python3
"""End-to-end crash checks (S3, task F).

Each case runs the real binary against the `test` DB, breaks it at a bad moment, and
checks the state left behind with SQL. Postgres is read directly (psql in the `db`
container), never through the app, so the checks work after the app has died.

Usage: scripts/crash_checks.py [kill9|after_insert|sigterm|db_restart|db_restart_live ...]
       (no arguments runs every case; `make crash` builds first)

Rules:
  - Uses port 8080 and TRUNCATEs the `test` DB: do not run it next to `make test`
    or another jobqueue process.
  - db_restart and db_restart_live restart the shared Postgres container. Data stays
    (the volume is never removed), but anything else using the DB loses its connection.
  - Requires the Debug build (after_insert puts a gdb breakpoint in JobService::Enqueue).

Standard library only.
"""

import http.client
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONN = "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"
BIN = ROOT / "build" / "jobqueue"
PORT = 8080
LOG_DIR = ROOT / "build" / "crash-logs"
NPROC = len(os.sched_getaffinity(0))  # what `nproc` prints; the app runs one worker per core
# More jobs than workers, so some stay PENDING.
N_JOBS = NPROC * 2 + 5
SLEEP_MS = 2000

passes = 0
fails = 0
current_app = None  # killed on exit if a case leaves it running


# ---------- helpers ----------


def sql(query):
    """Run one query with psql in the db container and return its output, stripped."""
    out = subprocess.run(
        ["docker", "compose", "exec", "-T", "db", "psql", "-U", "jobqueue", "-d", "test",
         "-tAc", query],
        cwd=ROOT, capture_output=True, text=True,
    ).stdout
    return out.strip()


def sql_int(query):
    out = sql(query)
    return int(out) if out else 0


def check(description, actual, expected):
    global passes, fails
    if str(actual) == str(expected):
        print(f"  PASS  {description}")
        passes += 1
    else:
        print(f"  FAIL  {description} (got '{actual}', expected '{expected}')")
        fails += 1


def check_true(description, ok):
    check(description, "yes" if ok else "no", "yes")


def note(text):
    print(f"  NOTE  {text}")


def wait_for(timeout, condition):
    """Poll every 0.2 s until condition() is true. Returns False on timeout."""
    deadline = time.monotonic() + timeout
    while not condition():
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.2)
    return True


def port_open():
    try:
        with socket.create_connection(("127.0.0.1", PORT), timeout=0.5):
            return True
    except OSError:
        return False


def db_ready():
    return subprocess.run(
        ["docker", "compose", "exec", "-T", "db", "pg_isready", "-U", "jobqueue", "-d", "test"],
        cwd=ROOT, capture_output=True,
    ).returncode == 0


def wait_db():
    return wait_for(30, db_ready)


def compose(*args):
    subprocess.run(["docker", "compose", *args], cwd=ROOT, capture_output=True)


def post(sleep_ms=SLEEP_MS, timeout=5):
    """POST one job. Returns the HTTP status, or 0 when the app gave no answer."""
    body = json.dumps({"payload": json.dumps({"sleep_ms": sleep_ms})}).encode()
    request = urllib.request.Request(
        f"http://localhost:{PORT}/jobs", data=body, method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status
    except urllib.error.HTTPError as e:  # 4xx/5xx still carry a status
        return e.code
    except (urllib.error.URLError, http.client.HTTPException, OSError):
        return 0  # connection refused, reset, or timed out


def post_jobs(count, sleep_ms=SLEEP_MS):
    for _ in range(count):
        post(sleep_ms)


class App:
    """The jobqueue binary (or gdb running it), with its output appended to a log file."""

    def __init__(self, log_name, command=None):
        global current_app
        self.log_path = LOG_DIR / f"{log_name}.log"
        env = dict(os.environ, JOBQUEUE_PG_CONN=CONN)
        with open(self.log_path, "a") as log:
            self.proc = subprocess.Popen(command or [str(BIN)], cwd=ROOT, env=env,
                                         stdout=log, stderr=subprocess.STDOUT)
        current_app = self

    def exited(self):
        return self.proc.poll() is not None

    def signal(self, sig):
        self.proc.send_signal(sig)

    def wait(self):
        """Wait for exit; returns the exit code the shell would show (signal N -> 128 + N)."""
        global current_app
        code = self.proc.wait()
        current_app = None
        return 128 - code if code < 0 else code

    def log(self):
        return self.log_path.read_text(errors="replace")

    def recovered_count(self):
        found = re.findall(r"Recovered (\d+) jobs", self.log())
        return found[-1] if found else ""


def start_app(log_name):
    """Start the app and wait for its port. Returns None if it does not come up."""
    app = App(log_name)
    if not wait_for(15, port_open):
        print(f"  app did not start, see {app.log_path}")
        app.proc.kill()
        app.wait()
        return None
    return app


def restart_and_drain(log_name):
    """Restart the app, check it recovered every unfinished row, wait for all jobs to be
    DONE, then stop it."""
    unfinished = sql_int("SELECT count(*) FROM jobs WHERE job_status IN ('PENDING', 'RUNNING')")
    app = start_app(log_name)
    if app is None:
        return
    check("restart recovered every unfinished job", app.recovered_count(), unfinished)
    not_done = "SELECT count(*) FROM jobs WHERE job_status <> 'DONE'"
    wait_for(60, lambda: sql_int(not_done) == 0)
    check("all jobs DONE after restart", sql_int(not_done), 0)
    check("every job has started_at and completed_at",
          sql_int("SELECT count(*) FROM jobs WHERE started_at IS NULL OR completed_at IS NULL"), 0)
    app.signal(signal.SIGTERM)
    app.wait()


def running_count():
    return sql_int("SELECT count(*) FROM jobs WHERE job_status = 'RUNNING'")


# ---------- cases ----------


def case_kill9():
    """Crash mid-execute: RUNNING rows are left behind, restart recovery re-runs them."""
    app = start_app("kill9")
    if app is None:
        return
    post_jobs(N_JOBS)
    wait_for(10, lambda: running_count() > 0)
    app.signal(signal.SIGKILL)
    check("app killed by SIGKILL", app.wait(), 137)

    check("no job lost", sql_int("SELECT count(*) FROM jobs"), N_JOBS)
    check_true("RUNNING rows left behind", running_count() > 0)
    check("RUNNING rows have started_at",
          sql_int("SELECT count(*) FROM jobs WHERE job_status = 'RUNNING' AND started_at IS NULL"), 0)

    restart_and_drain("kill9-restart")


def case_after_insert():
    """Crash after the INSERT commits but before the job reaches memory (D-002): the client
    gets no answer, but the PENDING row exists and the next start runs it (at-least-once,
    D-003)."""
    # Breakpoint on the statement after CreateJob, found by its text so edits above it
    # do not move it. Still tied to that line of source: see TOP_OF_MIND known gaps.
    source = (ROOT / "src" / "job_service.cc").read_text().splitlines()
    line = next((i + 1 for i, text in enumerate(source) if "job_repo_.CreateJob(job);" in text), None)
    if line is None:
        check_true("found CreateJob call in JobService::Enqueue", False)
        return

    app = App("after_insert", [
        "gdb", "-q", "-batch",
        "-ex", "handle SIGPIPE nostop noprint",
        "-ex", f"break job_service.cc:{line + 1}",
        "-ex", "run", "-ex", "kill",
        "--args", str(BIN),
    ])
    if not wait_for(30, port_open):
        print(f"  app did not start under gdb, see {app.log_path}")
        return

    http_code = post(sleep_ms=10, timeout=15)
    app.wait()
    check("client got no response", http_code, 0)
    check("breakpoint hit before the crash", app.log().count("Breakpoint 1,"), 1)
    check("row committed as PENDING",
          sql_int("SELECT count(*) FROM jobs WHERE job_status = 'PENDING'"), 1)

    restart_and_drain("after_insert-restart")


def case_sigterm():
    """Graceful shutdown (D-018): in-flight jobs finish, queued jobs stay PENDING."""
    app = start_app("sigterm")
    if app is None:
        return
    post_jobs(N_JOBS)
    wait_for(10, lambda: running_count() > 0)
    running = sql("SELECT string_agg(id::text, ',') FROM jobs WHERE job_status = 'RUNNING'")
    app.signal(signal.SIGTERM)
    check("exit code 0", app.wait(), 0)
    check("log says Stopped cleanly", app.log().count("Stopped cleanly"), 1)

    check("no RUNNING rows left", running_count(), 0)
    check("jobs running at SIGTERM finished DONE",
          sql_int(f"SELECT count(*) FROM jobs WHERE id = ANY('{{{running}}}'::uuid[]) "
                  "AND job_status <> 'DONE'"), 0)
    check_true("queued jobs stayed PENDING",
               sql_int("SELECT count(*) FROM jobs WHERE job_status = 'PENDING'") > 0)

    restart_and_drain("sigterm-restart")


def case_db_restart():
    """Planned DB maintenance: app stopped, `docker compose down` + `up`. The volume keeps
    the data."""
    app = start_app("db_restart")
    if app is None:
        return
    post_jobs(N_JOBS)
    wait_for(10, lambda: running_count() > 0)
    app.signal(signal.SIGTERM)
    app.wait()

    counts = ("SELECT string_agg(job_status || ':' || n, ',' ORDER BY job_status) "
              "FROM (SELECT job_status, count(*) n FROM jobs GROUP BY 1) s")
    before = sql(counts)
    compose("down")  # never -v: that deletes the volume
    compose("up", "-d")
    if not wait_db():
        check_true("Postgres back up", False)
        return
    check("rows unchanged across compose down/up", sql(counts), before)

    restart_and_drain("db_restart-restart")


def case_db_restart_live():
    """Postgres restarts while the app runs (I-007, fixed in dec4697 + cbba132).

    The app must stay up: each thread reconnects on the call after a failure (D-007), so a
    few requests get 500 before POST works again.

    Fewer jobs than workers on purpose: the busy workers cover a Finish() that throws, and
    the idle ones cover a claim in WaitAndPop() that throws (their first DB call after the
    restart). With every worker busy, Finish() absorbs each worker's failure and no claim
    ever throws. A warm-up round first gives every worker a connection: the connection is
    opened lazily, so a worker that never ran a job would open a fresh one after the
    restart and not throw at all.

    Jobs whose Finish() threw stay RUNNING until the restart at the end (I-007 known limit).
    Jobs whose claim threw stay PENDING until the scheduler's next tick runs them.
    """
    n_warmup = NPROC  # one 2 s job per worker
    n_jobs = NPROC // 2 + 1
    app = start_app("db_restart_live")
    if app is None:
        return
    post_jobs(n_warmup)
    wait_for(15, lambda: sql_int("SELECT count(*) FROM jobs WHERE job_status <> 'DONE'") == 0)
    post_jobs(n_jobs)
    wait_for(10, lambda: running_count() > 0)
    compose("restart", "db")
    if not wait_db():
        check_true("Postgres back up", False)
        return
    restarted_at = sql("SELECT extract(epoch FROM now())")

    posted_ok = 0

    def post_ok():
        nonlocal posted_ok
        if post(sleep_ms=10) != 201:
            return False
        posted_ok += 1
        return True

    check_true("POST returns 201 again after the DB restart", wait_for(15, post_ok))
    # One job per worker, so every idle worker wakes up and claims on its dead connection.
    for _ in range(NPROC):
        post_ok()

    if wait_for(3, app.exited):
        check(f"app still running after the DB restart (exit code {app.wait()})", "no", "yes")
    else:
        check_true("app still running after the DB restart", True)

        done_after = ("SELECT count(*) FROM jobs WHERE job_status = 'DONE' "
                      f"AND created_at > to_timestamp({restarted_at})")
        check_true("jobs posted after the DB restart run to DONE",
                   wait_for(30, lambda: sql_int(done_after) > 0))

        # Claims that threw left PENDING rows nobody holds. The scheduler must run them while
        # the app keeps going (D-019); before it existed only a restart did. The first check
        # makes sure the case still produces such rows (see the warm-up note above).
        check_true("some claims threw on a dead connection",
                   "PENDING -> RUNNING skipped" in app.log())
        pending = "SELECT count(*) FROM jobs WHERE job_status = 'PENDING'"
        check_true("PENDING rows left by those claims run without a restart",
                   wait_for(15, lambda: sql_int(pending) == 0))

        app.signal(signal.SIGTERM)
        check("exit code 0 on SIGTERM", app.wait(), 0)
        check("log says Stopped cleanly", app.log().count("Stopped cleanly"), 1)
    note(f"RUNNING rows left: {running_count()}")

    check("no job lost", sql_int("SELECT count(*) FROM jobs"), n_warmup + n_jobs + posted_ok)
    restart_and_drain("db_restart_live-restart")


# ---------- main ----------

CASES = {
    "kill9": case_kill9,
    "after_insert": case_after_insert,
    "sigterm": case_sigterm,
    "db_restart": case_db_restart,
    "db_restart_live": case_db_restart_live,
}


def cleanup():
    if current_app is not None and not current_app.exited():
        current_app.proc.kill()
    subprocess.run(["pkill", "-9", "-f", f"gdb .*{BIN}"], capture_output=True)


def main():
    sys.stdout.reconfigure(line_buffering=True)  # show each result as it happens, even when piped
    names = sys.argv[1:] or list(CASES)
    unknown = [n for n in names if n not in CASES]
    if unknown:
        print(f"Unknown case: {unknown[0]} (cases: {' '.join(CASES)})")
        return 1
    if not os.access(BIN, os.X_OK):
        print(f"No binary at {BIN}; run 'make build' first")
        return 1
    if port_open():
        print(f"Port {PORT} is in use; stop the other jobqueue first")
        return 1
    if not wait_db():
        print("Postgres is not up; run 'docker compose up -d' and 'make migrate'")
        return 1
    shutil.rmtree(LOG_DIR, ignore_errors=True)
    LOG_DIR.mkdir(parents=True)

    try:
        for name in names:
            print(f"== {name}")
            sql("TRUNCATE jobs, dead_letter_jobs")
            CASES[name]()
    finally:
        cleanup()

    print(f"== {passes} passed, {fails} failed (logs in {LOG_DIR.relative_to(ROOT)})")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
