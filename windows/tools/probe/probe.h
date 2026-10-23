#pragma once
// ixpat-probe: the hardware instrument for the Windows port. Console tool, Win32 only, no UI.
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace probe {

constexpr std::uint16_t kVendorId = 0x045E;
constexpr std::uint16_t kProductId = 0x0880;

struct Collection {
    std::wstring path;
    std::uint16_t vid = 0, pid = 0, version = 0;
    std::uint16_t usagePage = 0, usage = 0;
    std::uint16_t inputLen = 0, outputLen = 0, featureLen = 0;
    bool openedReadWrite = false;  // CreateFile(GENERIC_READ|GENERIC_WRITE) succeeded
    DWORD openError = 0;           // when it did not (the collection was still queried with access 0)
};

/// Every HID interface of the Surface Mouse (VID 045E, PID 0880) present right now.
std::vector<Collection> enumerateSurfaceMouse();

/// Open a collection for I/O. Returns INVALID_HANDLE_VALUE and sets `error` on failure.
HANDLE openCollection(const std::wstring& path, bool readWrite, DWORD& error);

/// Report ID of the feature report that carries `usagePage`/`usage` (from the value caps), or 0.
std::uint8_t featureReportIdFor(HANDLE h, std::uint16_t usagePage, std::uint16_t usage);

std::string hex(const std::uint8_t* p, std::size_t n);
std::string narrow(const std::wstring& w);
bool isSurfaceMousePath(const std::wstring& path);

int cmdList();
int cmdTable();
int cmdPlay(int ordinal, int intensity, bool useWriteFile);
int cmdVersions();
int cmdLog(int seconds, bool inject, const std::string& outFile);

}  // namespace probe
