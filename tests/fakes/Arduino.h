#pragma once
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include "esp_arduino_version.h"

#define IRAM_ATTR
#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1
#define RISING 1
#define CONFIG_IDF_TARGET_ESP32 1
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
#define portENTER_CRITICAL_ISR(x) (void)(x)
#define portEXIT_CRITICAL_ISR(x) (void)(x)

struct FakeSerial {
  std::deque<char> input;
  std::string output;
  void begin(uint32_t) {}
  int available() const { return static_cast<int>(input.size()); }
  int read() { const char ch = input.front(); input.pop_front(); return static_cast<unsigned char>(ch); }
  void print(const char* text) { output += text; }
  void println(const char* text = "") { output += text; output += '\n'; }
  void printf(const char* format, ...) {
    char buffer[4096];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    output += buffer;
  }
};
extern FakeSerial Serial;
uint32_t millis();
void delay(uint32_t ms);
void pinMode(uint8_t pin, int mode);
void digitalWrite(uint8_t pin, int level);
int digitalRead(uint8_t pin);
void attachInterruptArg(uint8_t pin, void (*callback)(void*), void* arg, int mode);
double ledcSetup(uint8_t channel, double frequency, uint8_t bits);
void ledcAttachPin(uint8_t pin, uint8_t channel);
void ledcDetachPin(uint8_t pin);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
bool ledcWrite(uint8_t pin, uint32_t value);
#else
void ledcWrite(uint8_t channel, uint32_t value);
#endif
bool ledcAttachChannel(uint8_t pin, uint32_t frequency, uint8_t bits, int8_t channel);
bool ledcDetach(uint8_t pin);

