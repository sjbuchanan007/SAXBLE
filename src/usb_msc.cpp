#include "usb_msc.h"
#include "session_log.h"
#include <SD.h>
#include "USB.h"
#include "USBMSC.h"

namespace UsbMsc {
namespace {

USBMSC g_msc;
bool   g_ready  = false;
bool   g_active = false;

constexpr uint16_t kSec = 512;   // SD sector size (standard)

// Map MSC block I/O onto raw SD sector reads/writes. TinyUSB may issue
// transfers with a byte offset and sizes that aren't a whole sector, so handle
// the general case via a per-sector scratch buffer.
int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint8_t  tmp[kSec];
    uint32_t done = 0;
    while (done < bufsize) {
        uint32_t pos   = offset + done;
        uint32_t sec   = lba + pos / kSec;
        uint32_t so    = pos % kSec;
        uint32_t chunk = kSec - so;
        if (chunk > bufsize - done) chunk = bufsize - done;
        if (!SD.readRAW(tmp, sec)) return -1;
        memcpy(dst + done, tmp + so, chunk);
        done += chunk;
    }
    return done;
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    uint8_t  tmp[kSec];
    uint32_t done = 0;
    while (done < bufsize) {
        uint32_t pos   = offset + done;
        uint32_t sec   = lba + pos / kSec;
        uint32_t so    = pos % kSec;
        uint32_t chunk = kSec - so;
        if (chunk > bufsize - done) chunk = bufsize - done;
        if (chunk != kSec && !SD.readRAW(tmp, sec)) return -1;  // read-modify-write
        memcpy(tmp + so, buffer + done, chunk);
        if (!SD.writeRAW(tmp, sec)) return -1;
        done += chunk;
    }
    return done;
}

bool onStartStop(uint8_t /*power*/, bool /*start*/, bool /*load_eject*/) {
    return true;
}

} // namespace

bool begin() {
    if (g_ready) return true;
    if (!SessionLog::mountSd()) return false;

    uint32_t sectors = SD.numSectors();
    if (sectors == 0) return false;

    g_msc.vendorID("SAXBLE");
    g_msc.productID("SD Card");
    g_msc.productRevision("1.0");
    g_msc.onStartStop(onStartStop);
    g_msc.onRead(onRead);
    g_msc.onWrite(onWrite);
    g_msc.mediaPresent(false);          // detached until the user activates export
    g_msc.begin(sectors, kSec);

    USB.begin();
    g_ready = true;
    return true;
}

void setActive(bool on) {
    if (!g_ready) { g_active = false; return; }
    g_active = on;
    g_msc.mediaPresent(on);
    if (!on) {
        // Re-mount so the device's view of the card reflects host changes.
        SD.end();
        SessionLog::mountSd();
    }
}

bool active()    { return g_active; }
bool available() { return g_ready; }

} // namespace UsbMsc
