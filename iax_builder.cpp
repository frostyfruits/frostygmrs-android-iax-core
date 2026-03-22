#include "iax_builder.h"

#include <string.h>

namespace frosty {

    static void appendU8(std::vector<uint8_t>& out, uint8_t v) {
        out.push_back(v);
    }

    static void appendU16(std::vector<uint8_t>& out, uint16_t v) {
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        out.push_back(static_cast<uint8_t>(v & 0xff));
    }

    static void appendU32(std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        out.push_back(static_cast<uint8_t>(v & 0xff));
    }

    static void appendIeString(std::vector<uint8_t>& out, uint8_t ieType, const char* value) {
        if (!value || !value[0]) {
            return;
        }

        const size_t len = strlen(value);
        if (len > 255) {
            return;
        }

        appendU8(out, ieType);
        appendU8(out, static_cast<uint8_t>(len));
        out.insert(out.end(), value, value + len);
    }

    static void appendIeStdString(std::vector<uint8_t>& out, uint8_t ieType, const std::string& value) {
        if (value.empty() || value.size() > 255) {
            return;
        }

        appendU8(out, ieType);
        appendU8(out, static_cast<uint8_t>(value.size()));
        out.insert(out.end(), value.begin(), value.end());
    }

    static void appendIeU16(std::vector<uint8_t>& out, uint8_t ieType, uint16_t value) {
        appendU8(out, ieType);
        appendU8(out, 2);
        appendU16(out, value);
    }

    static void appendIeU32(std::vector<uint8_t>& out, uint8_t ieType, uint32_t value) {
        appendU8(out, ieType);
        appendU8(out, 4);
        appendU32(out, value);
    }

    static void appendIeBytes(std::vector<uint8_t>& out, uint8_t ieType, const uint8_t* value, size_t len) {
        if (len > 255) {
            return;
        }

        appendU8(out, ieType);
        appendU8(out, static_cast<uint8_t>(len));

        if (value && len > 0) {
            out.insert(out.end(), value, value + len);
        }
    }

    std::vector<uint8_t> IaxBuilder::buildInitialNew(
            uint16_t srcCallNo,
            uint32_t timestamp,
            const char* username,
            const char* calledNumber,
            const char* calledContext,
            const char* callingNumber,
            const char* callingName,
            const uint8_t* callToken,
            size_t callTokenLen) {
        std::vector<uint8_t> out;
        out.reserve(160);

        const uint16_t dstCallNo = 0;
        const uint8_t fullFrameType = 0x06;
        const uint8_t iaxSubclassNew = 0x01;

        const uint16_t iaxVersion = 2;
        const uint32_t capability = 0x00000004;
        const uint32_t format = 0x00000004;

        appendU16(out, static_cast<uint16_t>(srcCallNo | 0x8000));
        appendU16(out, dstCallNo);
        appendU32(out, timestamp);
        appendU8(out, 0);
        appendU8(out, 0);
        appendU8(out, fullFrameType);
        appendU8(out, iaxSubclassNew);

        appendIeU16(out, 0x0b, iaxVersion);
        appendIeBytes(out, 0x36, callToken, callTokenLen);
        appendIeString(out, 0x01, calledNumber);
        appendIeString(out, 0x05, calledContext);
        appendIeString(out, 0x02, callingNumber);
        appendIeString(out, 0x04, callingName);
        appendIeString(out, 0x06, username);
        appendIeU32(out, 0x08, capability);
        appendIeU32(out, 0x09, format);

        return out;
    }

    std::vector<uint8_t> IaxBuilder::buildAck(
            uint16_t srcCallNo,
            uint16_t dstCallNo,
            uint32_t timestamp,
            uint8_t oseq,
            uint8_t iseq) {
        std::vector<uint8_t> out;
        out.reserve(12);

        const uint8_t fullFrameType = 0x06;
        const uint8_t iaxSubclassAck = 0x04;

        appendU16(out, static_cast<uint16_t>(srcCallNo | 0x8000));
        appendU16(out, dstCallNo);
        appendU32(out, timestamp);
        appendU8(out, oseq);
        appendU8(out, iseq);
        appendU8(out, fullFrameType);
        appendU8(out, iaxSubclassAck);

        return out;
    }

    std::vector<uint8_t> IaxBuilder::buildAuthRepMd5(
            uint16_t srcCallNo,
            uint16_t dstCallNo,
            uint32_t timestamp,
            uint8_t oseq,
            uint8_t iseq,
            const char* username,
            const std::string& md5Hex) {
        std::vector<uint8_t> out;
        out.reserve(128);

        const uint8_t fullFrameType = 0x06;
        const uint8_t iaxSubclassAuthRep = 0x09;

        appendU16(out, static_cast<uint16_t>(srcCallNo | 0x8000));
        appendU16(out, dstCallNo);
        appendU32(out, timestamp);
        appendU8(out, oseq);
        appendU8(out, iseq);
        appendU8(out, fullFrameType);
        appendU8(out, iaxSubclassAuthRep);

        appendIeString(out, 0x06, username);
        appendIeStdString(out, 0x10, md5Hex);

        return out;
    }

    std::vector<uint8_t> IaxBuilder::buildLagRp(
            uint16_t srcCallNo,
            uint16_t dstCallNo,
            uint32_t timestamp,
            uint8_t oseq,
            uint8_t iseq) {
        std::vector<uint8_t> out;
        out.reserve(12);

        const uint8_t fullFrameType = 0x06;
        const uint8_t iaxSubclassLagRp = 0x0c;

        appendU16(out, static_cast<uint16_t>(srcCallNo | 0x8000));
        appendU16(out, dstCallNo);
        appendU32(out, timestamp);
        appendU8(out, oseq);
        appendU8(out, iseq);
        appendU8(out, fullFrameType);
        appendU8(out, iaxSubclassLagRp);

        return out;
    }

    std::vector<uint8_t> IaxBuilder::buildPong(
            uint16_t srcCallNo,
            uint16_t dstCallNo,
            uint32_t timestamp,
            uint8_t oseq,
            uint8_t iseq) {
        std::vector<uint8_t> out;
        out.reserve(12);

        const uint8_t fullFrameType = 0x06;
        const uint8_t iaxSubclassPong = 0x03;

        appendU16(out, static_cast<uint16_t>(srcCallNo | 0x8000));
        appendU16(out, dstCallNo);
        appendU32(out, timestamp);
        appendU8(out, oseq);
        appendU8(out, iseq);
        appendU8(out, fullFrameType);
        appendU8(out, iaxSubclassPong);

        return out;
    }

    std::vector<uint8_t> IaxBuilder::buildPing(
            uint16_t srcCallNo,
            uint16_t dstCallNo,
            uint32_t timestamp,
            uint8_t oseq,
            uint8_t iseq) {
        std::vector<uint8_t> out;
        out.reserve(12);

        const uint8_t fullFrameType = 0x06;
        const uint8_t iaxSubclassPing = 0x02;

        appendU16(out, static_cast<uint16_t>(srcCallNo | 0x8000));
        appendU16(out, dstCallNo);
        appendU32(out, timestamp);
        appendU8(out, oseq);
        appendU8(out, iseq);
        appendU8(out, fullFrameType);
        appendU8(out, iaxSubclassPing);

        return out;
    }

}  // namespace frosty