#include <Arduino.h>
#include <U8g2lib.h>
#include <NimBLEDevice.h>
#include "driver/gpio.h"

static const char *BLE_SERVICE_UUID = "7b7f0001-6c2b-4a73-9b1f-0f5a0b7e1000";
static const char *BLE_LANGUAGE_UUID = "7b7f0002-6c2b-4a73-9b1f-0f5a0b7e1000";

// Set to 1 when the whole device is mounted upside down, or to 0 for the
// normal orientation. This controls both drawing and animation direction.
#define DEVICE_UPSIDE_DOWN 1
#if DEVICE_UPSIDE_DOWN
#define DISPLAY_ROTATION U8G2_R2
#else
#define DISPLAY_ROTATION U8G2_R0
#endif

// Each OLED has its own SDA and SCL lines because the modules share an address.
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oled1(DISPLAY_ROTATION, 5, 8, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oled2(DISPLAY_ROTATION, 6, 9, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oled3(DISPLAY_ROTATION, 7, 4, U8X8_PIN_NONE);

String inputLanguage = "--";
String previousLanguage = "--";
String nextLanguage = "--";
String displayMode = "CAROUSEL";
String toggleLanguage = "--";

static constexpr uint8_t CHANNELS = 3;
static constexpr size_t FRAME_SIZE = 128 * 64 / 8;
static constexpr uint8_t SDA_PINS[CHANNELS] = {8, 9, 4};
static constexpr uint8_t SCL_PINS[CHANNELS] = {5, 6, 7};
static constexpr uint8_t OLED_WRITE_ADDRESS = 0x78;
static uint8_t frameBuffers[CHANNELS][FRAME_SIZE];
static uint8_t oldFrameBuffers[CHANNELS][FRAME_SIZE];
static uint8_t animationBuffers[CHANNELS][FRAME_SIZE];

static inline void parallelSetSda(uint8_t value) {
  uint32_t mask = 0;
  for (uint8_t i = 0; i < CHANNELS; ++i) {
    if (value & (1 << i)) mask |= (1UL << SDA_PINS[i]);
  }
  const uint32_t allSda = (1UL << SDA_PINS[0]) | (1UL << SDA_PINS[1]) | (1UL << SDA_PINS[2]);
  GPIO.out_w1tc.val = allSda;
  GPIO.out_w1ts.val = mask;
}

static inline void parallelSetScl(bool high) {
  const uint32_t allScl = (1UL << SCL_PINS[0]) | (1UL << SCL_PINS[1]) | (1UL << SCL_PINS[2]);
  if (high) GPIO.out_w1ts.val = allScl;
  else GPIO.out_w1tc.val = allScl;
}

static inline void parallelClock() {
  delayMicroseconds(1);
  parallelSetScl(true);
  delayMicroseconds(1);
  parallelSetScl(false);
}

static void parallelStart() {
  parallelSetSda(0x07);
  parallelSetScl(true);
  delayMicroseconds(1);
  parallelSetSda(0);
  delayMicroseconds(1);
  parallelSetScl(false);
}

static void parallelStop() {
  parallelSetSda(0);
  parallelSetScl(true);
  delayMicroseconds(1);
  parallelSetSda(0x07);
  delayMicroseconds(1);
}

static void parallelWriteByte(const uint8_t bytes[CHANNELS]) {
  for (int8_t bit = 7; bit >= 0; --bit) {
    uint8_t sda = 0;
    for (uint8_t channel = 0; channel < CHANNELS; ++channel) {
      if (bytes[channel] & (1 << bit)) sda |= (1 << channel);
    }
    parallelSetSda(sda);
    parallelClock();
  }

  // Release SDA for ACK. ACK is intentionally ignored because all three
  // displays receive the same transaction structure.
  parallelSetSda(0x07);
  parallelClock();
}

static void parallelWriteCommand(uint8_t command) {
  const uint8_t bytes[CHANNELS] = {command, command, command};
  parallelWriteByte(bytes);
}

static void parallelSendFrames(const uint8_t frames[CHANNELS][FRAME_SIZE]) {
  for (uint8_t page = 0; page < 8; ++page) {
    parallelStart();
    const uint8_t address[CHANNELS] = {OLED_WRITE_ADDRESS, OLED_WRITE_ADDRESS, OLED_WRITE_ADDRESS};
    parallelWriteByte(address);
    parallelWriteCommand(0x00);
    parallelWriteCommand(0xB0 | page);
    parallelWriteCommand(0x00);
    parallelWriteCommand(0x10);
    parallelStop();

    parallelStart();
    parallelWriteByte(address);
    parallelWriteCommand(0x40);
    for (uint16_t column = 0; column < 128; ++column) {
      const uint8_t data[CHANNELS] = {
        frames[0][page * 128 + column],
        frames[1][page * 128 + column],
        frames[2][page * 128 + column]
      };
      parallelWriteByte(data);
    }
    parallelStop();
  }
}

static void animateFrames(bool moveLeft) {
  // The whole framebuffer contains only the language, so every page slides.
  for (uint16_t offset = 16; offset <= 128; offset += 16) {
    memcpy(animationBuffers, frameBuffers, sizeof(animationBuffers));

    for (uint8_t channel = 0; channel < CHANNELS; ++channel) {
      // A screen whose language did not change must remain visually still.
      if (memcmp(oldFrameBuffers[channel], frameBuffers[channel], FRAME_SIZE) == 0) {
        continue;
      }
      for (uint8_t page = 0; page < 8; ++page) {
        const uint16_t row = page * 128;
        for (uint16_t x = 0; x < 128; ++x) {
          uint8_t pixelColumn;
          if (moveLeft) {
            pixelColumn = (x + offset < 128)
                ? oldFrameBuffers[channel][row + x + offset]
                : frameBuffers[channel][row + x + offset - 128];
          } else {
            pixelColumn = (x >= offset)
                ? oldFrameBuffers[channel][row + x - offset]
                : frameBuffers[channel][row + 128 - offset + x];
          }
          animationBuffers[channel][row + x] = pixelColumn;
        }
      }
    }

    parallelSendFrames(animationBuffers);
    delay(6);
  }
}

static inline bool framePixel(const uint8_t *frame, int16_t x, int16_t y) {
  if (x < 0 || x >= 128 || y < 0 || y >= 64) return false;
  return frame[(y / 8) * 128 + x] & (1U << (y & 7));
}

static inline void setFramePixel(uint8_t *frame, int16_t x, int16_t y) {
  frame[(y / 8) * 128 + x] |= (1U << (y & 7));
}

static void composeVerticalTransition(uint8_t *destination,
                                      const uint8_t *oldFrame,
                                      const uint8_t *newFrame,
                                      int16_t oldOffset,
                                      int16_t newOffset) {
  memset(destination, 0, FRAME_SIZE);
  for (int16_t y = 0; y < 64; ++y) {
    for (int16_t x = 0; x < 128; ++x) {
      if (framePixel(oldFrame, x, y - oldOffset) ||
          framePixel(newFrame, x, y - newOffset)) {
        setFramePixel(destination, x, y);
      }
    }
  }
}

static void animateToggleExchange() {
  // The two languages move in opposite vertical directions, making the
  // two-screen exchange clearly different from the regular horizontal slide.
  // Physical screens 1 and 2 are channels 2 and 1 after the 180-degree
  // rotation. Physical screen 3 (channel 0) is copied unchanged.
#if DEVICE_UPSIDE_DOWN
  constexpr int8_t oled1Direction = 1;  // raw pixels are rotated by U8G2_R2
#else
  constexpr int8_t oled1Direction = -1;
#endif
  constexpr int8_t oled2Direction = -oled1Direction;

  for (int16_t offset = 8; offset <= 64; offset += 8) {
    composeVerticalTransition(
        animationBuffers[2], oldFrameBuffers[2], frameBuffers[2],
        oled1Direction * offset,
        -oled1Direction * (64 - offset));
    composeVerticalTransition(
        animationBuffers[1], oldFrameBuffers[1], frameBuffers[1],
        oled2Direction * offset,
        -oled2Direction * (64 - offset));
    memcpy(animationBuffers[0], oldFrameBuffers[0], FRAME_SIZE);
    parallelSendFrames(animationBuffers);
    delay(6);
  }
}

void setInputLanguage(const String &value);

class ServerCallbacks : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    NimBLEDevice::getAdvertising()->start();
  }
};

class LanguageCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    setInputLanguage(characteristic->getValue().c_str());
  }
};

void drawDisplay(U8G2 &display, const char *title, const char *text) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB14_tr);
  display.drawStr(4, 25, title);
  display.setFont(u8g2_font_6x12_tr);
  display.drawStr(4, 48, text);
}

void drawLanguage(U8G2 &display, const char *label, const String &language) {
  display.clearBuffer();
  display.setFont(u8g2_font_fub42_tr);
  const int16_t width = display.getStrWidth(language.c_str());
  const int16_t x = (128 - width) / 2;
  // Language codes contain uppercase letters only, so centering against the
  // font descent would shift them upward. Center the uppercase body itself.
  const int16_t y = (64 + display.getAscent()) / 2;
  display.drawStr(x, y, language.c_str());
}

void prepareLanguageFrame(U8G2 &display, uint8_t channel, const char *label,
                          const String &language) {
  drawLanguage(display, label, language);
  memcpy(frameBuffers[channel], display.getBufferPtr(), FRAME_SIZE);
}

String remainingLanguage() {
  if (previousLanguage != inputLanguage && previousLanguage != toggleLanguage) {
    return previousLanguage;
  }
  if (nextLanguage != inputLanguage && nextLanguage != toggleLanguage) {
    return nextLanguage;
  }
  return nextLanguage;
}

