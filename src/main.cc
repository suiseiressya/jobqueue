#include <iostream>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <cstdlib>

#include "http_server.h"
#include "thread_pool.h"

int main() {
    std::optional<pqxx::connection> db;
    std::string connection_string = std::getenv("JOBQUEUE_PG_CONN");

    try {
        db.emplace(connection_string);
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect to Postgres: " << e.what() << "\n";
        std::cerr << "Tried connection string: " << connection_string << "\n";
        std::cerr << "Set JOBQUEUE_PG_CONN or configure ~/.pg_service.conf with [jobqueue].\n";
        return 1;
    }

    std::cout << "Connected to Postgres db=" << db->dbname() << " user=" << db->username() << "\n";

    JobService job_service;
    HttpServer server(job_service);
    ThreadPool thread_pool(job_service);

    thread_pool.Start();

    std::cout << "Server listening on http://localhost:8080\n";
    server.Listen("0.0.0.0", 8080);  // blocks until server.Stop() is called

    thread_pool.Stop();
}
