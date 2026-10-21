#include "eee/net/TcpTransport.hpp"

#include "eee/core/Log.hpp"
#include "eee/net/Protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace eee::net {
namespace {

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void configureSocket(int fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
#if defined(TCP_KEEPALIVE)
    int keepAlive = 30; // macOS: idle seconds before probes
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &keepAlive, sizeof(keepAlive));
#elif defined(TCP_KEEPIDLE)
    int keepAlive = 30;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepAlive, sizeof(keepAlive));
#endif
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
}

bool writeAll(int fd, const core::u8* data, core::usize size) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    core::usize sent = 0;
    while (sent < size) {
        pollfd pfd{fd, POLLOUT, 0};
        const int waitMs = 50;
        const int ready = poll(&pfd, 1, waitMs);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (ready == 0) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            continue;
        }
        const ssize_t result = ::send(fd, data + sent, size - sent, 0);
        if (result > 0) {
            sent += static_cast<core::usize>(result);
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

TcpTransport::TcpTransport() = default;
TcpTransport::~TcpTransport() { closeAll(); }

bool TcpTransport::listen(core::u16 port) { return listenOn("0.0.0.0", port); }

bool TcpTransport::listenOn(std::string_view bindAddress, core::u16 port) {
    closeAll();
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        core::logError("tcp: socket() failed");
        return false;
    }
    int yes = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const std::string bindString(bindAddress);
    if (::inet_pton(AF_INET, bindString.c_str(), &address.sin_addr) != 1) {
        core::logf(core::LogLevel::Error, "tcp: invalid bind address '%s'",
                   bindString.c_str());
        closeAll();
        return false;
    }
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        core::logf(core::LogLevel::Error, "tcp: bind failed: %s", std::strerror(errno));
        closeAll();
        return false;
    }
    if (::listen(listenFd_, 4) != 0) {
        core::logf(core::LogLevel::Error, "tcp: listen failed: %s", std::strerror(errno));
        closeAll();
        return false;
    }
    setNonBlocking(listenFd_);

    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    if (getsockname(listenFd_, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
        port_ = ntohs(bound.sin_port);
    } else {
        port_ = port;
    }
    return true;
}

core::u16 TcpTransport::localPort() const { return port_; }

std::optional<ConnectionId> TcpTransport::accept() {
    if (listenFd_ < 0) return std::nullopt;
    while (true) {
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        const int fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&peer), &length);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return std::nullopt;
            if (errno == EINTR) continue;
            return std::nullopt;
        }
        configureSocket(fd);
        setNonBlocking(fd);
        return addConnection(fd);
    }
}

std::optional<ConnectionId> TcpTransport::connect(std::string_view host, core::u16 port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const std::string hostString(host);
    const std::string portString = std::to_string(port);
    if (getaddrinfo(hostString.c_str(), portString.c_str(), &hints, &results) != 0) {
        core::logError("tcp: getaddrinfo failed");
        return std::nullopt;
    }

    int fd = -1;
    for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
        fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(results);
    if (fd < 0) {
        core::logError("tcp: connect failed");
        return std::nullopt;
    }
    configureSocket(fd);
    setNonBlocking(fd);
    return addConnection(fd);
}

bool TcpTransport::send(ConnectionId connection, std::span<const core::u8> payload) {
    Connection* conn = find(connection);
    if (conn == nullptr || conn->fd < 0) return false;
    if (payload.size() > kMaxFramePayload) return false;

    std::vector<core::u8> frame;
    frame.reserve(4 + payload.size());
    const auto size = static_cast<core::u32>(payload.size());
    for (int i = 0; i < 4; ++i) frame.push_back(static_cast<core::u8>((size >> (8 * i)) & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return writeAll(conn->fd, frame.data(), frame.size());
}

std::optional<std::vector<core::u8>> TcpTransport::receive(ConnectionId connection) {
    Connection* conn = find(connection);
    if (conn == nullptr || conn->fd < 0) return std::nullopt;

    core::u8 chunk[4096];
    while (true) {
        const ssize_t count = ::recv(conn->fd, chunk, sizeof(chunk), 0);
        if (count > 0) {
            conn->buffer.insert(conn->buffer.end(), chunk, chunk + count);
            continue;
        }
        if (count == 0) {
            close(connection);
            return std::nullopt;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        close(connection);
        return std::nullopt;
    }

    if (conn->buffer.size() < 4) return std::nullopt;
    core::u32 size = 0;
    for (int i = 0; i < 4; ++i) size |= static_cast<core::u32>(conn->buffer[i]) << (8 * i);
    if (size > kMaxFramePayload) {
        core::logError("tcp: oversized frame received");
        close(connection);
        return std::nullopt;
    }
    if (conn->buffer.size() < 4 + size) return std::nullopt;

    std::vector<core::u8> payload(conn->buffer.begin() + 4,
                                  conn->buffer.begin() + 4 + static_cast<std::ptrdiff_t>(size));
    conn->buffer.erase(conn->buffer.begin(),
                       conn->buffer.begin() + 4 + static_cast<std::ptrdiff_t>(size));
    return payload;
}

void TcpTransport::close(ConnectionId connection) {
    Connection* conn = find(connection);
    if (conn == nullptr) return;
    if (conn->fd >= 0) {
        ::close(conn->fd);
        conn->fd = -1;
    }
    conn->buffer.clear();
}

void TcpTransport::closeAll() {
    for (Connection& connection : connections_) {
        if (connection.fd >= 0) {
            ::close(connection.fd);
            connection.fd = -1;
        }
        connection.buffer.clear();
    }
    connections_.clear();
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    port_ = 0;
}

bool TcpTransport::isConnected(ConnectionId connection) const {
    const Connection* conn = find(connection);
    return conn != nullptr && conn->fd >= 0;
}

TcpTransport::Connection* TcpTransport::find(ConnectionId connection) {
    if (connection == 0 || connection > connections_.size()) return nullptr;
    return &connections_[connection - 1];
}

const TcpTransport::Connection* TcpTransport::find(ConnectionId connection) const {
    if (connection == 0 || connection > connections_.size()) return nullptr;
    return &connections_[connection - 1];
}

ConnectionId TcpTransport::addConnection(int fd) {
    connections_.push_back(Connection{fd, {}});
    return static_cast<ConnectionId>(connections_.size());
}

} // namespace eee::net
