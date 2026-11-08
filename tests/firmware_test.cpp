#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <vector>
#include "board_config.h"

void setup();
void loop();
FakeSerial Serial;
std::vector<uint8_t> fakeStoredConfig;
namespace {
uint32_t clockMs = 0;
int pinsToChannels[40];
int pwmByPin[40] = {};
int pinLevels[40] = {};
void (*callbacks[40])(void*) = {};
void* callbackArguments[40] = {};
int checks = 0;
void check(bool condition, const char* message) {
  ++checks;
  if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
void verifyOutputs() {
  for (const auto& wheel : board::kWheels)
    check(!(pwmByPin[wheel.ina] && pwmByPin[wheel.inb]), "H bridge never drives both inputs high");
}
void writeByPin(uint8_t pin, uint32_t duty) {
  check(duty <= 255, "hardware PWM in 8-bit range");
  pwmByPin[pin] = static_cast<int>(duty);
  verifyOutputs();
}
void send(const std::string& command) {
  for (char ch : command) Serial.input.push_back(ch);
  while (Serial.available()) loop();
}
void runFor(uint32_t milliseconds) {
  const uint32_t start = clockMs;
  while (static_cast<uint32_t>(clockMs - start) < milliseconds) loop();
}
void stopped(const char* message) {
  for (const auto& wheel : board::kWheels)
    check(pwmByPin[wheel.ina] == 0 && pwmByPin[wheel.inb] == 0, message);
}
void pulse(uint8_t pin, int count) {
  check(callbacks[pin] != nullptr, "encoder interrupt installed");
  while (count--) callbacks[pin](callbackArguments[pin]);
}
}  // namespace

uint32_t millis() { return clockMs; }
void delay(uint32_t ms) { clockMs += ms; }
void pinMode(uint8_t, int) {}
void digitalWrite(uint8_t pin, int level) { pinLevels[pin] = level; }
int digitalRead(uint8_t pin) { return pinLevels[pin]; }
void attachInterruptArg(uint8_t pin, void (*callback)(void*), void* arg, int mode) {
  check(mode == RISING, "single-channel encoder uses rising edges");
  callbacks[pin] = callback;
  callbackArguments[pin] = arg;
}
double ledcSetup(uint8_t, double frequency, uint8_t bits) { return bits == 8 ? frequency : 0; }
void ledcAttachPin(uint8_t pin, uint8_t channel) { pinsToChannels[pin] = channel; }
void ledcDetachPin(uint8_t pin) { pinsToChannels[pin] = -1; pwmByPin[pin] = 0; }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
bool ledcWrite(uint8_t pin, uint32_t value) {
  if (pinsToChannels[pin] < 0) return false;
  writeByPin(pin, value);
  return true;
}
#else
void ledcWrite(uint8_t channel, uint32_t value) {
  for (int pin = 0; pin < 40; ++pin)
    if (pinsToChannels[pin] == channel) writeByPin(pin, value);
}
#endif
bool ledcAttachChannel(uint8_t pin, uint32_t, uint8_t bits, int8_t channel) {
  if (bits != 8) return false;
  pinsToChannels[pin] = channel;
  return true;
}
bool ledcDetach(uint8_t pin) { ledcDetachPin(pin); return true; }

int main() {
  for (int& channel : pinsToChannels) channel = -1;
  setup();
  stopped("boot leaves every motor stopped");
  check(pinsToChannels[13] == 0 && pinsToChannels[15] == 1 &&
        pinsToChannels[12] == 2 && pinsToChannels[14] == 3 &&
        pinsToChannels[27] == 4 && pinsToChannels[26] == 5 &&
        pinsToChannels[25] == 6 && pinsToChannels[33] == 7, "actual GPIO mapping matches wiring");
  send("telemetry off\r\n");
  send("m fl 60 1000\n");
  runFor(200);
  check(pwmByPin[13] == 60 && pwmByPin[15] == 0 && pwmByPin[12] == 0 &&
        pwmByPin[27] == 0 && pwmByPin[25] == 0, "single-wheel command energizes only FL");
  send("status\n");
  runFor(850);
  stopped("status does not extend pulse timeout");

  send("m fl 80 1000\n");
  runFor(220);
  send("!");
  stopped("emergency character stops without newline");
  send("m fr 80\n");
  runFor(200);
  stopped("remainder of emergency line cannot restart motor");
  send("m fr 60 100\n");
  runFor(50);
  check(pwmByPin[12] > 0, "next complete command accepted");
  runFor(60);
  stopped("short test expires");

  send("m fl 80 1000\n");
  runFor(220);
  send("m fl -80 1000\n");
  stopped("direction reversal cuts first");
  runFor(78);
  stopped("direction reversal waits");
  runFor(60);
  check(pwmByPin[13] == 0 && pwmByPin[15] > 0, "reverse uses INB PWM");
  send("m fl bad\n");
  stopped("invalid command cuts output");
  send("m fl 60\n");
  runFor(300);
  send(std::string(150, 'a'));
  stopped("overlong line stops before newline");
  send("m rr 100\n");
  runFor(100);
  stopped("overlong line suffix is discarded");
  send("m fl 60\n");
  runFor(200);
  send(std::string(1, '\0'));
  stopped("NUL injection stops output");
  send("\n");

  send("invert fl 1\nsave\ndefaults\nload\nm fl 60\n");
  runFor(300);
  check(pwmByPin[15] == 60 && pwmByPin[13] == 0, "saved inversion loads and changes real output");
  send("defaults\nlimit 40\nall 80 40 -80 -40 1000\n");
  runFor(350);
  check(pwmByPin[13] == 40 && pwmByPin[12] == 20 && pwmByPin[26] == 40 &&
        pwmByPin[33] == 20, "runtime limit preserves four-wheel PWM ratios");
  send("x\nzero\nppr all 20\n");
  pulse(32, 7);
  pulse(34, 8);
  pulse(35, 9);
  pulse(36, 10);
  Serial.output.clear();
  runFor(250);
  send("status\n");
  check(Serial.output.find("FL target=   0 out=   0 count=7") != std::string::npos,
        "FL encoder input counts independently");
  check(Serial.output.find("FR target=   0 out=   0 count=8") != std::string::npos,
        "FR encoder input counts independently");
  check(Serial.output.find("RL target=   0 out=   0 count=9") != std::string::npos,
        "RL encoder input counts independently");
  check(Serial.output.find("RR target=   0 out=   0 count=10") != std::string::npos,
        "RR encoder input counts independently");
  check(Serial.output.find("rpm=84.0") != std::string::npos, "RPM uses configured rising pulses per wheel rev");
  send("scan 40 100\n");
  runFor(50);
  send("!\n");
  runFor(3000);
  stopped("cancelled scan never restarts later");
  printf("PASS: firmware integration, Arduino API %d (%d checks)\n", ESP_ARDUINO_VERSION_MAJOR, checks);
  return 0;
}
