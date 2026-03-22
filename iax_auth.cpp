#include "iax_auth.h"

#include <string.h>

namespace frosty {

    namespace {

        class Md5 {
        public:
            Md5() {
                init();
            }

            void update(const uint8_t* input, size_t length) {
                size_t index = static_cast<size_t>((count_[0] >> 3) & 0x3f);

                if ((count_[0] += static_cast<uint32_t>(length << 3)) < (length << 3)) {
                    count_[1]++;
                }
                count_[1] += static_cast<uint32_t>(length >> 29);

                const size_t partLen = 64 - index;
                size_t i = 0;

                if (length >= partLen) {
                    memcpy(&buffer_[index], input, partLen);
                    transform(buffer_);

                    for (i = partLen; i + 63 < length; i += 64) {
                        transform(&input[i]);
                    }

                    index = 0;
                }

                memcpy(&buffer_[index], &input[i], length - i);
            }

            void finalize(uint8_t digest[16]) {
                static const uint8_t padding[64] = { 0x80 };

                uint8_t bits[8];
                encode(bits, count_, 8);

                const size_t index = static_cast<size_t>((count_[0] >> 3) & 0x3f);
                const size_t padLen = (index < 56) ? (56 - index) : (120 - index);

                update(padding, padLen);
                update(bits, 8);

                encode(digest, state_, 16);
            }

        private:
            void init() {
                count_[0] = 0;
                count_[1] = 0;

                state_[0] = 0x67452301;
                state_[1] = 0xefcdab89;
                state_[2] = 0x98badcfe;
                state_[3] = 0x10325476;
            }

            static uint32_t f(uint32_t x, uint32_t y, uint32_t z) {
                return (x & y) | (~x & z);
            }

            static uint32_t g(uint32_t x, uint32_t y, uint32_t z) {
                return (x & z) | (y & ~z);
            }

            static uint32_t h(uint32_t x, uint32_t y, uint32_t z) {
                return x ^ y ^ z;
            }

            static uint32_t i(uint32_t x, uint32_t y, uint32_t z) {
                return y ^ (x | ~z);
            }

            static uint32_t rotateLeft(uint32_t x, uint32_t n) {
                return (x << n) | (x >> (32 - n));
            }

            static void ff(uint32_t& a, uint32_t b, uint32_t c, uint32_t d,
                           uint32_t x, uint32_t s, uint32_t ac) {
                a += f(b, c, d) + x + ac;
                a = rotateLeft(a, s);
                a += b;
            }

            static void gg(uint32_t& a, uint32_t b, uint32_t c, uint32_t d,
                           uint32_t x, uint32_t s, uint32_t ac) {
                a += g(b, c, d) + x + ac;
                a = rotateLeft(a, s);
                a += b;
            }

            static void hh(uint32_t& a, uint32_t b, uint32_t c, uint32_t d,
                           uint32_t x, uint32_t s, uint32_t ac) {
                a += h(b, c, d) + x + ac;
                a = rotateLeft(a, s);
                a += b;
            }

            static void ii(uint32_t& a, uint32_t b, uint32_t c, uint32_t d,
                           uint32_t x, uint32_t s, uint32_t ac) {
                a += i(b, c, d) + x + ac;
                a = rotateLeft(a, s);
                a += b;
            }

