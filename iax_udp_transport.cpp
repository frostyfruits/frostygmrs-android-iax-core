#include "iax_udp_transport.h"

#include <android/log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define FROSTY_UDP_LOG_TAG "FrostyIaxUdp"
#define FROSTY_UDP_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, FROSTY_UDP_LOG_TAG, __VA_ARGS__)
#define FROSTY_UDP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, FROSTY_UDP_LOG_TAG, __VA_ARGS__)

namespace frosty {

    IaxUdpTransport::IaxUdpTransport() : socketFd_(-1) {
    }

    IaxUdpTransport::~IaxUdpTransport() {
        close();
    }

    bool IaxUdpTransport::open(const std::string& host, int port) {
        close();

        struct addrinfo hints;
        struct addrinfo* result = nullptr;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        const std::string portStr = std::to_string(port);

        const int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
        if (rc != 0 || !result) {
            FROSTY_UDP_LOGE("getaddrinfo failed rc=%d", rc);
            return false;
        }

        bool opened = false;

        for (struct addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
            socketFd_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (socketFd_ < 0) {
                continue;
            }

            if (connect(socketFd_, ai->ai_addr, ai->ai_addrlen) == 0) {
                opened = true;
                break;
            }

            ::close(socketFd_);
            socketFd_ = -1;
        }

        freeaddrinfo(result);

        if (!opened) {
            FROSTY_UDP_LOGE("UDP connect failed errno=%d", errno);
            return false;
        }

        FROSTY_UDP_LOGD("UDP open ok fd=%d", socketFd_);
        return true;
    }

    void IaxUdpTransport::close() {
        if (socketFd_ >= 0) {
            FROSTY_UDP_LOGD("UDP close fd=%d", socketFd_);
            ::close(socketFd_);
            socketFd_ = -1;
        }
    }

    bool IaxUdpTransport::isOpen() const {
        return socketFd_ >= 0;
    }

    bool IaxUdpTransport::sendPacket(const uint8_t* data, size_t size) {
        if (socketFd_ < 0 || !data || size == 0) {
            return false;
        }

        const ssize_t sent = send(socketFd_, data, size, 0);
        if (sent < 0 || static_cast<size_t>(sent) != size) {
            FROSTY_UDP_LOGE("UDP send failed errno=%d", errno);
            return false;
        }

        FROSTY_UDP_LOGD("UDP sent %zu bytes", size);
        return true;
    }

    int IaxUdpTransport::receivePacket(uint8_t* buffer, size_t bufferSize, int timeoutMs) {
        if (socketFd_ < 0 || !buffer || bufferSize == 0) {
            return -1;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socketFd_, &readfds);

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        const int ready = select(socketFd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            FROSTY_UDP_LOGE("UDP select failed errno=%d", errno);
            return -1;
        }

        if (ready == 0) {
            return 0;
        }

        const ssize_t received = recv(socketFd_, buffer, bufferSize, 0);
        if (received < 0) {
            FROSTY_UDP_LOGE("UDP recv failed errno=%d", errno);
            return -1;
        }

        FROSTY_UDP_LOGD("UDP received %zd bytes", received);
        return static_cast<int>(received);
    }

}  // namespace frosty