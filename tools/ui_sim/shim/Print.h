#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) {
    size_t k = 0;
    while (n--) k += write(*b++);
    return k;
  }
  size_t write(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), strlen(s)) : 0; }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write(uint8_t(c)); }
  size_t print(int v) {
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    return write(b);
  }
  size_t println(const char* s) { return print(s) + write("\n"); }
};