            void transform(const uint8_t block[64]) {
                uint32_t a = state_[0];
                uint32_t b = state_[1];
                uint32_t c = state_[2];
                uint32_t d = state_[3];
                uint32_t x[16];

                decode(x, block, 64);

                ff(a, b, c, d, x[0], 7, 0xd76aa478);
                ff(d, a, b, c, x[1], 12, 0xe8c7b756);
                ff(c, d, a, b, x[2], 17, 0x242070db);
                ff(b, c, d, a, x[3], 22, 0xc1bdceee);
                ff(a, b, c, d, x[4], 7, 0xf57c0faf);
                ff(d, a, b, c, x[5], 12, 0x4787c62a);
                ff(c, d, a, b, x[6], 17, 0xa8304613);
                ff(b, c, d, a, x[7], 22, 0xfd469501);
                ff(a, b, c, d, x[8], 7, 0x698098d8);
                ff(d, a, b, c, x[9], 12, 0x8b44f7af);
                ff(c, d, a, b, x[10], 17, 0xffff5bb1);
                ff(b, c, d, a, x[11], 22, 0x895cd7be);
                ff(a, b, c, d, x[12], 7, 0x6b901122);
                ff(d, a, b, c, x[13], 12, 0xfd987193);
                ff(c, d, a, b, x[14], 17, 0xa679438e);
                ff(b, c, d, a, x[15], 22, 0x49b40821);

                gg(a, b, c, d, x[1], 5, 0xf61e2562);
                gg(d, a, b, c, x[6], 9, 0xc040b340);
                gg(c, d, a, b, x[11], 14, 0x265e5a51);
                gg(b, c, d, a, x[0], 20, 0xe9b6c7aa);
                gg(a, b, c, d, x[5], 5, 0xd62f105d);
                gg(d, a, b, c, x[10], 9, 0x02441453);
                gg(c, d, a, b, x[15], 14, 0xd8a1e681);
                gg(b, c, d, a, x[4], 20, 0xe7d3fbc8);
                gg(a, b, c, d, x[9], 5, 0x21e1cde6);
                gg(d, a, b, c, x[14], 9, 0xc33707d6);
                gg(c, d, a, b, x[3], 14, 0xf4d50d87);
                gg(b, c, d, a, x[8], 20, 0x455a14ed);
                gg(a, b, c, d, x[13], 5, 0xa9e3e905);
                gg(d, a, b, c, x[2], 9, 0xfcefa3f8);
                gg(c, d, a, b, x[7], 14, 0x676f02d9);
                gg(b, c, d, a, x[12], 20, 0x8d2a4c8a);

                hh(a, b, c, d, x[5], 4, 0xfffa3942);
                hh(d, a, b, c, x[8], 11, 0x8771f681);
                hh(c, d, a, b, x[11], 16, 0x6d9d6122);
                hh(b, c, d, a, x[14], 23, 0xfde5380c);
                hh(a, b, c, d, x[1], 4, 0xa4beea44);
                hh(d, a, b, c, x[4], 11, 0x4bdecfa9);
                hh(c, d, a, b, x[7], 16, 0xf6bb4b60);
                hh(b, c, d, a, x[10], 23, 0xbebfbc70);
                hh(a, b, c, d, x[13], 4, 0x289b7ec6);
                hh(d, a, b, c, x[0], 11, 0xeaa127fa);
                hh(c, d, a, b, x[3], 16, 0xd4ef3085);
                hh(b, c, d, a, x[6], 23, 0x04881d05);
                hh(a, b, c, d, x[9], 4, 0xd9d4d039);
                hh(d, a, b, c, x[12], 11, 0xe6db99e5);
                hh(c, d, a, b, x[15], 16, 0x1fa27cf8);
                hh(b, c, d, a, x[2], 23, 0xc4ac5665);

                ii(a, b, c, d, x[0], 6, 0xf4292244);
                ii(d, a, b, c, x[7], 10, 0x432aff97);
                ii(c, d, a, b, x[14], 15, 0xab9423a7);
                ii(b, c, d, a, x[5], 21, 0xfc93a039);
                ii(a, b, c, d, x[12], 6, 0x655b59c3);
                ii(d, a, b, c, x[3], 10, 0x8f0ccc92);
                ii(c, d, a, b, x[10], 15, 0xffeff47d);
                ii(b, c, d, a, x[1], 21, 0x85845dd1);
                ii(a, b, c, d, x[8], 6, 0x6fa87e4f);
                ii(d, a, b, c, x[15], 10, 0xfe2ce6e0);
                ii(c, d, a, b, x[6], 15, 0xa3014314);
                ii(b, c, d, a, x[13], 21, 0x4e0811a1);
                ii(a, b, c, d, x[4], 6, 0xf7537e82);
                ii(d, a, b, c, x[11], 10, 0xbd3af235);
                ii(c, d, a, b, x[2], 15, 0x2ad7d2bb);
                ii(b, c, d, a, x[9], 21, 0xeb86d391);

                state_[0] += a;
                state_[1] += b;
                state_[2] += c;
                state_[3] += d;
            }

            static void encode(uint8_t* output, const uint32_t* input, size_t len) {
                for (size_t i = 0, j = 0; j < len; ++i, j += 4) {
                    output[j] = static_cast<uint8_t>(input[i] & 0xff);
                    output[j + 1] = static_cast<uint8_t>((input[i] >> 8) & 0xff);
                    output[j + 2] = static_cast<uint8_t>((input[i] >> 16) & 0xff);
                    output[j + 3] = static_cast<uint8_t>((input[i] >> 24) & 0xff);
                }
            }

            static void decode(uint32_t* output, const uint8_t* input, size_t len) {
                for (size_t i = 0, j = 0; j < len; ++i, j += 4) {
                    output[i] = static_cast<uint32_t>(input[j]) |
                                (static_cast<uint32_t>(input[j + 1]) << 8) |
                                (static_cast<uint32_t>(input[j + 2]) << 16) |
                                (static_cast<uint32_t>(input[j + 3]) << 24);
                }
            }

        private:
            uint32_t state_[4];
            uint32_t count_[2];
            uint8_t buffer_[64];
        };

        static std::string toHex(const uint8_t* data, size_t len) {
            static const char* kHex = "0123456789abcdef";
            std::string out;
            out.reserve(len * 2);

            for (size_t i = 0; i < len; ++i) {
                const uint8_t b = data[i];
                out.push_back(kHex[(b >> 4) & 0x0f]);
                out.push_back(kHex[b & 0x0f]);
            }

            return out;
        }

    }  // namespace

    std::string IaxAuth::computeMd5Hex(const std::string& input) {
        Md5 md5;
        md5.update(reinterpret_cast<const uint8_t*>(input.data()), input.size());

        uint8_t digest[16];
        md5.finalize(digest);

        return toHex(digest, sizeof(digest));
    }

    std::string IaxAuth::computeChallengeResponseMd5(
            const std::string& challenge,
            const std::string& password) {
        return computeMd5Hex(challenge + password);
    }

}  // namespace frosty