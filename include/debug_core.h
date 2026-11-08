#pragma once

#include <stdint.h>

// Pure C++ logic, shared by the firmware and the host tests.
namespace debugcar {
constexpr int kWheelCount = 4;
constexpr int kMaxPwm = 255;
constexpr uint32_t kDefaultPulseMs = 1000;
constexpr uint32_t kMaxPulseMs = 5000;
constexpr uint32_t kScanGapMs = 400;
constexpr uint32_t kReversePauseMs = 80;
constexpr uint32_t kRampIntervalMs = 10;
constexpr int kRampStep = 4;
constexpr unsigned kLineCapacity = 128;

struct Wheels {
  int pwm[kWheelCount] = {0, 0, 0, 0};
};

Wheels normalize(Wheels values, int limit);
// vx: forward; vy: left; wz: counterclockwise. Dimensionless mixing inputs.
Wheels mecanum(int vx, int vy, int wz, int limit);

enum class CommandType {
  Invalid, Help, Status, Stop, Motor, All, Drive, Scan,
  Invert, Limit, Ppr, Telemetry, Zero, Save, Load, Defaults
};

struct Command {
  CommandType type = CommandType::Invalid;
  Wheels wheels;
  int wheel = -1;  // 0..3, or 4 for all (ppr only).
  int value = 0;
  bool enabled = false;
  uint32_t durationMs = kDefaultPulseMs;
  const char* error = "Unknown command; type help.";
};

// Splits a mutable, NUL-terminated line. Rejects extra tokens and bad numbers.
Command parseCommand(char* line);

class TimedMotion {
 public:
  enum class Mode { Idle, Pulse, Scan };
  void pulse(Wheels values, uint32_t durationMs, uint32_t now);
  void scan(int pwm, uint32_t durationMs, uint32_t now);
  void stop();
  void update(uint32_t now);
  const Wheels& target() const { return target_; }
  Mode mode() const { return mode_; }
  int scanWheel() const { return scanWheel_; }
  bool inGap() const { return inGap_; }
  uint32_t remainingMs(uint32_t now) const;

 private:
  Wheels target_;
  Mode mode_ = Mode::Idle;
  uint32_t startedMs_ = 0;
  uint32_t durationMs_ = 0;
  int scanWheel_ = 0;
  int scanPwm_ = 0;
  bool inGap_ = false;
};

class MotorRamp {
 public:
  int update(int target, uint32_t now);
  void cut(uint32_t now);
  int value() const { return value_; }

 private:
  int value_ = 0;
  int lastDirection_ = 0;
  uint32_t stoppedMs_ = 0;
  uint32_t lastStepMs_ = 0;
};
}  // namespace debugcar

