#include <cstdlib>
#include <iostream>
#include <pqxx/pqxx>
#include <string>

#include "http_server.h"
#include "thread_pool.h"

int main() {
    const char* connection_env = std::getenv("JOBQUEUE_PG_CONN");
    if (connection_env == nullptr) {
        std::cerr << "JOBQUEUE_PG_CONN is not set.\n";
        return 1;
    }
    std::string connection_string = connection_env;

    try {
        pqxx::connection db{connection_string};
        std::cout << "Connected to Postgres db=" << db.dbname() << " user=" << db.username()
                  << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect to Postgres: " << e.what() << "\n";
        std::cerr << "Tried connection string: " << connection_string << "\n";
        std::cerr << "Set JOBQUEUE_PG_CONN or configure ~/.pg_service.conf with [jobqueue].\n";
        return 1;
    }

    JobRepository job_repo(connection_string);
    JobService job_service(job_repo);
    HttpServer server(job_service);
    ThreadPool thread_pool(job_service);

    thread_pool.Start();

    std::cout << "Server listening on http://localhost:8080\n";
    server.Listen("0.0.0.0", 8080);  // blocks until server.Stop() is called

    thread_pool.Stop();
}
