#include "core/zip_writer.h"

#include <array>

namespace ixpat {
namespace {

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> t = [] {
        std::array<std::uint32_t, 256> a{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            a[i] = c;
        }
        return a;
    }();
    return t;
}

void put16(std::vector<unsigned char>& v, std::uint16_t x) {
    v.push_back(static_cast<unsigned char>(x & 0xFF));
    v.push_back(static_cast<unsigned char>(x >> 8));
}

void put32(std::vector<unsigned char>& v, std::uint32_t x) {
    put16(v, static_cast<std::uint16_t>(x & 0xFFFF));
    put16(v, static_cast<std::uint16_t>(x >> 16));
}

constexpr std::uint16_t kVersion = 20;    // 2.0: stored entries, no features beyond that
constexpr std::uint16_t kFlagUtf8 = 0x0800;

}  // namespace

std::uint32_t crc32(const void* data, std::size_t n, std::uint32_t crc) {
    const auto* p = static_cast<const unsigned char*>(data);
    const auto& t = crcTable();
    std::uint32_t c = crc ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void ZipWriter::add(const std::string& name, const void* data, std::size_t n, std::uint16_t dosDate, std::uint16_t dosTime) {
    Entry e{name, crc32(data, n), static_cast<std::uint32_t>(n), static_cast<std::uint32_t>(body_.size()), dosDate, dosTime};
    put32(body_, 0x04034B50u);  // local file header
    put16(body_, kVersion);
    put16(body_, kFlagUtf8);
    put16(body_, 0);  // stored
    put16(body_, e.time);
    put16(body_, e.date);
    put32(body_, e.crc);
    put32(body_, e.size);
    put32(body_, e.size);
    put16(body_, static_cast<std::uint16_t>(name.size()));
    put16(body_, 0);
    body_.insert(body_.end(), name.begin(), name.end());
    const auto* p = static_cast<const unsigned char*>(data);
    body_.insert(body_.end(), p, p + n);
    entries_.push_back(std::move(e));
}

std::vector<unsigned char> ZipWriter::finish() const {
    std::vector<unsigned char> out = body_;
    const auto cdStart = static_cast<std::uint32_t>(out.size());
    for (const Entry& e : entries_) {
        put32(out, 0x02014B50u);  // central directory header
        put16(out, kVersion);     // made by
        put16(out, kVersion);     // needed
        put16(out, kFlagUtf8);
        put16(out, 0);
        put16(out, e.time);
        put16(out, e.date);
        put32(out, e.crc);
        put32(out, e.size);
        put32(out, e.size);
        put16(out, static_cast<std::uint16_t>(e.name.size()));
        put16(out, 0);  // extra
        put16(out, 0);  // comment
        put16(out, 0);  // disk
        put16(out, 0);  // internal attributes
        put32(out, 0);  // external attributes
        put32(out, e.offset);
        out.insert(out.end(), e.name.begin(), e.name.end());
    }
    const auto cdSize = static_cast<std::uint32_t>(out.size()) - cdStart;
    put32(out, 0x06054B50u);  // end of central directory
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<std::uint16_t>(entries_.size()));
    put16(out, static_cast<std::uint16_t>(entries_.size()));
    put32(out, cdSize);
    put32(out, cdStart);
    put16(out, 0);
    return out;
}

}  // namespace ixpat
