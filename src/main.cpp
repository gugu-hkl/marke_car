#include <Arduino.h>
#include <Preferences.h>
#include <esp_arduino_version.h>

#include "board_config.h"
#include "debug_core.h"

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "This wiring is for the classic ESP32. Select ESP32 Dev Module, not S3/C3."
#endif

using debugcar::Command;
using debugcar::CommandType;
using debugcar::TimedMotion;
using debugcar::Wheels;

namespace {
struct SavedConfig {
  uint32_t version = 1;
  uint32_t inversionMask = 0;
  uint32_t ppr[4] = {0, 0, 0, 0};
};

SavedConfig config;
Preferences preferences;
bool storageReady = false;
bool hardwareReady = false;
bool telemetry = true;
int pwmLimit = board::kStartupLimit;
TimedMotion motion;
debugcar::MotorRamp ramps[4];
int writtenPwm[4] = {0, 0, 0, 0};
bool pwmAttached[8] = {};

portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t encoderCounts[4] = {};
uint8_t encoderIndices[4] = {0, 1, 2, 3};
uint32_t totals[4] = {};
uint32_t previousTotals[4] = {};
float pulsesPerSecond[4] = {};
uint32_t lastEncoderSampleMs = 0;
uint32_t lastTelemetryMs = 0;

char line[debugcar::kLineCapacity] = {};
unsigned lineLength = 0;
bool discardLine = false;

void IRAM_ATTR onEncoder(void* argument) {
  const uint8_t index = *static_cast<uint8_t*>(argument);
  portENTER_CRITICAL_ISR(&encoderMux);
  ++encoderCounts[index];
  portEXIT_CRITICAL_ISR(&encoderMux);
}

void snapshotEncoders(uint32_t* destination) {
  portENTER_CRITICAL(&encoderMux);
  for (int i = 0; i < 4; ++i) destination[i] = encoderCounts[i];
  portEXIT_CRITICAL(&encoderMux);
}

bool attachPwm(uint8_t channel, uint8_t pin) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttachChannel(pin, board::kPwmHz, board::kPwmBits, channel);
#else
  if (ledcSetup(channel, board::kPwmHz, board::kPwmBits) == 0) return false;
  ledcAttachPin(pin, channel);
  return true;
#endif
}

bool duty(uint8_t channel, uint8_t pin, uint32_t value) {
  if (!pwmAttached[channel]) return false;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  return ledcWrite(pin, value);
#else
  (void)pin;
  ledcWrite(channel, value);
  return true;
#endif
}

void detachPwm(uint8_t pin) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(pin);
#else
  ledcDetachPin(pin);
#endif
}

// Signed ELECTRICAL PWM, after applying the per-wheel inversion setting.
bool writeMotor(int index, int value) {
  const auto& pins = board::kWheels[index];
  const uint8_t a = index * 2;
  const uint8_t b = a + 1;
  bool success;
  if (value > 0) {
    success = duty(b, pins.inb, 0);
    success = duty(a, pins.ina, value) && success;
  } else {
    success = duty(a, pins.ina, 0);
    success = duty(b, pins.inb, value < 0 ? -value : 0) && success;
  }
  writtenPwm[index] = value;
  return success;
}

void stopAll(uint32_t now) {
  motion.stop();
  for (int i = 0; i < 4; ++i) {
    ramps[i].cut(now);
    if (hardwareReady) writeMotor(i, 0);
  }
}

void hardwareFault() {
  motion.stop();
  hardwareReady = false;
  for (int i = 0; i < 4; ++i) {
    const auto& pins = board::kWheels[i];
    ramps[i].cut(millis());
    if (pwmAttached[i * 2]) detachPwm(pins.ina);
    if (pwmAttached[i * 2 + 1]) detachPwm(pins.inb);
    pinMode(pins.ina, OUTPUT);
    pinMode(pins.inb, OUTPUT);
    digitalWrite(pins.ina, LOW);
    digitalWrite(pins.inb, LOW);
    writtenPwm[i] = 0;
  }
  memset(pwmAttached, 0, sizeof(pwmAttached));
  Serial.println("FAULT: PWM initialization/write failed. Outputs LOW; reset board.");
}

