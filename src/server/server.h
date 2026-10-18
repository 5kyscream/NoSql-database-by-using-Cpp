#pragma once
#include "server/config.h"
#include "storage/db.h"
#include "commands/command_registry.h"
#include <string>

namespace minired {

class Server {
public:
    explicit Server(const Config& cfg, storage::DB& db);
    ~Server();

    // Block and run the event loop until stop() is called.
    void run();
    void stop();

private:
    const Config&           cfg_;
    storage::DB&            db_;
    commands::CommandRegistry registry_;

    int  server_fd_  = -1;
    int  epoll_fd_   = -1;
    bool running_    = false;

    void setup_socket();
    void event_loop();
    void accept_client();
    void handle_client(int fd);

    // Per-client read/write state
    struct Client {
        std::string  read_buf;
        std::string  write_buf;
        protocol::RespParser parser;
    };

    // fd → Client state
    std::unordered_map<int, Client> clients_;

    static void set_nonblocking(int fd);
    static void add_epoll(int epfd, int fd, uint32_t events);
    static void mod_epoll(int epfd, int fd, uint32_t events);
    static void del_epoll(int epfd, int fd);
};

} // namespace minired
