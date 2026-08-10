# Job Queue

Only works on Linux/WSL

## Quick start

```bash
make build      # build
make start      # rerun without build
make run        # build + run
make clean      
make distclean
```

## Database setup

Create `~/.pg_service.conf`:

```ini
[jobqueue]
host=localhost
port=5432
dbname=jobqueue
user=youruser
```

Put the password in `~/.pgpass`:

```bash
echo "localhost:5432:jobqueue:youruser:yourpassword" >> ~/.pgpass
chmod 600 ~/.pgpass
```

## Tests

```bash
make test        # unit + integration
make tsan        # ThreadSanitizer
make helgrind    # thread errors
make memcheck    # memory leaks
```