#include "debug_core.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace debugcar {
namespace {
int magnitude(int value) { return value < 0 ? -value : value; }
int sign(int value) { return (value > 0) - (value < 0); }

bool number(const char* text, int minimum, int maximum, int& value) {
  if (!text || !*text) return false;
  char* end = nullptr;
  errno = 0;
  const long parsed = strtol(text, &end, 10);
  if (errno || *end || parsed < minimum || parsed > maximum) return false;
  value = static_cast<int>(parsed);
  return true;
}

int wheelIndex(const char* text, bool allowAll = false) {
  const char* names[] = {"fl", "fr", "rl", "rr"};
  for (int i = 0; i < kWheelCount; ++i) {
    if (strcmp(text, names[i]) == 0) return i;
  }
  return allowAll && strcmp(text, "all") == 0 ? 4 : -1;
}
}  // namespace

Wheels normalize(Wheels values, int limit) {
  if (limit < 0) limit = 0;
  if (limit > kMaxPwm) limit = kMaxPwm;
  int peak = 0;
  for (int value : values.pwm) {
    if (magnitude(value) > peak) peak = magnitude(value);
  }
  if (peak > limit) {
    for (int& value : values.pwm) value = value * limit / peak;
  }
  return values;
}

Wheels mecanum(int vx, int vy, int wz, int limit) {
  Wheels result;
  result.pwm[0] = vx - vy - wz;
  result.pwm[1] = vx + vy + wz;
  result.pwm[2] = vx + vy - wz;
  result.pwm[3] = vx - vy + wz;
  return normalize(result, limit);
}

Command parseCommand(char* line) {
  Command result;
  char* tokens[8] = {};
  int count = 0;
  char* cursor = line;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (!*cursor) break;
    if (count == 8) {
      result.error = "Too many arguments.";
      return result;
    }
    tokens[count++] = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') {
      if (*cursor >= 'A' && *cursor <= 'Z') *cursor += 'a' - 'A';
      ++cursor;
    }
    if (*cursor) *cursor++ = '\0';
  }
  if (!count) {
    result.error = "Empty command.";
    return result;
  }
  const char* name = tokens[0];
  if (count == 1) {
    if (!strcmp(name, "help") || !strcmp(name, "h") || !strcmp(name, "?"))
      result.type = CommandType::Help;
    else if (!strcmp(name, "status")) result.type = CommandType::Status;
    else if (!strcmp(name, "stop") || !strcmp(name, "x")) result.type = CommandType::Stop;
    else if (!strcmp(name, "zero")) result.type = CommandType::Zero;
    else if (!strcmp(name, "save")) result.type = CommandType::Save;
    else if (!strcmp(name, "load")) result.type = CommandType::Load;
    else if (!strcmp(name, "defaults")) result.type = CommandType::Defaults;
    if (result.type != CommandType::Invalid) return result;
  }
  result.error = "Invalid syntax or value; type help. Motion stopped.";
  int duration = static_cast<int>(kDefaultPulseMs);
  if ((!strcmp(name, "m") || !strcmp(name, "motor")) && (count == 3 || count == 4)) {
    result.wheel = wheelIndex(tokens[1]);
    if (result.wheel < 0 || !number(tokens[2], -255, 255, result.value) ||
        (count == 4 && !number(tokens[3], 1, kMaxPulseMs, duration))) return result;
    result.wheels.pwm[result.wheel] = result.value;
    result.type = CommandType::Motor;
  } else if (!strcmp(name, "all") && (count == 5 || count == 6)) {
    for (int i = 0; i < kWheelCount; ++i) {
      if (!number(tokens[i + 1], -255, 255, result.wheels.pwm[i])) return result;
    }
    if (count == 6 && !number(tokens[5], 1, kMaxPulseMs, duration)) return result;
    result.type = CommandType::All;
  } else if (!strcmp(name, "drive") && (count == 4 || count == 5)) {
    int vx, vy, wz;
    if (!number(tokens[1], -255, 255, vx) || !number(tokens[2], -255, 255, vy) ||
        !number(tokens[3], -255, 255, wz) ||
        (count == 5 && !number(tokens[4], 1, kMaxPulseMs, duration))) return result;
    // Apply the actual runtime limit in main, once, to preserve wheel ratios.
    result.wheels.pwm[0] = vx - vy - wz;
    result.wheels.pwm[1] = vx + vy + wz;
    result.wheels.pwm[2] = vx + vy - wz;
    result.wheels.pwm[3] = vx - vy + wz;
    result.type = CommandType::Drive;
  } else if (!strcmp(name, "move") && (count == 3 || count == 4)) {
    int speed;
    if (!number(tokens[2], 0, 255, speed) ||
        (count == 4 && !number(tokens[3], 1, kMaxPulseMs, duration))) return result;
    int vx = 0, vy = 0, wz = 0;
    if (!strcmp(tokens[1], "fwd")) vx = speed;
    else if (!strcmp(tokens[1], "back")) vx = -speed;
    else if (!strcmp(tokens[1], "left")) vy = speed;
    else if (!strcmp(tokens[1], "right")) vy = -speed;
    else if (!strcmp(tokens[1], "ccw")) wz = speed;
    else if (!strcmp(tokens[1], "cw")) wz = -speed;
    else return result;
    result.wheels = mecanum(vx, vy, wz, 255);
    result.type = CommandType::Drive;
  } else if (!strcmp(name, "scan") && count <= 3) {
    result.value = 60;
    duration = 800;
    if ((count >= 2 && !number(tokens[1], -255, 255, result.value)) ||
        result.value == 0 ||
        (count == 3 && !number(tokens[2], 1, kMaxPulseMs, duration))) return result;
    result.type = CommandType::Scan;
  } else if (!strcmp(name, "invert") && count == 3) {
    result.wheel = wheelIndex(tokens[1]);
    if (result.wheel < 0 || !number(tokens[2], 0, 1, result.value)) return result;
    result.enabled = result.value != 0;
    result.type = CommandType::Invert;
  } else if (!strcmp(name, "limit") && count == 2) {
    if (!number(tokens[1], 1, 255, result.value)) return result;
    result.type = CommandType::Limit;
  } else if (!strcmp(name, "ppr") && count == 3) {
    result.wheel = wheelIndex(tokens[1], true);
    if (result.wheel < 0 || !number(tokens[2], 0, 1000000, result.value)) return result;
    result.type = CommandType::Ppr;
  } else if (!strcmp(name, "telemetry") && count == 2) {
    if (strcmp(tokens[1], "on") && strcmp(tokens[1], "off")) return result;
    result.enabled = !strcmp(tokens[1], "on");
    result.type = CommandType::Telemetry;
  }
  result.durationMs = static_cast<uint32_t>(duration);
  return result;
}

