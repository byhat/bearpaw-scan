// libbearpawcore — driver engine for the Mustek BearPaw 2448 CU Pro (055f:0408).
//
// The protocol was reverse-engineered from a live USB capture of the
// vendor software (Nov 2026):
//   * control: vendor control transfers (bmReq 0x40/0xc0, bReq 4/12);
//   * data: bulk IN 0x82; auxiliary writes: bulk OUT 0x01;
//   * image line: 7680 bytes = 2560 px, channels in GBR order,
//     the G die is mirrored in X with a 66 px shift, the whole frame
//     is mirrored, usable width 2122 px (219 px margins), 8 bit.
#pragma once

#include <cstdint>
#include <atomic>
#include <string>
#include <vector>
#include <functional>

namespace bearpaw {

constexpr uint16_t kVid = 0x055f;
constexpr uint16_t kPid = 0x0408;

struct DeviceInfo {
    int bus = 0, address = 0;
    std::string product;     // e.g. "USB2.0 Scanner"
    std::string id;          // "bus:addr"
};

struct ScanParams {
    bool color = true;       // false = grayscale (G channel)
    int  width = 2122;       // output width (as recorded)
};

// One decoded image line: RGB interleaved (len = width*3) or gray (len = width).
using LineFn     = std::function<void(const uint8_t *line, int len)>;
using ProgressFn = std::function<void(double pct, const std::string &stage)>;
using LogFn      = std::function<void(const std::string &msg)>;

class Scanner {
public:
    explicit Scanner(LogFn log = nullptr);
    ~Scanner();

    static std::vector<DeviceInfo> findDevices();

    // Synchronous full scan cycle (~50 s): init -> calibration -> scan ->
    // carriage return. Lines are delivered as they are decoded via onLine.
    // Returns false on error (details go to the log callback).
    bool scan(const ScanParams &params,
              const LineFn &onLine,
              const ProgressFn &onProgress,
              const std::atomic_bool &cancel);

private:
    struct Impl;
    Impl *d = nullptr;
};

} // namespace bearpaw
