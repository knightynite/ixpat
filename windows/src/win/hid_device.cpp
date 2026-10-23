#include "win/hid_device.h"

#include <cfgmgr32.h>
#include <devpkey.h>
#include <hidsdi.h>
#include <initguid.h>
#include <hidclass.h>

#include <algorithm>
#include <cwctype>

#include "win/log.h"

namespace ixpat::win {
namespace {

constexpr std::uint16_t kVendorId = 0x045E, kProductId = 0x0880;
constexpr std::uint16_t kHapticsPage = 0x0E;
constexpr std::uint8_t kOutputReport = 0x43, kTableReport = 0x42;
constexpr std::size_t kQueueLimit = 8;
constexpr DWORD kReenumerateMs = 5000;
constexpr DWORD kBatteryMs = 15 * 60 * 1000;

// DEVPKEY_Bluetooth_Battery: {104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2, a BYTE percent on the BTHLE device node.
const DEVPROPKEY kBatteryKey = {{0x104EA319, 0x6EE2, 0x4701, {0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5}}, 2};

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

/// Interface paths of the Surface Mouse's haptics collections present now.
std::vector<std::wstring> hapticsPaths() {
    std::vector<std::wstring> out;
    GUID guid = GUID_DEVINTERFACE_HID;
    ULONG size = 0;
    if (CM_Get_Device_Interface_List_SizeW(&size, &guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) return out;
    std::vector<wchar_t> buf(size);
    if (CM_Get_Device_Interface_ListW(&guid, nullptr, buf.data(), size, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) return out;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
        std::wstring path(p), l = lower(path);
        if (l.find(L"vid&02045e_pid&0880") == std::wstring::npos && l.find(L"vid_045e&pid_0880") == std::wstring::npos) continue;
        out.push_back(std::move(path));
    }
    return out;
}

}  // namespace

HidDevice::HidDevice() = default;
HidDevice::~HidDevice() { stop(); }

DeviceInfo HidDevice::snapshot() const {
    AcquireSRWLockShared(&lock_);
    DeviceInfo copy = info_;
    ReleaseSRWLockShared(&lock_);
    return copy;
}

void HidDevice::publish() {
    if (onChange) onChange(snapshot());
}

void HidDevice::start() {
    if (thread_.joinable()) return;
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof filter;
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_HID;
    if (CM_Register_Notification(&filter, this, &HidDevice::cmNotify, &notify_) != CR_SUCCESS) {
        debugLog("hid: CM_Register_Notification failed; relying on the 5 s re-enumeration");
        notify_ = nullptr;
    }
    thread_ = std::thread([this] { loop(); });
}

void HidDevice::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
    if (notify_) CM_Unregister_Notification(notify_);
    notify_ = nullptr;
    CloseHandle(wake_);
    CloseHandle(stopEvent_);
    wake_ = stopEvent_ = nullptr;
}

DWORD CALLBACK HidDevice::cmNotify(HCMNOTIFICATION, PVOID ctx, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD) {
    auto* self = static_cast<HidDevice*>(ctx);
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
        self->enumerate_ = true;
        if (self->wake_) SetEvent(self->wake_);
    }
    return ERROR_SUCCESS;
}

void HidDevice::trigger(int ordinal, int intensity) {
    AcquireSRWLockExclusive(&lock_);
    if (queue_.size() >= kQueueLimit) queue_.pop_front();
    queue_.push_back({static_cast<std::uint8_t>(std::clamp(ordinal, 0, 255)),
                      static_cast<std::uint8_t>(std::clamp(intensity, 0, 4))});
    ReleaseSRWLockExclusive(&lock_);
    if (wake_) SetEvent(wake_);
}

void HidDevice::refreshBattery() {
    battery_ = true;
    if (wake_) SetEvent(wake_);
}

bool HidDevice::tryOpen() {
    for (const auto& path : hapticsPaths()) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            // Mouse/keyboard collections refuse RW (that is expected); remember the error for the haptics one only.
            continue;
        }
        HIDD_ATTRIBUTES a{};
        a.Size = sizeof a;
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        const bool ok = HidD_GetAttributes(h, &a) && a.VendorID == kVendorId && a.ProductID == kProductId &&
                        HidD_GetPreparsedData(h, &pp) && HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS &&
                        caps.UsagePage == kHapticsPage;
        if (pp) HidD_FreePreparsedData(pp);
        if (!ok) {
            CloseHandle(h);
            continue;
        }
        device_ = h;
        outputLen_ = caps.OutputReportByteLength;
        featureLen_ = caps.FeatureReportByteLength;
        char fw[16];
        std::snprintf(fw, sizeof fw, "%X.%02X", a.VersionNumber >> 8, a.VersionNumber & 0xFF);
        AcquireSRWLockExclusive(&lock_);
        info_.connected = true;
        info_.firmware = fw;
        info_.path = path;
        info_.lastError.clear();
        ReleaseSRWLockExclusive(&lock_);
        debugLog(std::string("Surface Mouse haptics attached (fw ") + fw + ", out " + std::to_string(outputLen_) +
                 " feat " + std::to_string(featureLen_) + ")");
        readTable();
        battery_ = true;
        publish();
        return true;
    }
    return false;
}

