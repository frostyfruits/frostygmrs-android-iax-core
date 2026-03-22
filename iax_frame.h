#pragma once

#include <stdint.h>
#include <stddef.h>

namespace frosty {

    static constexpr int kIaxDefaultPort = 4569;
    static constexpr size_t kIaxMaxPacketSize = 1500;

    enum class SessionState : int {
        Idle = 0,
        SocketReady = 1,
        Connecting = 2,
        Connected = 3,
        Disconnecting = 4,
        Disconnected = 5,
        Error = 6
    };

    struct ConnectParams {
        const char* username;
        const char* password;
        const char* host;
        int port;
        const char* remoteNode;
    };

    struct ReceivedPacket {
        uint8_t data[kIaxMaxPacketSize];
        size_t size;
    };

}  // namespace frosty