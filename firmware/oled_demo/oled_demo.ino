#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

constexpr int SDA_PIN = 4;
constexpr int SCL_PIN = 5;
constexpr int RELAY_A_PIN = 8;
constexpr int RELAY_B_PIN = 10;
constexpr bool RELAY_ACTIVE_LOW = false;
constexpr uint32_t STEP_MS = 2000;
constexpr uint32_t REFRESH_MS = 400;
constexpr uint32_t RELAY_SETTLE_MS = 80;
Adafruit_SSD1306 display(128, 64, &Wire, -1, 100000, 100000);
bool ready = false;
uint8_t address = 0;
uint8_t relayState = 0;
uint32_t lastStep = 0;
uint32_t lastRetry = 0;
uint32_t lastRefresh = 0;
uint32_t switchedAt = 0;
uint32_t framesOk = 0;
uint32_t framesFailed = 0;
bool displayDirty = true;
bool injectFrameFailure = false;

bool initializeDisplay() {
  address = 0;
  Serial.println("Scanning I2C: SDA=4 SCL=5, 100 kHz");
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C ACK at 0x%02X\n", a);
      if (a == 0x3C || (a == 0x3D && address == 0)) address = a;
    }
  }
  if (!address) {
    Serial.println("No OLED at 0x3C/0x3D. Check power, labels and wiring.");
    return false;
  }
  // Keep the explicit Wire pin assignment; no reset pin on four-pin modules.
  if (!display.begin(SSD1306_SWITCHCAPVCC, address, false, false)) {
    Serial.println("Display buffer allocation failed.");
    return false;
  }
  Serial.printf("Using 0x%02X; assuming SSD1306 128x64. Confirm visually.\n", address);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  return true;
}

void setup() {
  const int offLevel = RELAY_ACTIVE_LOW ? HIGH : LOW;
  digitalWrite(RELAY_A_PIN, offLevel);
  digitalWrite(RELAY_B_PIN, offLevel);
  pinMode(RELAY_A_PIN, OUTPUT);
  pinMode(RELAY_B_PIN, OUTPUT);
  Serial.begin(115200);
  // Diagnostics must never stall relay/display updates when USB isn't read.
  Serial.setTxTimeoutMs(0);
  // Boot without needing a serial monitor connected.
  delay(1500);
  Serial.printf("ESP32-C3 OLED demo; flash=%u bytes\n", ESP.getFlashChipSize());
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);
  ready = initializeDisplay();
  lastRetry = millis();
  lastStep = millis();
  updateRelays();
}

void drawCell(int y, const char* name, bool active) {
  const uint16_t background = active ? SSD1306_WHITE : SSD1306_BLACK;
  const uint16_t foreground = active ? SSD1306_BLACK : SSD1306_WHITE;
  display.fillRect(0, y, 128, 32, background);
  display.drawRect(0, y, 128, 32, SSD1306_WHITE);
  display.setTextSize(2);
  display.setTextColor(foreground, background);
  char label[8];
  snprintf(label, sizeof(label), "%s: %s", name, active ? "ON" : "OFF");
  display.setCursor((128 - strlen(label) * 12) / 2, y + 8);
  display.print(label);
}

bool sendDisplayPacket(uint8_t control, const uint8_t* data, size_t count) {
  Wire.beginTransmission(address);
  Wire.write(control);
  const size_t written = Wire.write(data, count);
  const uint8_t error = Wire.endTransmission();
  if (error || written != count) {
    Serial.printf("OLED transfer error=%u bytes=%u/%u at %lu ms\n", error,
                  (unsigned)written, (unsigned)count, (unsigned long)millis());
    return false;
  }
  return true;
}

bool flushDisplay() {
  // Always restart addressing: a partial frame must not offset the next frame.
  const uint8_t commands[] = {0x20, 0x00, 0x21, 0, 127, 0x22, 0, 7,
                              0xA4, 0xA6, 0xAF};
  if (!sendDisplayPacket(0x00, commands, sizeof(commands))) return false;
  for (size_t offset = 0; offset < 1024; offset += 32) {
    // Serial 'f' tests recovery from a partial frame without touching relays.
    if (injectFrameFailure && offset == 256) {
      injectFrameFailure = false;
      Serial.println("TEST: interrupted OLED frame after 256 bytes");
      return false;
    }
    if (!sendDisplayPacket(0x40, display.getBuffer() + offset, 32)) return false;
  }
  return true;
}

void drawStatus() {
  if (!ready) return;
  display.clearDisplay();
  drawCell(0, "A", (relayState & 2) != 0);
  drawCell(32, "B", (relayState & 1) != 0);
  // Dark separator remains visible when both cells are highlighted.
  display.drawFastHLine(1, 31, 126, SSD1306_BLACK);
  if (flushDisplay()) {
    ++framesOk;
    if (displayDirty) {
      Serial.printf("OLED synced state=%u at %lu ms ok=%lu failed=%lu\n",
                    relayState, (unsigned long)millis(),
                    (unsigned long)framesOk, (unsigned long)framesFailed);
    }
    displayDirty = false;
  } else {
    ++framesFailed;
    displayDirty = true;
    // The next refresh retries the CURRENT state, never a queued old frame.
  }
}

void updateRelays() {
  const bool a = (relayState & 2) != 0;
  const bool b = (relayState & 1) != 0;
  // Deassert GPIOs first; mechanical contact timing is not interlocked.
  const int offLevel = RELAY_ACTIVE_LOW ? HIGH : LOW;
  const int onLevel = RELAY_ACTIVE_LOW ? LOW : HIGH;
  if (!a) digitalWrite(RELAY_A_PIN, offLevel);
  if (!b) digitalWrite(RELAY_B_PIN, offLevel);
  if (a) digitalWrite(RELAY_A_PIN, onLevel);
  if (b) digitalWrite(RELAY_B_PIN, onLevel);
  switchedAt = millis();
  displayDirty = true;
  Serial.printf("State %u: A=%s B=%s (active-%s) at %lu ms\n", relayState,
                a ? "ON" : "OFF", b ? "ON" : "OFF",
                RELAY_ACTIVE_LOW ? "low" : "high", (unsigned long)switchedAt);
}

void loop() {
  const uint32_t now = millis();
  if (now - lastStep >= STEP_MS) {
    const uint32_t steps = (now - lastStep) / STEP_MS;
    lastStep += steps * STEP_MS;
    relayState = (relayState + steps) & 3;
    updateRelays();
  }
  while (Serial.available()) {
    if (Serial.read() == 'f') injectFrameFailure = true;
  }
  if (!ready && millis() - lastRetry >= 5000) {
    lastRetry = millis();
    ready = initializeDisplay();
    displayDirty = true;
  }
  const uint32_t refreshNow = millis();
  // Keep display traffic away from the immediate relay switching transient.
  // Avoid beginning a ~100 ms frame immediately before the next relay step.
  if (ready && refreshNow - switchedAt >= RELAY_SETTLE_MS &&
      refreshNow - lastRefresh >= REFRESH_MS &&
      STEP_MS - (refreshNow - lastStep) > 200 &&
      refreshNow - lastStep < STEP_MS) {
    lastRefresh = refreshNow;
    drawStatus();
  }
  delay(5);
}
