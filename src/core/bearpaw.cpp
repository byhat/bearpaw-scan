#include "bearpaw.h"

#include <libusb-1.0/libusb.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

namespace bearpaw {

// ---------- protocol constants (from the recorded session) ----------
static constexpr int kStride = 7680;      // bytes per sensor line
static constexpr int kPx = 2560;          // pixels per sensor line
static constexpr int kSkipLines = 168;    // service lines before the image
static constexpr int kOutW = 2122;        // usable width
static constexpr int kGShift = 66;        // mirrored G-die: shift
// white balance: paper -> white (measured against the reference scan)
static constexpr double kLutR[2] = {0.938, 24.3};
static constexpr double kLutG[2] = {0.915, 24.4};
static constexpr double kLutB[2] = {0.895, 25.5};

// ---------- session.ops: BPS1 format ----------
struct SessionOp {
    uint8_t kind = 0;   // 0=ctrl_out 1=ctrl_in 2=bulk_out 3=bulk_in
    uint8_t bm = 0, req = 0;
    uint16_t val = 0, idx = 0, len = 0;
    double t = 0;
    uint32_t reqLen = 0, actLen = 0;
    std::vector<uint8_t> payload;
};

#pragma pack(push, 1)
struct OpHead {
    uint8_t kind, bm, req;
    uint16_t val, idx, len;
    double t;
    uint32_t rl, al;
};
#pragma pack(pop)
static_assert(sizeof(OpHead) == 25, "OpHead packed");

struct Scanner::Impl {
    LogFn log;
    libusb_context *ctx = nullptr;
    libusb_device_handle *h = nullptr;

    ~Impl() { closeAll(); }

    void closeAll() {
        if (h) {
            libusb_close(h);
            h = nullptr;
        }
        if (ctx) {
            libusb_exit(ctx);
            ctx = nullptr;
        }
    }

    void say(const std::string &m) { if (log) log(m); }
};

Scanner::Scanner(LogFn log) : d(new Impl{std::move(log)}) {}
Scanner::~Scanner() { delete d; }

// ---------- device discovery ----------
std::vector<DeviceInfo> Scanner::findDevices() {
    std::vector<DeviceInfo> out;
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != 0)
        return out;
    libusb_device **list = nullptr;
    const ssize_t n = libusb_get_device_list(ctx, &list);
    for (ssize_t i = 0; i < n; ++i) {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0)
            continue;
        if (desc.idVendor != kVid || desc.idProduct != kPid)
            continue;
        DeviceInfo info;
        info.bus = libusb_get_bus_number(list[i]);
        info.address = libusb_get_device_address(list[i]);
        libusb_device_handle *h = nullptr;
        if (libusb_open(list[i], &h) == 0) {
            char prod[128] = {};
            if (desc.iProduct)
                libusb_get_string_descriptor_ascii(h, desc.iProduct,
                                                   (unsigned char *)prod, sizeof(prod));
            info.product = prod;
            libusb_close(h);
        }
        info.id = std::to_string(info.bus) + ":" + std::to_string(info.address);
        out.push_back(info);
    }
    if (list)
        libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return out;
}

// ---------- session loading ----------
// session.ops embedded by the linker (ld -b binary data/session.ops)
extern "C" const unsigned char _binary_session_ops_start[];
extern "C" const unsigned char _binary_session_ops_end[];

static std::vector<SessionOp> parseSession(const uint8_t *begin, const uint8_t *end) {
    const std::vector<uint8_t> blob(begin, end);
    if (blob.size() < 8 || std::memcmp(blob.data(), "BPS1", 4) != 0)
        return {};
    uint32_t n = 0;
    std::memcpy(&n, blob.data() + 4, 4);
    std::vector<SessionOp> ops;
    ops.reserve(n);
    size_t off = 8;
    for (uint32_t i = 0; i < n; ++i) {
        if (off + sizeof(OpHead) > blob.size())
            return {};
        OpHead h;
        std::memcpy(&h, blob.data() + off, sizeof(h));
        off += sizeof(h);
        SessionOp op;
        op.kind = h.kind; op.bm = h.bm; op.req = h.req;
        op.val = h.val; op.idx = h.idx; op.len = h.len; op.t = h.t;
        op.reqLen = h.rl; op.actLen = h.al;
        if (h.len) {
            if (off + h.len > blob.size())
                return {};
            op.payload.assign(blob.begin() + off, blob.begin() + off + h.len);
            off += h.len;
        }
        ops.push_back(std::move(op));
    }
    return ops;
}

