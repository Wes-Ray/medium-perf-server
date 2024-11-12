#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#include "config.h"

enum ParseState {
    METHOD,
    PATH,
    VERSION,
    DONE
};

struct HttpRequest {
    ParseState state;
    std::string method;
    std::string path;
    std::string version;
};

int parse_http_request(HttpRequest* req, const char* buf) {
    req->state = ParseState::METHOD;
    std::string token;


    for (int i=0; req->state != ParseState::DONE && buf[i] != '\0'; i++) {
        // collect token until delimiter hit
        if (buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') {
            token += buf[i];
            continue;
        }

        switch (req->state) {
            case ParseState::METHOD:
                req->method = token;
                req->state = ParseState::PATH;
                break;
            case ParseState::PATH:
                req->path = token;
                req->state = ParseState::VERSION;
                break;
            case ParseState::VERSION:
                req->version = token;
                req->state = ParseState::DONE;
                break;
            case ParseState::DONE:
                break;
        }
        token.clear();
    }

    return 0;
}

int process_buffer(const char* buf) {
    std::cout << "PROCESSING\n----------" << std::endl;
    std::cout << buf << std::endl;
    std::cout << "----------\nEND PROCESSING" << std::endl;

    HttpRequest req;
    if (0 != parse_http_request(&req, buf)) {
        std::cerr << "error parsing request" << std::endl;
        return 1;
    }

    std::cout << "\nPROCESSED\n----------" << std::endl;

    std::cout << "method: " << req.method << std::endl;
    std::cout << "path: " << req.path << std::endl;
    std::cout << "version: " << req.version << std::endl;

    std::cout << "----------\nEND PROCESSED" << std::endl;

    return 0;
}


int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return -1;
    }

    // Enable socket re-use
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Setsockopt failed" << std::endl;
        return -1;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return -1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return -1;
    }

    std::cout << "Server listening on port " << SERVER_PORT << "..." << std::endl;

    while (true) {
        int new_socket;
        int addrlen = sizeof(address);
        new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        
        const int buffer_size = 1024;  // TODO: experiment with smaller buffers
        char buffer[buffer_size] = {0};
        int valread = read(new_socket, buffer, buffer_size);
        if (valread < 0) {
            std::cerr << "Error reading from socket" << std::endl;
            close(new_socket);
            continue;
        }
        if (valread == 0) {
            // Client closed connection
            std::cout << "Client disconnected" << std::endl;
            close(new_socket);
            continue;
        }

        // std::cout << "Received (processing): " << buffer << std::endl;

        int ret = process_buffer(buffer);

        const char *success =   "HTTP/1.0 200 OK\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: 8\r\n"
                                "\r\n"
                                "success\n";

        const char *failure =   "HTTP/1.0 200 OK\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: 8\r\n"
                                "\r\n"
                                "failure\n";

        if (ret == 0) {
            send(new_socket, success, strlen(success), 0);
        } else {
            send(new_socket, failure, strlen(failure), 0);
        }
        
        close(new_socket);
    }

    return 0;
}