void prepareAllLanguageFrames() {
  if (displayMode == "TOGGLE" && toggleLanguage != "--" &&
      toggleLanguage != inputLanguage) {
    // Physical displays 1 and 2 exchange the two last-used languages.
    // Physical display 3 keeps the third language and remains still.
    prepareLanguageFrame(oled1, 0, "NEXT", remainingLanguage());
    prepareLanguageFrame(oled2, 1, "CURRENT", inputLanguage);
    prepareLanguageFrame(oled3, 2, "PREVIOUS", toggleLanguage);
  } else {
    prepareLanguageFrame(oled1, 0, "NEXT", nextLanguage);
    prepareLanguageFrame(oled2, 1, "CURRENT", inputLanguage);
    prepareLanguageFrame(oled3, 2, "PREVIOUS", previousLanguage);
  }
}

void drawAllLanguages() {
  prepareAllLanguageFrames();
  parallelSendFrames(frameBuffers);
}

void setup() {
  Serial.begin(115200);

  oled1.begin();
  oled2.begin();
  oled3.begin();

  for (uint8_t i = 0; i < CHANNELS; ++i) {
    pinMode(SDA_PINS[i], OUTPUT_OPEN_DRAIN);
    pinMode(SCL_PINS[i], OUTPUT_OPEN_DRAIN);
  }
  parallelSetSda(0x07);
  parallelSetScl(false);

  // Speed up full-frame I2C transfers: the standard 100 kHz bus creates a
  // noticeable delay when all three OLEDs are updated in sequence.
  oled1.setBusClock(400000);
  oled2.setBusClock(400000);
  oled3.setBusClock(400000);

  NimBLEDevice::init("Language Display");
  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  NimBLEService *service = server->createService(BLE_SERVICE_UUID);
  NimBLECharacteristic *language = service->createCharacteristic(
      BLE_LANGUAGE_UUID, NIMBLE_PROPERTY::WRITE);
  language->setCallbacks(new LanguageCallbacks());
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->start();

  prepareAllLanguageFrames();
  Serial.printf("FRAME P=%s C=%s N=%s\\n", previousLanguage.c_str(), inputLanguage.c_str(), nextLanguage.c_str());
  parallelSendFrames(frameBuffers);
}

void setInputLanguage(const String &value) {
  String message = value;
  message.trim();

  const int firstSeparator = message.indexOf('|');
  const int secondSeparator = message.indexOf('|', firstSeparator + 1);
  const int thirdSeparator = message.indexOf('|', secondSeparator + 1);
  if (firstSeparator <= 0 || secondSeparator <= firstSeparator ||
      thirdSeparator <= secondSeparator || message.length() >= 64) return;

  const String oldCurrentLanguage = inputLanguage;
  memcpy(oldFrameBuffers, frameBuffers, sizeof(oldFrameBuffers));

  previousLanguage = message.substring(0, firstSeparator);
  inputLanguage = message.substring(firstSeparator + 1, secondSeparator);
  nextLanguage = message.substring(secondSeparator + 1, thirdSeparator);
  const int fourthSeparator = message.indexOf('|', thirdSeparator + 1);
  const int fifthSeparator = fourthSeparator >= 0
      ? message.indexOf('|', fourthSeparator + 1)
      : -1;
  String direction = fourthSeparator >= 0
      ? message.substring(thirdSeparator + 1, fourthSeparator)
      : message.substring(thirdSeparator + 1);
  displayMode = (fourthSeparator >= 0 && fifthSeparator > fourthSeparator)
      ? message.substring(fourthSeparator + 1, fifthSeparator)
      : "CAROUSEL";
  toggleLanguage = fifthSeparator >= 0
      ? message.substring(fifthSeparator + 1)
      : "--";
  direction.trim();
  displayMode.trim();
  toggleLanguage.trim();
  if (previousLanguage.length() == 0 || inputLanguage.length() == 0 || nextLanguage.length() == 0) return;
  prepareAllLanguageFrames();
  if (displayMode == "TOGGLE" && oldCurrentLanguage != "--") {
    // Physical display 3 (channel 0) is frozen throughout toggle mode.
    memcpy(frameBuffers[0], oldFrameBuffers[0], FRAME_SIZE);
  }
  Serial.printf("FRAME P=%s C=%s N=%s MODE=%s TOGGLE=%s\\n",
                previousLanguage.c_str(), inputLanguage.c_str(), nextLanguage.c_str(),
                displayMode.c_str(), toggleLanguage.c_str());
  if (oldCurrentLanguage != "--" && oldCurrentLanguage != inputLanguage &&
      displayMode == "TOGGLE") {
    animateToggleExchange();
  } else if (oldCurrentLanguage != "--" && oldCurrentLanguage != inputLanguage && direction == "LEFT") {
#if DEVICE_UPSIDE_DOWN
    animateFrames(false);
#else
    animateFrames(true);
#endif
  } else if (oldCurrentLanguage != "--" && oldCurrentLanguage != inputLanguage && direction == "RIGHT") {
#if DEVICE_UPSIDE_DOWN
    animateFrames(true);
#else
    animateFrames(false);
#endif
  } else {
    parallelSendFrames(frameBuffers);
  }
}

void loop() {
  if (Serial.available()) {
    String received = Serial.readStringUntil('\n');
    received.trim();

    if (received.length() > 0 && received.length() < 64) {
      setInputLanguage(received);
    }
  }

}