static std::vector<SessionOp> loadSession(const std::string &file) {
    std::ifstream f(file, std::ios::binary);
    if (!f)
        return {};
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    return parseSession(blob.data(), blob.data() + blob.size());
}

// ---------- main loop ----------
bool Scanner::scan(const ScanParams &params, const LineFn &onLine,
                   const ProgressFn &onProgress, const std::atomic_bool &cancel) {
    // session: the embedded copy first, then files (for experiments)
    std::vector<SessionOp> ops = parseSession(_binary_session_ops_start,
                                              _binary_session_ops_end);
    if (ops.empty()) {
        static const char *kCandidates[] = {
            "/usr/share/bearpaw-sane/session.ops",
            "data/session.ops", "../data/session.ops", "session.ops"};
        for (const char *cand : kCandidates) {
            ops = loadSession(cand);
            if (!ops.empty())
                break;
        }
    }
    if (ops.empty()) {
        d->say("!! session.ops not found");
        return false;
    }
    d->say("session: " + std::to_string(ops.size()) + " ops");

    if (libusb_init(&d->ctx) != 0) {
        d->say("!! libusb_init failed");
        return false;
    }
    // pick the first 055f:0408 device
    d->h = libusb_open_device_with_vid_pid(d->ctx, kVid, kPid);
    if (!d->h) {
        d->say("!! could not open scanner (absent or no access; see the udev rule / sudo)");
        d->closeAll();
        return false;
    }

    // pick the configuration with EP 0x82 and claim its interfaces
    libusb_device *dev = libusb_get_device(d->h);
    libusb_config_descriptor *cfgd = nullptr;
    int bestCfg = -1, nIf = 0;
    for (int i = 0;
         libusb_get_config_descriptor(dev, i, &cfgd) == 0 && bestCfg < 0; ++i) {
        bool has82 = false;
        for (int ifn = 0; ifn < cfgd->bNumInterfaces; ++ifn)
            for (int a = 0; a < cfgd->interface[ifn].num_altsetting; ++a)
                for (int e = 0; e < cfgd->interface[ifn].altsetting[a].bNumEndpoints; ++e)
                    if (cfgd->interface[ifn].altsetting[a].endpoint[e].bEndpointAddress == 0x82)
                        has82 = true;
        if (has82) {
            bestCfg = cfgd->bConfigurationValue;
            nIf = cfgd->bNumInterfaces;
        }
        libusb_free_config_descriptor(cfgd);
        cfgd = nullptr;
    }
    if (bestCfg < 0) {
        d->say("!! no configuration with bulk-IN 0x82");
        d->closeAll();
        return false;
    }
    int active = -1;
    if (libusb_get_configuration(d->h, &active) == 0 && active != bestCfg)
        libusb_set_configuration(d->h, bestCfg);
    for (int i = 0; i < nIf; ++i) {
        if (libusb_claim_interface(d->h, i) != 0)
            d->say("claim if" + std::to_string(i) + ": busy/error (continuing)");
    }
    d->say("device ready (config " + std::to_string(bestCfg) + ")");

    // decoder LUTs
    uint8_t lut[3][256];
    for (int v = 0; v < 256; ++v) {
        for (int c = 0; c < 3; ++c) {
            const double *k = c == 0 ? kLutR : (c == 1 ? kLutG : kLutB);
            int r = int(v * k[0] + k[1]);
            lut[c][v] = uint8_t(r < 0 ? 0 : (r > 255 ? 255 : r));
        }
    }
    std::vector<uint8_t> raw(kStride * 64);   // line reassembly buffer
    int rawFill = 0, lineNo = 0;
    std::vector<uint8_t> outRow(params.color ? kOutW * 3 : kOutW);

    const auto feed = [&](const uint8_t *data, int n) {
        while (n > 0) {
            const int take = std::min(n, kStride - rawFill);
            std::memcpy(raw.data() + rawFill, data, take);
            rawFill += take;
            data += take;
            n -= take;
            if (rawFill == kStride) {
                rawFill = 0;
                if (++lineNo > kSkipLines && onLine) {
                    const uint8_t *line = raw.data();
                    // orientation: sensor columns map straight to output
                    // (x -> 219+x); only the G die is mirrored (+shift 66)
                    if (params.color) {
                        for (int x = 0; x < kOutW; ++x) {
                            int pG = kPx - 1 + kGShift - (219 + x);
                            if (pG > kPx - 1) pG = kPx - 1;
                            const int pRB = 219 + x;
                            outRow[3 * x + 0] = lut[0][line[pRB * 3 + 2]];
                            outRow[3 * x + 1] = lut[1][line[pG * 3]];
                            outRow[3 * x + 2] = lut[2][line[pRB * 3 + 1]];
                        }
                        onLine(outRow.data(), kOutW * 3);
                    } else {
                        for (int x = 0; x < kOutW; ++x) {
                            int pG = kPx - 1 + kGShift - (219 + x);
                            if (pG > kPx - 1) pG = kPx - 1;
                            outRow[x] = lut[1][line[pG * 3]];
                        }
                        onLine(outRow.data(), kOutW);
                    }
                }
            }
        }
    };

    // replay: absolute schedule of the recording, no drift accumulation
    const auto t0 = std::chrono::steady_clock::now();
    uint8_t buf[32768];
    int stallsSkipped = 0, errs = 0;
    const int total = int(ops.size());
    for (int i = 0; i < total; ++i) {
        if (cancel.load()) {
            d->say("scan cancelled");
            break;
        }
        const SessionOp &op = ops[i];
        const auto target = t0 + std::chrono::microseconds(int64_t(op.t * 1e6));
        for (std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
             now < target;
             now = std::chrono::steady_clock::now()) {
            const auto remain = std::chrono::duration_cast<std::chrono::microseconds>(target - now);
            if (remain.count() > 2500)
                std::this_thread::sleep_for(remain - std::chrono::microseconds(2000));
        }
        int rc = 0;
        switch (op.kind) {
        case 0:
            rc = libusb_control_transfer(d->h, op.bm, op.req, op.val, op.idx,
                                         const_cast<uint8_t *>(op.payload.data()),
                                         op.len, 1000);
            if (int(rc) < 0) {
                ++errs;
                char m[128];
                std::snprintf(m, sizeof(m), "ctrl_out(0x%04x): %s", op.val, libusb_error_name(rc));
                d->say(m);
            }
            break;
        case 1: {
            uint8_t rbuf[16] = {};
            if (op.len > sizeof(rbuf))
                break;
            for (int attempt = 0; attempt < 4; ++attempt) {
                rc = libusb_control_transfer(d->h, op.bm, op.req, op.val, op.idx,
                                             rbuf, op.len, 300);
                if (rc >= 0)
                    break;
                if (rc == LIBUSB_ERROR_PIPE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }
                break;
            }
            if (rc == LIBUSB_ERROR_PIPE && op.req == 12)
                ++stallsSkipped;   // status polls are not critical
            else if (rc < 0)
                ++errs;
            break;
        }
        case 2:
            rc = libusb_bulk_transfer(d->h, 0x01,
                                      const_cast<uint8_t *>(op.payload.data()),
                                      int(op.payload.size()), &rc, 5000);
            if (rc != 0) {
                ++errs;
                d->say(std::string("bulk_out: ") + libusb_error_name(rc));
            }
            break;
        case 3: {
            const int size = std::max(std::max(int(op.reqLen), int(op.actLen)), 1);
            int got = 0;
            const int rc3 = libusb_bulk_transfer(d->h, 0x82, buf,
                                                 std::min(size, int(sizeof(buf))),
                                                 &got, 1000);
            if (rc3 == 0 && got > 0)
                feed(buf, got);
            else if (rc3 == LIBUSB_ERROR_PIPE)
                libusb_clear_halt(d->h, 0x82);
            /* ETIMEDOUT: empty FIFO — just carry on */
            break;
        }
        }
        if (errs > 20) {
            d->say("!! too many USB errors");
            d->closeAll();
            return false;
        }
        if ((i & 255) == 0 && onProgress)
            onProgress(double(i) / total * 100.0, "");
    }
    if (onProgress)
        onProgress(100.0, "");
    char m[160];
    std::snprintf(m, sizeof(m), "replay done: %d image lines, %d stalled status polls skipped",
                  lineNo - kSkipLines, stallsSkipped);
    d->say(m);

    d->closeAll();
    return errs <= 20;
}

} // namespace bearpaw
