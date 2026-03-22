#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace frosty {

    class IaxUdpTransport {
    public:
        IaxUdpTransport();
        ~IaxUdpTransport();

        bool open(const std::string& host, int port);
        void close();

        bool isOpen() const;

        bool sendPacket(const uint8_t* data, size_t size);
        int receivePacket(uint8_t* buffer, size_t bufferSize, int timeoutMs);

    private:
        int socketFd_;
    };

}  // namespace frosty