#pragma once

#include <stdint.h>

namespace board {
// Order: front left, front right, rear left, rear right.
// These are the GPIO labels printed beside the motor sockets, not output pins
// to connect directly to a bare motor. The board contains SA8345 H bridges.
struct WheelPins {
  const char* name;
  uint8_t ina;
  uint8_t inb;
  uint8_t encoder;
};

constexpr WheelPins kWheels[] = {
    {"FL", 13, 15, 32},
    {"FR", 12, 14, 34},
    {"RL", 27, 26, 35},
    {"RR", 25, 33, 36},
};

// SA8345 PWM mode A: PWM on one input, the other LOW. Both LOW = coast.
// Start with 1 kHz; mode A can have starting problems at high frequencies.
constexpr uint32_t kPwmHz = 1000;
constexpr uint8_t kPwmBits = 8;
constexpr int kStartupLimit = 100;  // Out of 255. Does not persist across boots.
constexpr uint32_t kTelemetryMs = 500;
constexpr uint32_t kEncoderSampleMs = 250;
constexpr uint32_t kBaudRate = 115200;
}  // namespace board

