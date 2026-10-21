#pragma once

#include "eee/core/Types.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace eee::net {

using ConnectionId = core::u32;

// Transport carries opaque length-prefixed frames. Protocol encoding/decoding
// lives in Protocol.hpp so transports stay replaceable (TCP now, UDP later).
class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool listen(core::u16 port) = 0;
    [[nodiscard]] virtual core::u16 localPort() const = 0;

    [[nodiscard]] virtual std::optional<ConnectionId> accept() = 0;
    [[nodiscard]] virtual std::optional<ConnectionId> connect(std::string_view host,
                                                              core::u16 port) = 0;

    virtual bool send(ConnectionId connection, std::span<const core::u8> payload) = 0;
    [[nodiscard]] virtual std::optional<std::vector<core::u8>> receive(
        ConnectionId connection) = 0;

    virtual void close(ConnectionId connection) = 0;
    virtual void closeAll() = 0;

    [[nodiscard]] virtual bool isConnected(ConnectionId connection) const = 0;
};

} // namespace eee::net
