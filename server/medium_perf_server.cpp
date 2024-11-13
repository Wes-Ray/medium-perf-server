#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <errno.h>
#include <filesystem>

#include "config.h"

namespace fs = std::filesystem;
fs::path project_root = "/home/dev/dev/medium-performance-server";
fs::path file_dir = project_root / "files";
fs::path upload_dir = project_root / "upload";

enum ParseState {
    METHOD,
    PATH,
    VERSION,
    HEADER_KEY,
    HEADER_VALUE,
    DONE,
};

struct HttpRequest {
    ParseState  state;
    std::string method;
    std::string path;
    std::string version;

    std::string current_header_key;
    std::map<std::string, std::string> header_lines;
};

int parse_http_request(HttpRequest* req, const char* buf) {
    req->state = ParseState::METHOD;
    std::string token;

    for (int i = 0; buf[i] != '\0'; i++) {

        if (buf[i] == '\r') {
            continue;
        }

        if (buf[i] == '\n' || buf[i] == ' ') {
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
                    req->state = ParseState::HEADER_KEY;
                    break;
                case ParseState::HEADER_KEY:
                    if (!token.empty()) {
                        req->current_header_key = token;
                        // remove : at end of key
                        req->current_header_key.pop_back();
                        // lowercase
                        std::transform( req->current_header_key.begin(), 
                                        req->current_header_key.end(), 
                                        req->current_header_key.begin(), 
                                        ::tolower);
                        req->state = ParseState::HEADER_VALUE;
                    } else if (buf[i] == '\n') {
                        req->state = ParseState::DONE;
                    }
                    break;
                case ParseState::HEADER_VALUE:
                    req->header_lines[req->current_header_key] = token;
                    req->state = ParseState::HEADER_KEY;
                    break;
                case ParseState::DONE:
                    break;
            }
            token.clear();
            continue;
        }

        token += buf[i];
    }

    req->current_header_key.clear();

    return 0;
}

int send_response(HttpRequest* req, int socket) {
    std::cout << "SENDING RESPONSE" << std::endl;

    //
    // Upload
    //
    if (req->path == "/upload") {
        std::cout << "Uploading..." << std::endl;

        std::cout << "Uploading..." << std::endl;
        std::string response = 
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 7\r\n"
            "\r\n"
            "Upload\n";
        send(socket, response.c_str(), response.length(), 0);
    }
    //
    // Download
    //
    else if (req->path == "/download") {

        std::cout << "Downloading..." << std::endl;

        const size_t file_size = 100 * 1024 * 1024;  //10MB

        std::string headers =
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: " + std::to_string(file_size) + "\r\n"
                "\r\n";

        send(socket, headers.c_str(), headers.length(), 0);

        // TODO: just using a file directly for now... could specify a file 
        int fd = open((file_dir / "Wireshark-4.4.1-x64.exe").c_str(), O_RDONLY);
        if (fd < 0) {
            std::cerr << "Failed to open file" << std::endl;
            return 1;
        }

        off_t offset = 0;
        ssize_t sent = sendfile(socket, fd, &offset, file_size);
        if (sent < 0) {
            std::cerr << "sendfile failed: " << strerror(errno) << std::endl;
            close (fd);
            return 1;
        }

        close(fd);
    }
    //
    // Unsupported path
    //
    else {
        std::cout << "Bad Path" << std::endl;
        std::string response = 
            "HTTP/1.0 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n"
            "\r\n"
            "Bad Path\n";
        send(socket, response.c_str(), response.length(), 0);
    }

    return 0;
}

int process_buffer(const char* buf, HttpRequest* req) {
    std::cout << "PROCESSING\n----------" << std::endl;
    std::cout << buf << std::endl;
    std::cout << "----------\nEND PROCESSING" << std::endl;

    if (0 != parse_http_request(req, buf)) {
        std::cerr << "error parsing request" << std::endl;
        return 1;
    }

    std::cout << "\nPROCESSED\n----------" << std::endl;
    std::cout << "method: " << req->method << std::endl;
    std::cout << "path: " << req->path << std::endl;
    std::cout << "version: " << req->version << std::endl;
    std::cout << "Header Lines... [" << req->header_lines.size() << "]" << std::endl;
    for (const auto& pair : req->header_lines) {
        std::cout << "\t" << pair.first << ": " << pair.second << std::endl;
    }
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

        HttpRequest req;
        int ret = process_buffer(buffer, &req);
        if (ret != 0) {
            std::cerr << "error processing buffer" << std::endl;
            return 1;
        }

        if (0 != send_response(&req, new_socket)) {
            std::cerr << "error sending the response" << std::endl;
            return 1;
        }

        close(new_socket);
    }

    return 0;
}