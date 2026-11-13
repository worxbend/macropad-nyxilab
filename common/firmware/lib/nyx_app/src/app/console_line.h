// Collects serial console input into lines. Header-only and free of Arduino, so the
// mocked-hardware tests can pin the line ends (app.cpp has no test).
#pragma once
#include <stdint.h>

namespace app {

class ConsoleLine {
 public:
  // Takes one received character; true when it ends a line, which text() then holds.
  bool feed(char ch) {
    const bool lf_after_cr = ch == '\n' && prev_ == '\r';  // the second half of a CRLF line end
    prev_ = ch;
    if (lf_after_cr) return false;
    if (ch != '\n' && ch != '\r') {
      if (n_ < sizeof(line_) - 1) line_[n_++] = ch;
      return false;
    }
    line_[n_] = 0;
    n_ = 0;
    return true;
  }
  const char* text() const { return line_; }

 private:
  char line_[32] = {};
  uint8_t n_ = 0;
  char prev_ = 0;
};

}  // namespace app
