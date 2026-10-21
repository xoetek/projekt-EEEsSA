#pragma once

#include "eee/net/Transport.hpp"

namespace eee::net {

// POSIX TCP transport (macOS/Linux). Frames are u32 length-prefixed,
// little-endian, non-blocking receive, bounded send timeout.
class TcpTransport final : public ITransport {
public:
    TcpTransport();
    ~TcpTransport() override;

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    bool listen(core::u16 port) override;
    [[nodiscard]] core::u16 localPort() const override;

    [[nodiscard]] std::optional<ConnectionId> accept() override;
    [[nodiscard]] std::optional<ConnectionId> connect(std::string_view host,
                                                      core::u16 port) override;

    bool send(ConnectionId connection, std::span<const core::u8> payload) override;
    [[nodiscard]] std::optional<std::vector<core::u8>> receive(
        ConnectionId connection) override;

    void close(ConnectionId connection) override;
    void closeAll() override;
    [[nodiscard]] bool isConnected(ConnectionId connection) const override;

private:
    struct Connection {
        int fd = -1;
        std::vector<core::u8> buffer;
    };

    [[nodiscard]] Connection* find(ConnectionId connection);
    [[nodiscard]] const Connection* find(ConnectionId connection) const;
    [[nodiscard]] ConnectionId addConnection(int fd);

    int listenFd_ = -1;
    core::u16 port_ = 0;
    std::vector<Connection> connections_;
};

} // namespace eee::net
