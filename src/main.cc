#include <signal.h>
#include <unistd.h>

#include <pqxx/pqxx>
#include <string>
#include <syncstream>

#include "http_server.h"
#include "scheduler.h"
#include "thread_pool.h"

int main() {
    // Block SIGINT/SIGTERM before any thread starts so every thread inherits
    // the mask and only the sigwait thread below receives them 
    sigset_t shutdown_signals;
    sigemptyset(&shutdown_signals);
    sigaddset(&shutdown_signals, SIGINT);
    sigaddset(&shutdown_signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &shutdown_signals, nullptr);

    const char* connection_env = std::getenv("JOBQUEUE_PG_CONN");
    if (connection_env == nullptr) {
        std::osyncstream(std::cerr) << "JOBQUEUE_PG_CONN is not set.\n";
        return 1;
    }
    std::string connection_string = connection_env;

    try {
        pqxx::connection db{connection_string};
        std::cout << "Connected to Postgres db=" << db.dbname() << " user=" << db.username()
                  << "\n";
    } catch (const std::exception& e) {
        std::osyncstream(std::cerr) << "Failed to connect to Postgres: " << e.what() << "\n";
        std::osyncstream(std::cerr) << "Tried connection string: " << connection_string << "\n";
        std::osyncstream(std::cerr) << "Set JOBQUEUE_PG_CONN or configure ~/.pg_service.conf with [jobqueue].\n";
        return 1;
    }

    JobRepository job_repo(connection_string);
    JobService job_service(job_repo);
    HttpServer server(job_service);
    ThreadPool thread_pool(job_service);
    // Brings back retried jobs once due and PENDING rows nobody holds (D-019).
    Scheduler scheduler(job_service, std::chrono::seconds(1));

    try {
        size_t n = job_service.RestartRecovery();
        std::cout << "Recovered " << n << " jobs" << std::endl;
    }
    catch (std::exception& e) {
        std::osyncstream(std::cerr) << "Failed to run restart recovery " << e.what() << std::endl;
        std::osyncstream(std::cerr) << "Stopping job queue" << std::endl;
        return 1; 
    }

    thread_pool.Start();
    scheduler.Start();  // after the signal mask above, like every thread

    // httplib's stop() is a no-op before listen() is running, so a signal that
    // lands too early is lost. Keep waiting: the next signal stops the server.
    std::atomic_bool listen_returned{false};
    std::thread signal_thread([&] {
        int sig = 0;
        while (sigwait(&shutdown_signals, &sig) == 0 && !listen_returned) {
            std::osyncstream(std::cerr) << "Received signal " << sig << ", shutting down\n";
            server.Stop();
        }
    });

    std::cout << "Server listening on http://localhost:8080" << std::endl;
    bool listened = server.Listen("0.0.0.0", 8080);  // blocks until server.Stop() is called
    if (!listened) std::osyncstream(std::cerr) << "Failed to listen on port 8080\n";

    // Wake the signal thread if Listen returned on its own (e.g. bind failure).
    listen_returned = true;
    kill(getpid(), SIGTERM);
    signal_thread.join();

    // Scheduler before pool: no new jobs are queued while workers shut down.
    scheduler.Stop();

    // In-flight jobs finish; PENDING jobs stay in the DB for the next start.
    thread_pool.Stop();
    std::cout << "Stopped cleanly\n";
    return listened ? 0 : 1;
}
