#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace frosty {

    class IaxBuilder {
    public:
        static std::vector<uint8_t> buildInitialNew(
                uint16_t srcCallNo,
                uint32_t timestamp,
                const char* username,
                const char* calledNumber,
                const char* calledContext,
                const char* callingNumber,
                const char* callingName,
                const uint8_t* callToken,
                size_t callTokenLen);

        static std::vector<uint8_t> buildAck(
                uint16_t srcCallNo,
                uint16_t dstCallNo,
                uint32_t timestamp,
                uint8_t oseq,
                uint8_t iseq);

        static std::vector<uint8_t> buildAuthRepMd5(
                uint16_t srcCallNo,
                uint16_t dstCallNo,
                uint32_t timestamp,
                uint8_t oseq,
                uint8_t iseq,
                const char* username,
                const std::string& md5Hex);

        static std::vector<uint8_t> buildLagRp(
                uint16_t srcCallNo,
                uint16_t dstCallNo,
                uint32_t timestamp,
                uint8_t oseq,
                uint8_t iseq);

        static std::vector<uint8_t> buildPong(
                uint16_t srcCallNo,
                uint16_t dstCallNo,
                uint32_t timestamp,
                uint8_t oseq,
                uint8_t iseq);

        static std::vector<uint8_t> buildPing(
                uint16_t srcCallNo,
                uint16_t dstCallNo,
                uint32_t timestamp,
                uint8_t oseq,
                uint8_t iseq);
    };

}  // namespace frosty