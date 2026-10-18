#include "server/server.h"
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <iostream>
#include <csignal>

namespace minired {

// ─────────────────────────────────────────────────────────────────────────────
static Server* g_server = nullptr;   // for signal handler

Server::Server(const Config& cfg, storage::DB& db)
    : cfg_(cfg), db_(db)
{
    registry_.register_all();
    g_server = this;

    // Install SIGINT/SIGTERM handler for graceful shutdown
    struct sigaction sa{};
    sa.sa_handler = [](int) { if (g_server) g_server->stop(); };
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

Server::~Server() {
    if (server_fd_ >= 0) ::close(server_fd_);
    if (epoll_fd_  >= 0) ::close(epoll_fd_);
}

// ─────────────────────────────────────────────────────────────────────────────
void Server::set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl F_GETFL failed");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl F_SETFL O_NONBLOCK failed");
}

void Server::add_epoll(int epfd, int fd, uint32_t events) {
    epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
        throw std::runtime_error(std::string("epoll_ctl ADD: ") + strerror(errno));
}

void Server::mod_epoll(int epfd, int fd, uint32_t events) {
    epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;
    ::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

void Server::del_epoll(int epfd, int fd) {
    ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
void Server::setup_socket() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(cfg_.port);
    addr.sin_addr.s_addr = ::inet_addr(cfg_.bind_addr.c_str());

    if (::bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));

    if (::listen(server_fd_, cfg_.backlog) < 0)
        throw std::runtime_error("listen() failed");

    set_nonblocking(server_fd_);
}

// ─────────────────────────────────────────────────────────────────────────────
void Server::run() {
    setup_socket();

    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ < 0) throw std::runtime_error("epoll_create1() failed");

    add_epoll(epoll_fd_, server_fd_, EPOLLIN);

    running_ = true;
    std::cout << "MiniRed listening on " << cfg_.bind_addr
              << ":" << cfg_.port << "\n";
    event_loop();
}

void Server::stop() {
    running_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kMaxEvents = 256;

void Server::event_loop() {
    epoll_event events[kMaxEvents];

    while (running_) {
        int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, 100 /*ms timeout*/);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == server_fd_) {
                accept_client();
            } else {
                if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                    handle_client(fd);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void Server::accept_client() {
    while (true) {
        sockaddr_in peer{};
        socklen_t   len = sizeof(peer);
        int cfd = ::accept4(server_fd_, (sockaddr*)&peer, &len, SOCK_NONBLOCK);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "accept4 error: " << strerror(errno) << "\n";
            break;
        }
        // Disable Nagle for low-latency responses
        int flag = 1;
        ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        clients_.emplace(cfd, Client{});
        add_epoll(epoll_fd_, cfd, EPOLLIN | EPOLLET);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void Server::handle_client(int fd) {
    char buf[4096];
    auto& client = clients_[fd];

    // Read as much as possible (ET mode)
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            if (client.parser.feed(buf, static_cast<size_t>(n))) {
                while (client.parser.has_command()) {
                    auto cmd  = client.parser.pop_command();
                    auto resp = registry_.dispatch(cmd, db_);
                    client.write_buf += resp;
                }
            }
        } else if (n == 0) {
            // Client closed connection
            del_epoll(epoll_fd_, fd);
            ::close(fd);
            clients_.erase(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            // Real error
            del_epoll(epoll_fd_, fd);
            ::close(fd);
            clients_.erase(fd);
            return;
        }
    }

    // Flush write buffer
    if (!client.write_buf.empty()) {
        ssize_t sent = ::write(fd, client.write_buf.data(), client.write_buf.size());
        if (sent > 0) {
            client.write_buf.erase(0, static_cast<size_t>(sent));
        }
        if (!client.write_buf.empty()) {
            // Register for EPOLLOUT to finish writing
            mod_epoll(epoll_fd_, fd, EPOLLIN | EPOLLOUT | EPOLLET);
        }
    }
}

} // namespace minired
