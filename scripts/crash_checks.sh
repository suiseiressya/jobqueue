#!/usr/bin/env bash
# End-to-end crash checks (S3, task F). Each case runs the real binary against the
# `test` DB, breaks it at a bad moment, and checks the state left behind with SQL.
#
# Usage: scripts/crash_checks.sh [kill9|after_insert|sigterm|db_restart|db_restart_live ...]
#        (no arguments runs every case; `make crash` builds first)
#
# Rules:
#   - Uses port 8080 and TRUNCATEs the `test` DB: do not run it next to `make test`
#     or another jobqueue process.
#   - db_restart and db_restart_live restart the shared Postgres container. Data stays
#     (the volume is never removed), but anything else using the DB loses its connection.
#   - Requires the Debug build (after_insert puts a gdb breakpoint in JobService::Enqueue).

set -u
cd "$(dirname "$0")/.."

CONN="host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"
BIN=./build/jobqueue
PORT=8080
LOG_DIR=build/crash-logs
# More jobs than workers (one worker per hardware thread), so some stay PENDING.
N_JOBS=$(( $(nproc) * 2 + 5 ))
SLEEP_MS=2000

PASSES=0
FAILS=0
APP_PID=""
APP_LOG=""
APP_EXIT=""

# ---------- helpers ----------

sql() {
    docker compose exec -T db psql -U jobqueue -d test -tAc "$1" | tr -d '[:space:]'
}

check() {  # check <description> <actual> <expected>
    if [[ "$2" == "$3" ]]; then
        echo "  PASS  $1"
        PASSES=$((PASSES + 1))
    else
        echo "  FAIL  $1 (got '$2', expected '$3')"
        FAILS=$((FAILS + 1))
    fi
}

note() { echo "  NOTE  $1"; }

# kill -0 also succeeds on an exited child that has not been reaped yet (zombie), so read its state.
app_exited() {
    local state
    state=$(ps -o stat= -p "$APP_PID" 2>/dev/null)
    [[ -z "$state" || "$state" == Z* ]]
}

