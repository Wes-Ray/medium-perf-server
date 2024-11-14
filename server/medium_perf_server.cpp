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
#include <sys/epoll.h>

#include "config.h"

// curl -X POST -F "file=@file.upload" http://localhost:8080/upload
// curl localhost:8080/download --output download.file

namespace fs = std::filesystem;
// fs::path project_root = "/home/dev/dev/medium-performance-server";
fs::path project_root = "/home/wes/dev/medium-perf-server";
fs::path file_dir = project_root / "files";
fs::path upload_dir = project_root / "upload";

const int MAX_EVENTS = 10;
const int MAX_BUFFER_SIZE = 4096;

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

    fs::path down_file = file_dir / "download-file.exe";

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

std::string read_from_socket(int fd) {
    std::string data;
    char buffer[MAX_BUFFER_SIZE];
    
    while (true) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data available right now
                break;
            }
            // Real error occurred
            throw std::runtime_error("Read error");
        }
        if (bytes_read == 0) {
            // Connection closed
            throw std::runtime_error("Connection closed");
        }
        data.append(buffer, bytes_read);
    }
    return data;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
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

    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return -1;
    }

    // Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "Epoll creation failed" << std::endl;
        return -1;
    }

    // Add server socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // Edge-triggered mode
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        std::cerr << "Failed to add server socket to epoll" << std::endl;
        return -1;
    }

    std::cout << "Server listening on port " << SERVER_PORT << "..." << std::endl;

    // Event loop
    struct epoll_event events[MAX_EVENTS];
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            std::cerr << "Epoll wait failed" << std::endl;
            continue;
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
                // Handle new connections
                while (true) {
                    int client_fd = accept4(server_fd, nullptr, nullptr, SOCK_NONBLOCK);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // No more new connections
                            break;
                        }
                        std::cerr << "Accept failed" << std::endl;
                        break;
                    }

                    // Add new client to epoll
                    struct epoll_event client_ev;
                    client_ev.events = EPOLLIN | EPOLLET;
                    client_ev.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
                        std::cerr << "Failed to add client to epoll" << std::endl;
                        close(client_fd);
                        continue;
                    }
                }
            } else {
                // Handle client requests
                try {
                    std::string data = read_from_socket(events[n].data.fd);
                    if (!data.empty()) {
                        HttpRequest req;
                        if (0 == process_buffer(data.c_str(), &req)) {
                            process_request(&req, events[n].data.fd);
                        }
                    }
                } catch (const std::exception& e) {
                    // Handle errors or closed connections
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[n].data.fd, nullptr);
                    close(events[n].data.fd);
                    continue;
                }
                
                // Close the connection after processing
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[n].data.fd, nullptr);
                close(events[n].data.fd);
            }
        }
    }

    close(epoll_fd);
    return 0;
}
