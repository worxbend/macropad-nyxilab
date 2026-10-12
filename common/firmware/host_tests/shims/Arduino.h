#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

constexpr int INPUT_PULLUP = 2, LOW = 0, CHANGE = 3;
inline uint32_t mock_now = 0;
inline unsigned mock_delays = 0;
inline int mock_adc[32] = {}, mock_pins[32] = {};
inline uint32_t millis() { return mock_now; }
inline void delay(int) { ++mock_delays; }
inline void pinMode(int, int) {}
inline int digitalRead(int pin) { return mock_pins[pin]; }
inline int analogRead(int pin) { return mock_adc[pin]; }
inline void analogReadResolution(int) {}
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
inline void noInterrupts() {}
inline void interrupts() {}
struct MockSerial {
  int space = 128;
  std::string output;
  int availableForWrite() { return space; }
  void write(const char* data, int len) { output.append(data, len); }
  template<typename... Args> void printf(const char* format, Args... args) {
    char line[256];
    const int len = snprintf(line, sizeof line, format, args...);
    if (len > 0 && len < int(sizeof line)) write(line, len);
  }
  void println(const char* line) { output += line; output += '\n'; }
};
inline MockSerial Serial;
