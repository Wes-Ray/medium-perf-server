#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <filesystem>
#include <fstream>

#include "config.h"

// curl -X POST -F "file=@file.upload" http://localhost:8080/upload
// curl localhost:8080/download --output download.file

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

int upload_file_from_client(HttpRequest* req, int socket) {

    auto content_type_it = req->header_lines.find("content-type");
    if (content_type_it == req->header_lines.end()) {
        std::string response = "HTTP/1.0 400 Bad Request\r\n\r\n";
        send(socket, response.c_str(), response.length(), 0);
        return 1;
    }

    std::string boundary;
    size_t bound_pos = content_type_it->second.find("boundary=");
    if (bound_pos != std::string::npos) {
        boundary = content_type_it->second.substr(bound_pos + 9);
    }

    char buffer[4096];
    std::string file_content;
    while (true) {
        ssize_t bytes = recv(socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            break;
        }
        file_content.append(buffer, bytes);
    }

    size_t file_start = file_content.find("\r\n\r\n");
    if (file_start != std::string::npos) {
        file_start += 4;  // skip \r\n\r\n

        size_t file_end = file_content.find(boundary, file_start);
        if (file_end != std::string::npos) {
            file_end -= 4;  // \r\n--

            std::string file_data = file_content.substr(file_start, file_end - file_start);

            std::string filename = upload_dir / "uploaded.file";
            
            std::ofstream outfile(filename, std::ios::binary);
            if (outfile.is_open()) {
                outfile.write(file_data.c_str(), file_data.size());
                outfile.close();

                std::string response = 
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 15\r\n"
                    "\r\n"
                    "Upload Success\n";
                send(socket, response.c_str(), response.length(), 0);
            } else {
                std::string response = "HTTP/1.0 500 Internal Server Error\r\n\r\n";
                send(socket, response.c_str(), response.length(), 0);
                return 1;
            }
        }
    }

    return 0;
}

int download_file_to_client(HttpRequest* req, int socket) {

    // TODO: just using a file directly for now... could specify a file 

    fs::path down_file = file_dir / "Wireshark-4.4.1-x64.exe";

    int fd = open(down_file.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "Failed to open file" << std::endl;
        return 1;
    }

    struct stat stat_buf;
    int ret = stat(down_file.c_str(), &stat_buf);
    if (ret != 0) {
        std::cerr << "Failed to get file size" << std::endl;
        return 1;
    } 
    off_t file_size = stat_buf.st_size;
    // const size_t file_size = 100 * 1024 * 1024;  //10MB

    std::string headers =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: " + std::to_string(file_size) + "\r\n"
            "\r\n";

    send(socket, headers.c_str(), headers.length(), 0);

    off_t offset = 0;
    ssize_t sent = sendfile(socket, fd, &offset, file_size);
    if (sent < 0) {
        std::cerr << "sendfile failed: " << strerror(errno) << std::endl;
        close (fd);
        return 1;
    }

    close(fd);

    return 0;
}

int process_request(HttpRequest* req, int socket) {
    std::cout << "SENDING RESPONSE" << std::endl;

    if (req->path == "/upload") {
        std::cout << "Uploading..." << std::endl;

        int ret = upload_file_from_client(req, socket);
        if (ret != 0) {
            return ret;
        }

        std::cout << "Upload complete" << std::endl;
    } 

    else if (req->path == "/download") {
        std::cout << "Downloading..." << std::endl;

        int ret = download_file_to_client(req, socket);
        if (ret != 0) {
            return ret;
        }

        std::cout << "Download complete" << std::endl;
    } 

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
        if (new_socket < 0) {
           std::cerr << "Accept failed" << std::endl;
            continue;
        }


        struct timeval timeout;
        timeout.tv_sec = 3;  // seconds timeout
        timeout.tv_usec = 0;
        if (setsockopt(new_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) < 0) {
            std::cerr << "Setting timeout failed" << std::endl;
            return -1;
        }
        
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

        if (0 != process_request(&req, new_socket)) {
            std::cerr << "error sending the response" << std::endl;
            return 1;
        }

        close(new_socket);
    }

    return 0;
}