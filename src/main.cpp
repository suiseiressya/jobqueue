#include "HttpServer.hpp"

#include <iostream>

int main() {
    JobService jobService;
    HttpServer server(jobService);

    std::cout << "Server listening on http://localhost:8080\n";
    server.listen("0.0.0.0", 8080);
}