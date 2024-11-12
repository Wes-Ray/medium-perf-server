#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

#include "config.h"

// Make socket non-blocking
void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
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

    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return -1;
    }

    // Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "Epoll creation failed" << std::endl;
        return -1;
    }

    // Make server socket non-blocking
    set_nonblocking(server_fd);

    // Add server socket to epoll
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        std::cerr << "Failed to add server socket to epoll" << std::endl;
        return -1;
    }

    std::cout << "Server listening on port " << SERVER_PORT << std::endl;

    // Event loop
    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
    char buffer[1024];

    while (true) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        
        for (int i = 0; i < num_events; i++) {
            if (events[i].data.fd == server_fd) {
                // Handle new connection
                while (true) {  // Accept all pending connections
                    int client_fd = accept(server_fd, nullptr, nullptr);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // No more connections to accept
                            break;
                        }
                        std::cerr << "Accept failed" << std::endl;
                        break;
                    }

                    // Make client socket non-blocking
                    set_nonblocking(client_fd);

                    // Add client to epoll
                    event.events = EPOLLIN | EPOLLET;  // Edge-triggered mode
                    event.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                        std::cerr << "Failed to add client to epoll" << std::endl;
                        close(client_fd);
                        continue;
                    }
                    
                    std::cout << "New client connected: " << client_fd << std::endl;
                }
            } else {
                // Handle client data
                int client_fd = events[i].data.fd;
                memset(buffer, 0, sizeof(buffer));
                
                int valread = read(client_fd, buffer, sizeof(buffer));
                if (valread <= 0) {
                    // Connection closed or error
                    std::cout << "Client disconnected: " << client_fd << std::endl;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                    close(client_fd);
                } else {
                    std::cout << "Received from client " << client_fd << ": " << buffer;
                    send(client_fd, buffer, valread, 0);
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;
}