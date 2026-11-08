#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>
extern std::vector<uint8_t> fakeStoredConfig;

class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  size_t getBytesLength(const char*) { return fakeStoredConfig.size(); }
  size_t getBytes(const char*, void* destination, size_t size) {
    if (size != fakeStoredConfig.size()) return 0;
    memcpy(destination, fakeStoredConfig.data(), size);
    return size;
  }
  size_t putBytes(const char*, const void* source, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(source);
    fakeStoredConfig.assign(bytes, bytes + size);
    return size;
  }
};

