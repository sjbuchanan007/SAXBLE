// SAXBLE Tab5 bring-up test #1 — touchscreen + display.
//
// Goal: confirm the M5Stack Tab5's 5" 1280x720 capacitive panel and M5GFX
// drawing work before porting the full app. No BLE here.
//
// What you should see after flashing:
//   * A title bar and a live readout of every touch point (the GT911 controller
//     reports up to 5 fingers).
//   * A coloured dot follows each finger; a fading trail is left behind.
//   * A "CLEAR" button (top-right) wipes the canvas and resets the tap counter.
//   * Every press is also printed over USB serial (115200) so you can confirm
//     coordinates even if the panel looks off.
//
// If touches register but X/Y look mirrored or swapped, that's an orientation
// issue — adjust setRotation() below; the panel itself is working.

#include <M5Unified.h>

namespace {

// Up to 5 simultaneous touches on the GT911. Give each a distinct colour so
// multi-touch is obvious.
constexpr int kMaxTouch = 5;
const uint16_t kFingerColor[kMaxTouch] = {
    TFT_RED, TFT_GREEN, TFT_CYAN, TFT_YELLOW, TFT_MAGENTA};

constexpr int kHeaderH = 64;     // title bar height
int           g_clearX0 = 0;     // CLEAR button hit-box (computed in setup)
int           g_clearY0 = 0;
int           g_clearX1 = 0;
int           g_clearY1 = 0;
uint32_t      g_tapCount = 0;

void drawHeader() {
    auto& d = M5.Display;
    d.fillRect(0, 0, d.width(), kHeaderH, TFT_NAVY);
    d.setTextColor(TFT_WHITE, TFT_NAVY);
    d.setTextSize(3);
    d.setCursor(16, 18);
    d.print("Tab5 touch test");

    // CLEAR button on the right.
    const int bw = 200, bh = 48;
    g_clearX0 = d.width() - bw - 12;
    g_clearY0 = (kHeaderH - bh) / 2;
    g_clearX1 = g_clearX0 + bw;
    g_clearY1 = g_clearY0 + bh;
    d.fillRoundRect(g_clearX0, g_clearY0, bw, bh, 8, TFT_DARKGREY);
    d.drawRoundRect(g_clearX0, g_clearY0, bw, bh, 8, TFT_WHITE);
    d.setTextColor(TFT_WHITE, TFT_DARKGREY);
    d.setCursor(g_clearX0 + 40, g_clearY0 + 14);
    d.print("CLEAR");
}

bool inClearButton(int x, int y) {
    return x >= g_clearX0 && x <= g_clearX1 && y >= g_clearY0 && y <= g_clearY1;
}

void clearCanvas() {
    auto& d = M5.Display;
    d.fillRect(0, kHeaderH, d.width(), d.height() - kHeaderH, TFT_BLACK);
    g_tapCount = 0;
}

// Live readout strip just under the header: how many fingers + their coords.
void drawReadout(int count) {
    auto& d = M5.Display;
    const int y = kHeaderH + 4;
    d.fillRect(0, y, d.width(), 30, TFT_BLACK);
    d.setTextSize(2);
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.setCursor(16, y + 4);
    d.printf("fingers:%d  taps:%lu", count, (unsigned long)g_tapCount);

    for (int i = 0; i < count && i < kMaxTouch; ++i) {
        auto t = M5.Touch.getDetail(i);
        d.setTextColor(kFingerColor[i], TFT_BLACK);
        d.setCursor(320 + i * 170, y + 4);
        d.printf("%d:%d,%d", i, t.x, t.y);
    }
}

} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // Landscape: 1280 wide x 720 tall. Adjust if your unit boots rotated.
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    drawHeader();

    Serial.begin(115200);
    Serial.printf("Tab5 touch test: display %dx%d\n",
                  M5.Display.width(), M5.Display.height());
}

void loop() {
    M5.update();

    int count = M5.Touch.getCount();

    for (int i = 0; i < count && i < kMaxTouch; ++i) {
        auto t = M5.Touch.getDetail(i);

        // Ignore touches landing on the header (that's the button zone).
        if (t.y < kHeaderH) {
            if (t.wasPressed() && inClearButton(t.x, t.y)) {
                clearCanvas();
                Serial.println("CLEAR pressed");
            }
            continue;
        }

        // Draw the finger as a filled dot; the canvas is never cleared between
        // frames, so movement leaves a trail.
        M5.Display.fillCircle(t.x, t.y, 8, kFingerColor[i]);

        if (t.wasPressed()) {
            g_tapCount++;
            Serial.printf("touch %d down @ %d,%d\n", i, t.x, t.y);
        }
    }

    drawReadout(count);
    delay(5);
}