void TimedMotion::pulse(Wheels values, uint32_t durationMs, uint32_t now) {
  stop();
  if (durationMs == 0 || durationMs > kMaxPulseMs) return;
  target_ = values;
  mode_ = Mode::Pulse;
  startedMs_ = now;
  durationMs_ = durationMs;
}

void TimedMotion::scan(int pwm, uint32_t durationMs, uint32_t now) {
  stop();
  if (durationMs == 0 || durationMs > kMaxPulseMs || pwm == 0 ||
      pwm < -kMaxPwm || pwm > kMaxPwm) return;
  mode_ = Mode::Scan;
  startedMs_ = now;
  durationMs_ = durationMs;
  scanPwm_ = pwm;
  target_.pwm[0] = pwm;
}

void TimedMotion::stop() {
  target_ = Wheels{};
  mode_ = Mode::Idle;
  scanWheel_ = 0;
  inGap_ = false;
}

void TimedMotion::update(uint32_t now) {
  if (mode_ == Mode::Idle) return;
  const uint32_t elapsed = now - startedMs_;
  if (mode_ == Mode::Pulse) {
    if (elapsed >= durationMs_) stop();
    return;
  }
  if (!inGap_ && elapsed >= durationMs_) {
    target_ = Wheels{};
    if (scanWheel_ == kWheelCount - 1) { stop(); return; }
    inGap_ = true;
    startedMs_ = now;
  } else if (inGap_ && elapsed >= kScanGapMs) {
    ++scanWheel_;
    inGap_ = false;
    startedMs_ = now;
    target_.pwm[scanWheel_] = scanPwm_;
  }
}

uint32_t TimedMotion::remainingMs(uint32_t now) const {
  if (mode_ == Mode::Idle) return 0;
  const uint32_t duration = inGap_ ? kScanGapMs : durationMs_;
  const uint32_t elapsed = now - startedMs_;
  return elapsed >= duration ? 0 : duration - elapsed;
}

void MotorRamp::cut(uint32_t now) {
  if (value_ != 0) {
    lastDirection_ = sign(value_);
    stoppedMs_ = now;
  }
  value_ = 0;
  lastStepMs_ = now;
}

int MotorRamp::update(int target, uint32_t now) {
  if (target > 255) target = 255;
  if (target < -255) target = -255;
  if (target == 0) { cut(now); return 0; }
  if (value_ != 0 && sign(value_) != sign(target)) cut(now);
  if (value_ == 0 && lastDirection_ != 0 && sign(target) != lastDirection_ &&
      static_cast<uint32_t>(now - stoppedMs_) < kReversePauseMs) {
    lastStepMs_ = now;
    return 0;
  }
  const uint32_t ticks = (now - lastStepMs_) / kRampIntervalMs;
  if (!ticks) return value_;
  lastStepMs_ += ticks * kRampIntervalMs;
  const int step = static_cast<int>(ticks > 64 ? 64 : ticks) * kRampStep;
  if (target > value_) value_ += target - value_ < step ? target - value_ : step;
  else value_ -= value_ - target < step ? value_ - target : step;
  if (value_ != 0) lastDirection_ = sign(value_);
  return value_;
}
}  // namespace debugcar