port_open() { (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; }

wait_for() {  # wait_for <timeout seconds> <command...>: poll every 0.2 s until the command succeeds
    local deadline=$(( SECONDS + $1 )); shift
    until "$@"; do
        (( SECONDS >= deadline )) && return 1
        sleep 0.2
    done
}

sql_is() { [[ "$(sql "$1")" == "$2" ]]; }
sql_positive() { (( $(sql "$1") > 0 )); }

wait_db() {
    wait_for 30 docker compose exec -T db pg_isready -U jobqueue -d test >/dev/null 2>&1
}

start_app() {  # start_app <log name>
    APP_LOG="$LOG_DIR/$1.log"
    JOBQUEUE_PG_CONN="$CONN" "$BIN" >>"$APP_LOG" 2>&1 &
    APP_PID=$!
    wait_for 15 port_open || { echo "  app did not start, see $APP_LOG"; return 1; }
}

# Wait for the app to exit; its exit code goes to APP_EXIT. Not usable inside $( ):
# a subshell is not the app's parent, so `wait` there returns 127 at once.
wait_app() {
    APP_EXIT=0
    { wait "$APP_PID"; } 2>/dev/null || APP_EXIT=$?
    APP_PID=""
}

post_jobs() {  # post_jobs <count>
    for _ in $(seq "$1"); do
        curl -s -o /dev/null -X POST "localhost:$PORT/jobs" -H 'Content-Type: application/json' \
            -d "{\"payload\":\"{\\\"sleep_ms\\\":$SLEEP_MS}\"}"
    done
}

recovered_count() { grep -o 'Recovered [0-9]* jobs' "$APP_LOG" | tail -1 | grep -o '[0-9]*'; }

# Restart the app, check it recovered every unfinished row, wait for all jobs to be DONE,
# then stop it.
restart_and_drain() {  # restart_and_drain <log name>
    local unfinished
    unfinished=$(sql "SELECT count(*) FROM jobs WHERE job_status IN ('PENDING', 'RUNNING')")
    start_app "$1" || return
    check "restart recovered every unfinished job" "$(recovered_count)" "$unfinished"
    wait_for 60 sql_is "SELECT count(*) FROM jobs WHERE job_status <> 'DONE'" 0
    check "all jobs DONE after restart" "$(sql "SELECT count(*) FROM jobs WHERE job_status <> 'DONE'")" 0
    check "every job has started_at and completed_at" \
        "$(sql "SELECT count(*) FROM jobs WHERE started_at IS NULL OR completed_at IS NULL")" 0
    kill -TERM "$APP_PID"
    wait_app
}

cleanup() {
    [[ -n "$APP_PID" ]] && kill -9 "$APP_PID" 2>/dev/null
    pkill -9 -f "gdb .*$BIN" 2>/dev/null
}
trap cleanup EXIT

# ---------- cases ----------

# Crash mid-execute: RUNNING rows are left behind, restart recovery re-runs them.
case_kill9() {
    start_app kill9 || return
    post_jobs "$N_JOBS"
    wait_for 10 sql_positive "SELECT count(*) FROM jobs WHERE job_status = 'RUNNING'"
    kill -9 "$APP_PID"
    wait_app
    check "app killed by SIGKILL" "$APP_EXIT" 137

    check "no job lost" "$(sql "SELECT count(*) FROM jobs")" "$N_JOBS"
    if sql_positive "SELECT count(*) FROM jobs WHERE job_status = 'RUNNING'"; then
        check "RUNNING rows left behind" yes yes
    else
        check "RUNNING rows left behind" no yes
    fi
    check "RUNNING rows have started_at" \
        "$(sql "SELECT count(*) FROM jobs WHERE job_status = 'RUNNING' AND started_at IS NULL")" 0

    restart_and_drain kill9-restart
}

# Crash after the INSERT commits but before the job reaches memory (D-002): the client
# gets no answer, but the PENDING row exists and the next start runs it (at-least-once, D-003).
case_after_insert() {
    # Breakpoint on the statement after CreateJob, found by its text so edits above it
    # do not move it. Still tied to that line of source: see TOP_OF_MIND known gaps.
    local line
    line=$(grep -n 'job_repo_.CreateJob(job);' src/job_service.cc | head -1 | cut -d: -f1)
    [[ -n "$line" ]] || { check "found CreateJob call in JobService::Enqueue" no yes; return; }

    APP_LOG="$LOG_DIR/after_insert.log"
    JOBQUEUE_PG_CONN="$CONN" gdb -q -batch \
        -ex 'handle SIGPIPE nostop noprint' \
        -ex "break job_service.cc:$((line + 1))" \
        -ex run -ex kill \
        --args "$BIN" >>"$APP_LOG" 2>&1 &
    APP_PID=$!
    wait_for 30 port_open || { echo "  app did not start under gdb, see $APP_LOG"; return; }

    local http_code
    http_code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 15 -X POST "localhost:$PORT/jobs" \
        -H 'Content-Type: application/json' -d '{"payload":"{\"sleep_ms\":10}"}')
    wait_app
    check "client got no response" "$http_code" 000
    check "breakpoint hit before the crash" "$(grep -c 'Breakpoint 1,' "$APP_LOG")" 1
    check "row committed as PENDING" "$(sql "SELECT count(*) FROM jobs WHERE job_status = 'PENDING'")" 1

    restart_and_drain after_insert-restart
}

# Graceful shutdown (D-018): in-flight jobs finish, queued jobs stay PENDING.
case_sigterm() {
    start_app sigterm || return
    post_jobs "$N_JOBS"
    wait_for 10 sql_positive "SELECT count(*) FROM jobs WHERE job_status = 'RUNNING'"
    local running
    running=$(sql "SELECT string_agg(id::text, ',') FROM jobs WHERE job_status = 'RUNNING'")
    kill -TERM "$APP_PID"
    wait_app
    check "exit code 0" "$APP_EXIT" 0
    check "log says Stopped cleanly" "$(grep -c 'Stopped cleanly' "$APP_LOG")" 1

    check "no RUNNING rows left" "$(sql "SELECT count(*) FROM jobs WHERE job_status = 'RUNNING'")" 0
    check "jobs running at SIGTERM finished DONE" \
        "$(sql "SELECT count(*) FROM jobs WHERE id = ANY('{$running}'::uuid[]) AND job_status <> 'DONE'")" 0
    if sql_positive "SELECT count(*) FROM jobs WHERE job_status = 'PENDING'"; then
        check "queued jobs stayed PENDING" yes yes
    else
        check "queued jobs stayed PENDING" no yes
    fi

    restart_and_drain sigterm-restart
}

