#pragma once
#include <windows.h>
#include <cfgmgr32.h>

#include <atomic>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/haptic_table.h"

namespace ixpat::win {

struct DeviceInfo {
    bool connected = false;
    std::string firmware = "-";  // "5.29"
    std::wstring path;           // the 0x0E collection's interface path
    std::optional<int> battery;  // percent, from the Bluetooth LE device
    std::vector<Waveform> table{measuredWaveforms().begin(), measuredWaveforms().end()};
    bool tableFromDevice = false;
    std::string lastError;
};

/// The Surface Mouse's haptics collection (usage page 0x0E) over the HID class driver: finds it,
/// reads the waveform table, fires triggers from a serial queue, and follows arrival / removal.
/// Only output report 0x43 is ever written. One worker thread; callbacks run on it.
class HidDevice {
public:
    HidDevice();
    ~HidDevice();
    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    /// Called on the HID thread after connect, disconnect, table read or battery refresh.
    std::function<void(const DeviceInfo&)> onChange;

    void start();
    void stop();
    /// Any thread. Bounded queue (8): the oldest pending trigger is dropped when it is full.
    void trigger(int ordinal, int intensity);
    void refreshBattery();
    void refreshTable();
    DeviceInfo snapshot() const;

private:
    struct Pending {
        std::uint8_t ordinal, intensity;
    };
    void loop();
    bool tryOpen();
    void close(const char* why);
    void readTable();
    void readBattery();
    void publish();
    static DWORD CALLBACK cmNotify(HCMNOTIFICATION, PVOID ctx, CM_NOTIFY_ACTION, PCM_NOTIFY_EVENT_DATA, DWORD);

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    DeviceInfo info_;
    std::deque<Pending> queue_;
    HANDLE device_ = INVALID_HANDLE_VALUE;
    std::uint16_t outputLen_ = 0, featureLen_ = 0;
    HANDLE wake_ = nullptr;     // queue or enumerate request
    HANDLE stopEvent_ = nullptr;
    std::atomic<bool> enumerate_{true};
    std::atomic<bool> battery_{true};
    std::atomic<bool> table_{false};
    HCMNOTIFICATION notify_ = nullptr;
    std::thread thread_;
};

}  // namespace ixpat::win
