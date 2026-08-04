#include "http_server.h"
#include "thread_pool.h"

#include <iostream>

int main() {
    JobService job_service;
    HttpServer server(job_service);
    ThreadPool thread_pool(job_service);

    thread_pool.Start();

    std::cout << "Server listening on http://localhost:8080\n";
    server.Listen("0.0.0.0", 8080);  // blocks until server.Stop() is called

    thread_pool.Stop();
}