# Planned DB maintenance: app stopped, `docker compose down` + `up`. The volume keeps the data.
case_db_restart() {
    start_app db_restart || return
    post_jobs "$N_JOBS"
    wait_for 10 sql_positive "SELECT count(*) FROM jobs WHERE job_status = 'RUNNING'"
    kill -TERM "$APP_PID"
    wait_app

    local before
    before=$(sql "SELECT string_agg(job_status || ':' || n, ',' ORDER BY job_status) FROM (SELECT job_status, count(*) n FROM jobs GROUP BY 1) s")
    docker compose down >/dev/null 2>&1   # never -v: that deletes the volume
    docker compose up -d >/dev/null 2>&1
    wait_db || { check "Postgres back up" no yes; return; }
    check "rows unchanged across compose down/up" \
        "$(sql "SELECT string_agg(job_status || ':' || n, ',' ORDER BY job_status) FROM (SELECT job_status, count(*) n FROM jobs GROUP BY 1) s")" \
        "$before"

    restart_and_drain db_restart-restart
}

# Postgres restarts while the app runs. Records today's behaviour (I-007): connections do
# not reconnect (D-007), so the app aborts when a worker's claim throws outside the try in
# WorkerLoop. Hard checks are only the DB-first guarantees: nothing lost, a restart heals it.
case_db_restart_live() {
    start_app db_restart_live || return
    post_jobs "$N_JOBS"
    wait_for 10 sql_positive "SELECT count(*) FROM jobs WHERE job_status = 'RUNNING'"
    docker compose restart db >/dev/null 2>&1
    wait_db || { check "Postgres back up" no yes; return; }

    local http_code
    http_code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 -X POST "localhost:$PORT/jobs" \
        -H 'Content-Type: application/json' -d '{"payload":"{\"sleep_ms\":10}"}')
    note "POST after the DB came back returned HTTP $http_code (I-007: expected 500 or 000)"

    if wait_for 20 app_exited; then
        wait_app
        note "app exited with code $APP_EXIT (I-007: expected 134, std::terminate)"
    else
        note "app still running 20 s after the DB restart; killing it"
        kill -9 "$APP_PID"
        wait_app
    fi
    note "RUNNING rows left: $(sql "SELECT count(*) FROM jobs WHERE job_status = 'RUNNING'")"

    check "no job lost" "$(sql "SELECT count(*) FROM jobs")" "$N_JOBS"
    restart_and_drain db_restart_live-restart
}

# ---------- main ----------

ALL_CASES=(kill9 after_insert sigterm db_restart db_restart_live)
CASES=("$@")
(( ${#CASES[@]} )) || CASES=("${ALL_CASES[@]}")

[[ -x "$BIN" ]] || { echo "No binary at $BIN; run 'make build' first"; exit 1; }
port_open && { echo "Port $PORT is in use; stop the other jobqueue first"; exit 1; }
wait_db || { echo "Postgres is not up; run 'docker compose up -d' and 'make migrate'"; exit 1; }
rm -rf "$LOG_DIR" && mkdir -p "$LOG_DIR"

for c in "${CASES[@]}"; do
    declare -F "case_$c" >/dev/null || { echo "Unknown case: $c (cases: ${ALL_CASES[*]})"; exit 1; }
    echo "== $c (jobs: $N_JOBS x ${SLEEP_MS} ms)"
    sql "TRUNCATE jobs" >/dev/null
    "case_$c"
done

echo "== $PASSES passed, $FAILS failed (logs in $LOG_DIR)"
(( FAILS == 0 ))