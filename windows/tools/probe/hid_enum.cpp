#include "probe.h"

#include <cfgmgr32.h>
#include <hidsdi.h>
#include <initguid.h>
#include <hidclass.h>

#include <algorithm>
#include <cwctype>

namespace probe {

std::string hex(const std::uint8_t* p, std::size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

bool isSurfaceMousePath(const std::wstring& path) {
    std::wstring p = path;
    std::transform(p.begin(), p.end(), p.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return p.find(L"vid&02045e_pid&0880") != std::wstring::npos || p.find(L"vid_045e&pid_0880") != std::wstring::npos;
}

HANDLE openCollection(const std::wstring& path, bool readWrite, DWORD& error) {
    const DWORD access = readWrite ? (GENERIC_READ | GENERIC_WRITE) : 0;
    HANDLE h = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    return h;
}

std::uint8_t featureReportIdFor(HANDLE h, std::uint16_t usagePage, std::uint16_t usage) {
    PHIDP_PREPARSED_DATA pp = nullptr;
    if (!HidD_GetPreparsedData(h, &pp)) return 0;
    std::uint8_t id = 0;
    HIDP_CAPS caps{};
    if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
        USHORT n = caps.NumberFeatureValueCaps;
        if (n) {
            std::vector<HIDP_VALUE_CAPS> v(n);
            if (HidP_GetValueCaps(HidP_Feature, v.data(), &n, pp) == HIDP_STATUS_SUCCESS) {
                for (USHORT i = 0; i < n && !id; ++i) {
                    const auto& c = v[i];
                    const bool hit = c.UsagePage == usagePage &&
                                     (c.IsRange ? (usage >= c.Range.UsageMin && usage <= c.Range.UsageMax)
                                                : c.NotRange.Usage == usage);
                    if (hit) id = c.ReportID;
                }
            }
        }
        n = caps.NumberFeatureButtonCaps;
        if (!id && n) {
            std::vector<HIDP_BUTTON_CAPS> b(n);
            if (HidP_GetButtonCaps(HidP_Feature, b.data(), &n, pp) == HIDP_STATUS_SUCCESS) {
                for (USHORT i = 0; i < n && !id; ++i) {
                    const auto& c = b[i];
                    const bool hit = c.UsagePage == usagePage &&
                                     (c.IsRange ? (usage >= c.Range.UsageMin && usage <= c.Range.UsageMax)
                                                : c.NotRange.Usage == usage);
                    if (hit) id = c.ReportID;
                }
            }
        }
    }
    HidD_FreePreparsedData(pp);
    return id;
}

std::vector<Collection> enumerateSurfaceMouse() {
    std::vector<Collection> out;
    GUID guid = GUID_DEVINTERFACE_HID;
    ULONG size = 0;
    if (CM_Get_Device_Interface_List_SizeW(&size, &guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) {
        return out;
    }
    std::vector<wchar_t> buf(size);
    if (CM_Get_Device_Interface_ListW(&guid, nullptr, buf.data(), size, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) {
        return out;
    }
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
        std::wstring path(p);
        if (!isSurfaceMousePath(path)) continue;
        Collection c;
        c.path = path;
        DWORD err = 0;
        HANDLE h = openCollection(path, true, err);
        c.openedReadWrite = h != INVALID_HANDLE_VALUE;
        if (!c.openedReadWrite) {
            c.openError = err;
            h = openCollection(path, false, err);  // access 0 still answers attributes and caps
        }
        if (h != INVALID_HANDLE_VALUE) {
            HIDD_ATTRIBUTES a{};
            a.Size = sizeof a;
            if (HidD_GetAttributes(h, &a)) {
                c.vid = a.VendorID;
                c.pid = a.ProductID;
                c.version = a.VersionNumber;
            }
            PHIDP_PREPARSED_DATA pp = nullptr;
            if (HidD_GetPreparsedData(h, &pp)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                    c.usagePage = caps.UsagePage;
                    c.usage = caps.Usage;
                    c.inputLen = caps.InputReportByteLength;
                    c.outputLen = caps.OutputReportByteLength;
                    c.featureLen = caps.FeatureReportByteLength;
                }
                HidD_FreePreparsedData(pp);
            }
            CloseHandle(h);
        }
        if (c.vid == kVendorId && c.pid == kProductId) out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const Collection& a, const Collection& b) { return a.path < b.path; });
    return out;
}

}  // namespace probe
