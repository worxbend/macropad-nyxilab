#pragma once
#include <array>
#include <cstring>
struct MockEEPROM {
  std::array<unsigned char, 256> flash{}, buffer{};
  unsigned commits = 0;
  void begin(int) { buffer = flash; }
  template<typename T> void get(int offset, T& value) { memcpy(&value, buffer.data() + offset, sizeof value); }
  template<typename T> void put(int offset, const T& value) { memcpy(buffer.data() + offset, &value, sizeof value); }
  bool commit() { flash = buffer; ++commits; return true; }
};
inline MockEEPROM EEPROM;
