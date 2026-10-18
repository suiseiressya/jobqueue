# Job Queue

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

## AWS Setup

### Jobqueue instance

1. Launch EC2: Ubuntu 24.04, `t3.small`.
2. Security group: inbound 22 and 8080 from your IP. Do not open 5433 or 9399.
3. Connect from your machine:

   ```bash
   chmod 400 ~/.ssh/jobqueue.pem
   ssh -i ~/.ssh/jobqueue.pem ubuntu@<JOBQUEUE_PUBLIC_IP>
   ```

4. Install, then log in again:

   ```bash
   sudo apt update && sudo apt install -y build-essential cmake git pkg-config uuid-dev \
       libpq-dev docker.io docker-compose-v2 postgresql-client
   sudo usermod -aG docker ubuntu
   exit
   ```

5. Build:

   ```bash
   git clone https://github.com/suiseiressya/jobqueue.git && cd jobqueue
   docker compose up -d --wait
   make migrate
   cmake -B build-release -DCMAKE_BUILD_TYPE=Release
   cmake --build build-release --target jobqueue -j2
   ```

6. Run:

   ```bash
   sudo tee /etc/systemd/system/jobqueue.service >/dev/null <<'UNIT'
   [Unit]
   Description=jobqueue
   After=docker.service network-online.target
   Requires=docker.service

   [Service]
   User=ubuntu
   WorkingDirectory=/home/ubuntu/jobqueue
   Environment="JOBQUEUE_PG_CONN=host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"
   ExecStartPre=/usr/bin/docker compose up -d --wait
   ExecStart=/home/ubuntu/jobqueue/build-release/jobqueue
   Restart=on-failure
   KillSignal=SIGTERM

   [Install]
   WantedBy=multi-user.target
   UNIT
   sudo systemctl daemon-reload
   sudo systemctl enable --now jobqueue
   journalctl -u jobqueue -f
   ```

7. Test:

   ```bash
   curl -s -X POST localhost:8080/jobs -H 'Content-Type: application/json' -d '{"payload":"{}"}'
   ```

8. To Run Grafana and Prometheus from your machine:

   ```bash
   ssh -i ~/.ssh/jobqueue.pem -N -L 3000:localhost:3000 -L 9090:localhost:9090 ubuntu@<JOBQUEUE_PUBLIC_IP>
   ```
   
   Then open http://localhost:3000 and http://localhost:9090 normally.

Deploy a new commit:

```bash
cd ~/jobqueue && git pull
docker compose up -d --wait && make migrate
cmake --build build-release --target jobqueue -j2
sudo systemctl restart jobqueue
```

### k6 instance: load testing

1. Launch EC2: Ubuntu 24.04, `t3.small`, same subnet and key pair as the jobqueue instance.
2. Security groups: k6 inbound 22 from your IP; jobqueue inbound 8080 from the k6 security group.
3. Get the server's private IP: `hostname -I` on the jobqueue instance.
4. On the k6 instance:

   ```bash
   sudo apt update && sudo apt install -y docker.io
   ```

5. On the jobqueue instance, turn the rate limiter off:

   ```bash
   sudo systemctl edit jobqueue
   # add:
   #   [Service]
   #   Environment=JOBQUEUE_RATE_LIMIT_RPS=0
   sudo systemctl restart jobqueue
   ```

6. Copying the benchmark file to k6:

   ```bash
   scp -i ~/.ssh/jobqueue.pem benchmarks/post_jobs.js ubuntu@<K6_PUBLIC_IP>:~/

   # or: just paste content directly with scp doesn't work
   ```

7. Run load testing on k6:

   ```bash
   sudo mkdir -p /opt/bench && sudo cp ~/post_jobs.js /opt/bench/
   sudo chmod 755 /opt/bench && sudo chmod 644 /opt/bench/post_jobs.js
   cd ~
   for i in 1 2 3; do
     sudo docker run --rm -i --network host -v /opt/bench:/bench:ro grafana/k6:1.1.0 \
       run -e BASE_URL=http://<SERVER_PRIVATE_IP>:8080 /bench/post_jobs.js | tee run$i.txt
   done
   ```

8. To copy the run results to your machine: 

   ```bash
   scp -i ~/.ssh/jobqueue.pem 'ubuntu@<K6_PUBLIC_IP>:~/run*.txt' .
   ```

9. On the jobqueue instance, turn the rate limiter back on:

   ```bash
   sudo systemctl revert jobqueue && sudo systemctl restart jobqueue
   ```
