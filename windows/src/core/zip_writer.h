#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ixpat {

/// CRC-32 (IEEE 802.3, reflected, as ZIP and PNG use it). `crc` continues an earlier run.
std::uint32_t crc32(const void* data, std::size_t n, std::uint32_t crc = 0);

/// A ZIP archive built in memory from stored (uncompressed) entries: enough for a few logs and a crash dump, with no
/// library and nothing clever. Names are UTF-8 with forward slashes; every entry carries the stamp given to add().
/// GitHub accepts a .zip on an issue where it refuses a .dmp, which is the whole reason this exists.
class ZipWriter {
public:
    /// `dosDate`/`dosTime` are MS-DOS packed (ZIP's own format); 1980-01-01 00:00 when left at the defaults.
    void add(const std::string& name, const void* data, std::size_t n, std::uint16_t dosDate = 0x0021, std::uint16_t dosTime = 0);
    /// The whole archive: local headers and data in order, then the central directory and the end record.
    std::vector<unsigned char> finish() const;
    std::size_t entries() const { return entries_.size(); }
    std::size_t bytes() const { return body_.size(); }

private:
    struct Entry {
        std::string name;
        std::uint32_t crc, size, offset;
        std::uint16_t date, time;
    };
    std::vector<unsigned char> body_;
    std::vector<Entry> entries_;
};

}  // namespace ixpat