bool beginHardware() {
  // Establish all eight LOW levels before attaching any PWM output.
  for (const auto& pins : board::kWheels) {
    pinMode(pins.ina, OUTPUT);
    pinMode(pins.inb, OUTPUT);
    digitalWrite(pins.ina, LOW);
    digitalWrite(pins.inb, LOW);
  }
  for (int i = 0; i < 4; ++i) {
    const auto& pins = board::kWheels[i];
    pwmAttached[i * 2] = attachPwm(i * 2, pins.ina);
    pwmAttached[i * 2 + 1] = attachPwm(i * 2 + 1, pins.inb);
    if (!pwmAttached[i * 2] || !pwmAttached[i * 2 + 1]) return false;
    if (!writeMotor(i, 0)) return false;
  }
  return true;
}

void serviceMotion(uint32_t now) {
  const auto oldMode = motion.mode();
  const int oldWheel = motion.scanWheel();
  const bool oldGap = motion.inGap();
  motion.update(now);
  if (hardwareReady) {
    for (int i = 0; i < 4; ++i) {
      const int logical = motion.target().pwm[i];
      const int electrical = config.inversionMask & (1u << i) ? -logical : logical;
      const int next = ramps[i].update(electrical, now);
      if (next != writtenPwm[i] && !writeMotor(i, next)) {
        hardwareFault();
        break;
      }
    }
  }
  // Report after applying the stop/output change, so printing cannot delay it.
  if (oldMode != TimedMotion::Mode::Idle && motion.mode() == TimedMotion::Mode::Idle)
    Serial.println("DONE: outputs stopped.");
  else if (motion.mode() == TimedMotion::Mode::Scan &&
           (oldWheel != motion.scanWheel() || oldGap != motion.inGap())) {
    if (motion.inGap()) Serial.println("SCAN: pause.");
    else Serial.printf("SCAN: %s\n", board::kWheels[motion.scanWheel()].name);
  }
}

void sampleEncoders(uint32_t now) {
  const uint32_t elapsed = now - lastEncoderSampleMs;
  if (elapsed < board::kEncoderSampleMs) return;
  snapshotEncoders(totals);
  for (int i = 0; i < 4; ++i) {
    const uint32_t delta = totals[i] - previousTotals[i];
    pulsesPerSecond[i] = static_cast<float>(delta) * 1000.0f / elapsed;
    previousTotals[i] = totals[i];
  }
  lastEncoderSampleMs = now;
}

void zeroEncoders(uint32_t now) {
  portENTER_CRITICAL(&encoderMux);
  for (auto& count : encoderCounts) count = 0;
  portEXIT_CRITICAL(&encoderMux);
  memset(totals, 0, sizeof(totals));
  memset(previousTotals, 0, sizeof(previousTotals));
  memset(pulsesPerSecond, 0, sizeof(pulsesPerSecond));
  lastEncoderSampleMs = now;
}

bool loadConfig() {
  config = SavedConfig{};
  if (!storageReady || preferences.getBytesLength("calibration") != sizeof(SavedConfig))
    return false;
  SavedConfig stored;
  if (preferences.getBytes("calibration", &stored, sizeof(stored)) != sizeof(stored) ||
      stored.version != 1 || stored.inversionMask > 15) return false;
  for (uint32_t ppr : stored.ppr) if (ppr > 1000000) return false;
  config = stored;
  return true;
}

const char* modeName() {
  if (motion.mode() == TimedMotion::Mode::Scan) return motion.inGap() ? "scan-gap" : "scan";
  return motion.mode() == TimedMotion::Mode::Pulse ? "pulse" : "idle";
}

void printStatus(uint32_t now, bool full) {
  uint32_t current[4];
  snapshotEncoders(current);
  Serial.printf("STATE mode=%s remaining=%lums limit=%d hw=%s\n", modeName(),
                static_cast<unsigned long>(motion.remainingMs(now)), pwmLimit,
                hardwareReady ? "OK" : "FAULT");
  for (int i = 0; i < 4; ++i) {
    Serial.printf("%s target=%4d out=%4d count=%lu pps=%.1f rpm=",
                  board::kWheels[i].name, motion.target().pwm[i], writtenPwm[i],
                  static_cast<unsigned long>(current[i]), pulsesPerSecond[i]);
    if (config.ppr[i]) Serial.printf("%.1f", pulsesPerSecond[i] * 60.0f / config.ppr[i]);
    else Serial.print("--");
    if (full) {
      const auto& pins = board::kWheels[i];
      Serial.printf(" INA=%u INB=%u ENC=%u level=%d inv=%u ppr=%lu", pins.ina,
                    pins.inb, pins.encoder, digitalRead(pins.encoder),
                    (config.inversionMask >> i) & 1u,
                    static_cast<unsigned long>(config.ppr[i]));
    }
    Serial.println();
  }
}

