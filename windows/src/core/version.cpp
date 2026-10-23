#include "core/version.h"

#include <algorithm>
#include <charconv>
#include <vector>

namespace ixpat {
namespace {

std::vector<long> parts(std::string_view v) {
    std::vector<long> out;
    std::size_t start = 0;
    while (start <= v.size()) {
        auto dot = v.find('.', start);
        if (dot == std::string_view::npos) dot = v.size();
        std::string_view piece = v.substr(start, dot - start);
        long n = 0;
        const auto res = std::from_chars(piece.data(), piece.data() + piece.size(), n);
        if (res.ec != std::errc{} || res.ptr != piece.data() + piece.size()) n = 0;  // "2oops" is not a number
        out.push_back(n);
        start = dot + 1;
    }
    return out;
}

}  // namespace

bool isNewerVersion(std::string_view candidate, std::string_view current) {
    const auto n = parts(candidate), c = parts(current);
    const std::size_t count = std::max(n.size(), c.size());
    for (std::size_t i = 0; i < count; ++i) {
        const long a = i < n.size() ? n[i] : 0, b = i < c.size() ? c[i] : 0;
        if (a != b) return a > b;
    }
    return false;
}

std::string stripTagPrefix(std::string_view tag) {
    if (tag.starts_with("win-v")) tag.remove_prefix(5);
    else if (tag.starts_with("v")) tag.remove_prefix(1);
    return std::string(tag);
}

}  // namespace ixpat
