#include "http_server.h"

#include <iostream>

int main() {
    JobService job_service;
    HttpServer server(job_service);

    std::cout << "Server listening on http://localhost:8080\n";
    server.Listen("0.0.0.0", 8080);
}