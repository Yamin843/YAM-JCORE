// util/base64.h — base64 encode/decode with no size limits.
#ifndef YAMGG_UTIL_BASE64_H
#define YAMGG_UTIL_BASE64_H

#include <cstdint>
#include <string>
#include <vector>

namespace yamgg {
namespace util {

// Encode `data[0..len)` to base64. Always produces the padded form.
std::string base64_encode(const uint8_t* data, size_t len);
inline std::string base64_encode(const std::vector<uint8_t>& data) {
    return base64_encode(data.data(), data.size());
}

// Decode base64 to bytes. Accepts padded and unpadded input. Accepts
// both standard and URL-safe alphabets ('+'/'/' vs '-'/'_'). Returns
// true on success; returns false and sets `error` on invalid input.
bool base64_decode(const std::string& in,
                   std::vector<uint8_t>& out,
                   std::string& error);

} // namespace util
} // namespace yamgg

#endif // YAMGG_UTIL_BASE64_H