void printHelp() {
  Serial.println(
      "\nMecanum debug | ESP32 / SA8345 | 115200 baud, newline=LF or CRLF\n"
      "Order: FL FR RL RR. PWM: -255..255; default limit=100.\n"
      "!                         immediate stop (no newline needed); then newline\n"
      "stop | x                  stop all motors\n"
      "m fl 60 [ms]              one wheel only; fl/fr/rl/rr\n"
      "all 60 60 60 60 [ms]      independent FL FR RL RR PWM\n"
      "move fwd 60 [ms]          fwd/back/left/right/ccw/cw (X-layout)\n"
      "drive 60 0 0 [ms]         vx=forward, vy=left, wz=CCW; mixed PWM units\n"
      "scan [pwm] [ms]           FL -> FR -> RL -> RR; defaults 60, 800ms\n"
      "invert fl 0|1             set wheel polarity; stops motion\n"
      "limit 100                 max PWM 1..255; stops motion; not saved\n"
      "ppr all 0                 rising pulses per WHEEL revolution; 0=unknown\n"
      "ppr fl 20                 can set each wheel separately\n"
      "status                    pins, outputs, counts, unsigned speed\n"
      "telemetry on|off           periodic counts/speed, 500ms\n"
      "zero                      stop and clear all encoder counters\n"
      "save | load | defaults    stop; save/load/reset direction and PPR\n"
      "help                      stop and show this help\n"
      "ms: 1..5000, default 1000 (includes ramp time). No continuous mode.\n"
      "One encoder signal counts RISING edges; it cannot measure direction.\n");
}

void execute(Command command, uint32_t now) {
  switch (command.type) {
    case CommandType::Motor:
    case CommandType::All:
    case CommandType::Drive: {
      if (!hardwareReady) { Serial.println("ERR: hardware fault; reset required."); break; }
      const Wheels target = debugcar::normalize(command.wheels, pwmLimit);
      motion.pulse(target, command.durationMs, now);
      serviceMotion(now);
      Serial.printf("RUN: FL=%d FR=%d RL=%d RR=%d for %lums\n", target.pwm[0],
                    target.pwm[1], target.pwm[2], target.pwm[3],
                    static_cast<unsigned long>(command.durationMs));
      break;
    }
    case CommandType::Scan: {
      if (!hardwareReady) { Serial.println("ERR: hardware fault; reset required."); break; }
      Wheels single;
      single.pwm[0] = command.value;
      const int speed = debugcar::normalize(single, pwmLimit).pwm[0];
      // Cut an existing command before beginning a fresh sequential scan.
      stopAll(now);
      motion.scan(speed, command.durationMs, now);
      Serial.printf("SCAN: FL, PWM=%d, each=%lums, gap=%lums\n", speed,
                    static_cast<unsigned long>(command.durationMs),
                    static_cast<unsigned long>(debugcar::kScanGapMs));
      break;
    }
    case CommandType::Status: printStatus(now, true); break;
    case CommandType::Telemetry:
      telemetry = command.enabled;
      Serial.printf("OK: telemetry %s\n", telemetry ? "on" : "off");
      break;
    case CommandType::Stop:
      stopAll(now);
      Serial.println("STOP: outputs LOW (coast).");
      break;
    case CommandType::Help:
      stopAll(now);
      printHelp();
      break;
    case CommandType::Invert:
      stopAll(now);
      if (command.enabled) config.inversionMask |= 1u << command.wheel;
      else config.inversionMask &= ~(1u << command.wheel);
      Serial.printf("OK: %s invert=%d (use save to persist).\n",
                    board::kWheels[command.wheel].name, command.enabled);
      break;
    case CommandType::Limit:
      stopAll(now);
      pwmLimit = command.value;
      Serial.printf("OK: limit=%d; resets to %d after reboot.\n", pwmLimit, board::kStartupLimit);
      break;
    case CommandType::Ppr:
      stopAll(now);
      for (int i = 0; i < 4; ++i)
        if (command.wheel == 4 || command.wheel == i) config.ppr[i] = command.value;
      Serial.println("OK: PPR updated (use save to persist).");
      break;
    case CommandType::Zero:
      stopAll(now);
      zeroEncoders(now);
      Serial.println("OK: counters cleared.");
      break;
    case CommandType::Save:
      stopAll(now);
      if (storageReady && preferences.putBytes("calibration", &config, sizeof(config)) == sizeof(config))
        Serial.println("OK: direction and PPR saved to NVS.");
      else Serial.println("ERR: NVS save failed.");
      break;
    case CommandType::Load:
      stopAll(now);
      Serial.println(loadConfig() ? "OK: saved calibration loaded." : "INFO: no valid saved calibration; defaults loaded.");
      break;
    case CommandType::Defaults:
      stopAll(now);
      config = SavedConfig{};
      pwmLimit = board::kStartupLimit;
      Serial.println("OK: RAM defaults loaded; use save to overwrite saved calibration.");
      break;
    case CommandType::Invalid:
      stopAll(now);
      Serial.print("ERR: ");
      Serial.println(command.error);
      break;
  }
}

