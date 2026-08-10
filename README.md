# Job Queue

Only works on Linux/WSL (not tested on Windows)

## Quick start

Database spinup:

```bash
docker compose up
```

Then:

```bash
make build      # build
make start      # rerun without build
make run        # build + run
make clean      
make distclean
```

## Database setup

DBeaver settings:
```bash
Host: localhost
Port: 5433
Database: jobqueue

Authentication: Username/password
Username: jobqueue
Password: jobqueue

Or: jdbc:postgresql://localhost:5433/jobqueue
```

## Tests

```bash
make test        # unit + integration
make tsan        # ThreadSanitizer
make helgrind    # thread errors
make memcheck    # memory leaks
```