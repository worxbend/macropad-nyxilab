// Minimal Arduino API so Adafruit GFX + src/ui/render.cpp compile on a PC.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#ifndef ARDUINO
#define ARDUINO 100
#endif
#define PROGMEM
#define F(s) (s)
typedef bool boolean;
typedef uint8_t byte;
using std::max;
using std::min;

#include <string>

class __FlashStringHelper;
class String {
 public:
  String(const char* s = "") : s_(s ? s : "") {}
  unsigned int length() const { return unsigned(s_.size()); }
  const char* c_str() const { return s_.c_str(); }

 private:
  std::string s_;
};
inline double radians(double deg) { return deg * M_PI / 180.0; }

#include "Print.h"