void serviceSerial() {
  // Bound work per loop so incoming serial traffic cannot starve the timers.
  for (int budget = 0; budget < 32 && Serial.available(); ++budget) {
    const char ch = static_cast<char>(Serial.read());
    const uint32_t now = millis();
    serviceMotion(now);
    if (ch == '!') {
      stopAll(now);
      lineLength = 0;
      discardLine = true;  // Discard the rest of this line, even after an error.
      Serial.println("STOP: emergency request; send newline before next command.");
    } else if (ch == '\r' || ch == '\n') {
      if (!discardLine && lineLength) {
        line[lineLength] = '\0';
        execute(debugcar::parseCommand(line), now);
      }
      lineLength = 0;
      discardLine = false;
    } else if (discardLine) {
      continue;
    } else if (ch == '\b' || ch == 127) {
      if (lineLength) --lineLength;
    } else if ((ch >= 32 && ch <= 126) || ch == '\t') {
      if (lineLength + 1 < sizeof(line)) line[lineLength++] = ch;
      else {
        stopAll(now);
        discardLine = true;
        lineLength = 0;
        Serial.println("ERR: line too long; stopped, discarding until newline.");
      }
    } else {
      // Reject NUL and binary/non-ASCII bytes rather than accepting a hidden suffix.
      stopAll(now);
      discardLine = true;
      lineLength = 0;
      Serial.println("ERR: non-ASCII/control byte; stopped. Use ASCII commands.");
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(board::kBaudRate);
  hardwareReady = beginHardware();
  if (!hardwareReady) hardwareFault();
  for (int i = 0; i < 4; ++i) {
    // GPIO34/35/36 have no internal pull-up; all four use plain INPUT.
    pinMode(board::kWheels[i].encoder, INPUT);
    attachInterruptArg(board::kWheels[i].encoder, onEncoder, &encoderIndices[i], RISING);
  }
  storageReady = preferences.begin("mecanumdbg", false);
  const bool loaded = loadConfig();
  lastEncoderSampleMs = millis();
  lastTelemetryMs = millis();
  stopAll(millis());
  Serial.println("\nREADY: Mecanum debug. Motors stopped. Type help or status.");
  Serial.println(loaded ? "INFO: saved calibration loaded." : "INFO: default calibration; all invert=0, PPR unknown.");
  if (!storageReady) Serial.println("INFO: NVS unavailable; calibration cannot be saved.");
  printStatus(millis(), true);
}

void loop() {
  serviceMotion(millis());
  serviceSerial();
  const uint32_t now = millis();
  serviceMotion(now);
  sampleEncoders(now);
  if (telemetry && static_cast<uint32_t>(now - lastTelemetryMs) >= board::kTelemetryMs) {
    lastTelemetryMs = now;
    printStatus(now, false);
  }
  delay(1);
}
