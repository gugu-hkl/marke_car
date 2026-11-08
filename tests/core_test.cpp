#include "debug_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

using namespace debugcar;

namespace {
int checks = 0;
void check(bool condition, const char* message) {
  ++checks;
  if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
void expect(Wheels actual, int fl, int fr, int rl, int rr, const char* message) {
  check(actual.pwm[0] == fl && actual.pwm[1] == fr && actual.pwm[2] == rl &&
            actual.pwm[3] == rr, message);
}
Command parse(const char* text) {
  char buffer[256];
  check(strlen(text) < sizeof(buffer), "test command fits buffer");
  strcpy(buffer, text);
  return parseCommand(buffer);
}
}  // namespace

int main() {
  expect(mecanum(60, 0, 0, 100), 60, 60, 60, 60, "forward wheel directions");
  expect(mecanum(0, 60, 0, 100), -60, 60, 60, -60, "left wheel directions");
  expect(mecanum(0, 0, 60, 100), -60, 60, -60, 60, "CCW wheel directions");
  expect(mecanum(60, 60, 0, 100), 0, 100, 100, 0, "forward-left diagonal");
  expect(mecanum(60, -60, 0, 100), 100, 0, 0, 100, "forward-right diagonal");
  expect(mecanum(0, 0, 0, 100), 0, 0, 0, 0, "zero mix");
  expect(mecanum(100, 100, 100, 100), -33, 100, 33, 33, "mixed motion preserves ratios");
  for (int x = -255; x <= 255; x += 51) {
    for (int y = -255; y <= 255; y += 51) {
      for (int z = -255; z <= 255; z += 51) {
        const auto mixed = mecanum(x, y, z, 100);
        for (int value : mixed.pwm) check(value >= -100 && value <= 100, "all mixes respect limit");
      }
    }
  }

  auto command = parse("M FL -60 800");
  check(command.type == CommandType::Motor && command.wheel == 0 &&
            command.wheels.pwm[0] == -60 && command.durationMs == 800, "case-insensitive single wheel");
  command = parse("m rr 50");
  check(command.durationMs == 1000 && command.wheels.pwm[3] == 50, "default duration and RR mapping");
  command = parse("scan");
  check(command.type == CommandType::Scan && command.durationMs == 800 && command.value == 60, "scan defaults");
  command = parse("drive 255 255 255 5000");
  expect(normalize(command.wheels, 100), -33, 100, 33, 33, "mix is scaled only once at runtime");
  check(command.durationMs == 5000, "maximum valid duration");
  const char* invalid[] = {
      "m fl 60 -1", "m fl 60 0", "m fl 60 5001", "m fl 256", "m fl -256",
      "m fl 12foo", "m fl 1.5", "m fl nan", "m fl 99999999999999999999999999",
      "m wheel 60", "m all 60", "m fl 60 500 extra", "stop extra", "scan 0",
      "scan 60 5010", "all 10 10 10", "all 1 2 3 4 500 extra", "drive 1 2 256",
      "move unknown 60", "move fwd -60", "invert fl 2", "invert all 1", "limit 0",
      "limit 256", "ppr all -1", "ppr fl 1000001", "telemetry maybe", "save extra",
      "", "   ", "a b c d e f g h i", "m fl + 100", "m fl --1 100"};
  for (const auto* text : invalid)
    check(parse(text).type == CommandType::Invalid, "malformed input rejected");
  check(parse("ppr all 0").type == CommandType::Ppr, "unknown PPR allowed");
  check(parse("ppr rr 1000000").wheel == 3, "individual PPR wheel mapping");
  check(parse("\t all\t10 20 -30 40\t500").type == CommandType::All, "tabs accepted");
  expect(parse("move right 60").wheels, 60, -60, -60, 60, "right command directions");
  expect(parse("move back 60").wheels, -60, -60, -60, -60, "back command directions");
  expect(parse("move cw 60").wheels, 60, -60, 60, -60, "clockwise command directions");

  TimedMotion motion;
  motion.pulse(mecanum(60, 0, 0, 100), 1000, 100);
  motion.update(1099);
  check(motion.mode() == TimedMotion::Mode::Pulse && motion.remainingMs(1099) == 1,
        "pulse active before deadline");
  motion.update(1100);
  check(motion.mode() == TimedMotion::Mode::Idle, "pulse stops exactly at deadline");
  expect(motion.target(), 0, 0, 0, 0, "timeout clears every wheel");
  motion.pulse(mecanum(60, 0, 0, 100), 100, UINT32_MAX - 49);
  motion.update(49);
  check(motion.remainingMs(49) == 1, "deadline across millis wrap");
  motion.update(50);
  check(motion.mode() == TimedMotion::Mode::Idle, "timeout across millis wrap");
  motion.scan(60, 100, 200);
  uint32_t now = 200;
  for (int wheel = 0; wheel < 4; ++wheel) {
    for (int i = 0; i < 4; ++i)
      check(motion.target().pwm[i] == (i == wheel ? 60 : 0), "scan runs exactly one wheel in order");
    now += 100;
    motion.update(now);
    expect(motion.target(), 0, 0, 0, 0, "scan cuts output between wheels");
    if (wheel < 3) {
      motion.update(now + 399);
      check(motion.inGap(), "scan maintains gap");
      now += 400;
      motion.update(now);
    }
  }
  check(motion.mode() == TimedMotion::Mode::Idle, "scan completes automatically");
  motion.scan(60, 800, 10000);
  motion.stop();
  motion.update(20000);
  expect(motion.target(), 0, 0, 0, 0, "stopped scan never resumes");
  motion.pulse(mecanum(60, 0, 0, 100), 5001, 0);
  check(motion.mode() == TimedMotion::Mode::Idle, "invalid internal duration cannot start motion");

  MotorRamp ramp;
  ramp.update(0, 1000);
  check(ramp.update(60, 1009) == 0, "ramp waits for tick");
  check(ramp.update(60, 1010) == 4, "ramp starts gently");
  check(ramp.update(60, 1150) == 60, "ramp reaches target");
  check(ramp.update(-60, 1151) == 0, "reversal first cuts output");
  check(ramp.update(-60, 1230) == 0, "reversal holds zero for at least 80ms");
  check(ramp.update(-60, 1240) < 0, "reverse starts after pause");
  check(ramp.update(0, 1241) == 0, "stop is immediate, not ramped");
  ramp.update(0, 1300);
  check(ramp.update(60, 1310) == 0, "explicit stop retains reversal timing");
  check(ramp.update(60, 1340) > 0, "repeated idle calls do not extend reverse pause");
  ramp.cut(UINT32_MAX - 49);
  check(ramp.update(-60, 29) == 0, "reverse hold works across clock wrap");
  check(ramp.update(-60, 50) < 0, "reverse resumes across clock wrap");

  printf("PASS: core logic (%d checks)\n", checks);
  return 0;
}

