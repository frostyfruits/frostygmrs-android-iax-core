#pragma once

#include <stdint.h>
#include <string>

namespace frosty {

    class IaxAuth {
    public:
        static std::string computeMd5Hex(const std::string& input);
        static std::string computeChallengeResponseMd5(
                const std::string& challenge,
                const std::string& password);
    };

}  // namespace frosty