void HidDevice::close(const char* why) {
    if (device_ == INVALID_HANDLE_VALUE) return;
    CloseHandle(device_);
    device_ = INVALID_HANDLE_VALUE;
    AcquireSRWLockExclusive(&lock_);
    info_.connected = false;
    info_.battery.reset();
    queue_.clear();
    ReleaseSRWLockExclusive(&lock_);
    debugLog(std::string("Surface Mouse haptics detached (") + why + ")");
    publish();
}

void HidDevice::readTable() {
    if (device_ == INVALID_HANDLE_VALUE) return;
    std::vector<std::uint8_t> buf(std::max<std::size_t>(featureLen_, 201), 0);
    buf[0] = kTableReport;
    if (!HidD_GetFeature(device_, buf.data(), static_cast<ULONG>(buf.size()))) {
        debugLog("hid: HidD_GetFeature(0x42) failed, error " + std::to_string(GetLastError()) + "; using the measured table");
        return;
    }
    auto table = parseHapticTable(buf);
    if (table.empty()) {
        debugLog("hid: feature 0x42 held no waveforms; using the measured table");
        return;
    }
    AcquireSRWLockExclusive(&lock_);
    info_.table = std::move(table);
    info_.tableFromDevice = true;
    ReleaseSRWLockExclusive(&lock_);
}

void HidDevice::readBattery() {
    std::wstring path;
    AcquireSRWLockShared(&lock_);
    path = info_.path;
    ReleaseSRWLockShared(&lock_);
    if (path.empty()) return;
    // Interface -> device instance -> up the parent chain until a node carries the battery property.
    DEVPROPTYPE type = 0;
    wchar_t instance[MAX_DEVICE_ID_LEN]{};
    ULONG size = sizeof instance;
    if (CM_Get_Device_Interface_PropertyW(path.c_str(), &DEVPKEY_Device_InstanceId, &type, reinterpret_cast<PBYTE>(instance), &size, 0) != CR_SUCCESS) return;
    DEVINST node = 0;
    if (CM_Locate_DevNodeW(&node, instance, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) return;
    std::optional<int> pct;
    for (int depth = 0; depth < 5 && !pct; ++depth) {
        BYTE value = 0;
        ULONG vsize = sizeof value;
        if (CM_Get_DevNode_PropertyW(node, &kBatteryKey, &type, &value, &vsize, 0) == CR_SUCCESS && type == DEVPROP_TYPE_BYTE) {
            pct = value;
            break;
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    AcquireSRWLockExclusive(&lock_);
    info_.battery = pct;
    ReleaseSRWLockExclusive(&lock_);
}

void HidDevice::loop() {
    HANDLE waits[2] = {stopEvent_, wake_};
    ULONGLONG nextBattery = 0;
    bool loggedAbsent = false;
    for (;;) {
        const DWORD timeout = device_ == INVALID_HANDLE_VALUE ? kReenumerateMs : kBatteryMs;
        const DWORD r = WaitForMultipleObjects(2, waits, FALSE, timeout);
        if (r == WAIT_OBJECT_0) break;
        if (enumerate_.exchange(false) || (r == WAIT_TIMEOUT && device_ == INVALID_HANDLE_VALUE)) {
            if (device_ == INVALID_HANDLE_VALUE) {
                if (tryOpen()) loggedAbsent = false;
                else if (!loggedAbsent) {
                    loggedAbsent = true;
                    debugLog("no Surface Mouse haptics collection present (paired but asleep, or on another host)");
                }
            }
        }
        // Drain the trigger queue serially.
        for (;;) {
            Pending p{};
            AcquireSRWLockExclusive(&lock_);
            const bool have = !queue_.empty();
            if (have) {
                p = queue_.front();
                queue_.pop_front();
            }
            ReleaseSRWLockExclusive(&lock_);
            if (!have) break;
            if (device_ == INVALID_HANDLE_VALUE) continue;
            std::vector<std::uint8_t> buf(std::max<std::size_t>(outputLen_, 3), 0);
            buf[0] = kOutputReport;
            buf[1] = p.ordinal;
            buf[2] = p.intensity;
            if (!HidD_SetOutputReport(device_, buf.data(), static_cast<ULONG>(buf.size()))) {
                const DWORD e = GetLastError();
                if (e == ERROR_DEVICE_NOT_CONNECTED || e == ERROR_GEN_FAILURE || e == ERROR_NOT_READY || e == ERROR_INVALID_HANDLE) {
                    close("write failed");
                    enumerate_ = true;
                    break;
                }
                AcquireSRWLockExclusive(&lock_);
                info_.lastError = "haptic SetOutputReport failed, error " + std::to_string(e);
                ReleaseSRWLockExclusive(&lock_);
            }
        }
        if (device_ != INVALID_HANDLE_VALUE && (battery_.exchange(false) || GetTickCount64() >= nextBattery)) {
            readBattery();
            nextBattery = GetTickCount64() + kBatteryMs;
            publish();
        }
    }
    close("stopped");
}

}  // namespace ixpat::win
