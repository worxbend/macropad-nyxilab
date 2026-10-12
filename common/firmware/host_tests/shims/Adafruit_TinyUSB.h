#pragma once
#include <array>
#include <cstdint>
#include <vector>
#define HID_REPORT_ID(id) id
#define TUD_HID_REPORT_DESC_KEYBOARD(id) id
#define TUD_HID_REPORT_DESC_MOUSE(id) id
#define TUD_HID_REPORT_DESC_CONSUMER(id) id
using hid_report_type_t = int;
constexpr int HID_REPORT_TYPE_OUTPUT = 2;
struct MockReport {
  int id;
  uint16_t value;
  std::array<uint8_t, 6> keys{};
};
inline bool mock_ready = true, mock_accept = true;
inline std::vector<MockReport> mock_reports;
struct MockUSBDevice {
  bool connected = true, asleep = false;
  unsigned wakes = 0;
  bool isInitialized() { return true; }
  void begin(int) {}
  void setManufacturerDescriptor(const char*) {}
  void setProductDescriptor(const char*) {}
  bool mounted() { return connected; }
  bool suspended() { return asleep; }
  void detach() { connected = false; }
  void attach() { connected = true; }
  void remoteWakeup() { ++wakes; }
};
inline MockUSBDevice TinyUSBDevice;
struct Adafruit_USBD_HID {
  void setPollInterval(int) {}
  void setReportDescriptor(const uint8_t*, int) {}
  void setStringDescriptor(const char*) {}
  template<typename T> void setReportCallback(decltype(nullptr), T) {}
  void begin() {}
  bool ready() { return mock_ready; }
  bool keyboardReport(int id, uint8_t mods, uint8_t* keys) {
    if (!mock_accept) return false;
    MockReport report{id, mods, {}};
    for (int i = 0; i < 6; ++i) report.keys[i] = keys[i];
    mock_reports.push_back(report);
    mock_ready = false;
    return true;
  }
  bool sendReport16(int id, uint16_t value) {
    if (!mock_accept) return false;
    mock_reports.push_back({id, value, {}});
    mock_ready = false;
    return true;
  }
  bool mouseReport(int id, uint8_t buttons, int8_t, int8_t, int8_t, int8_t) {
    if (!mock_accept) return false;
    mock_reports.push_back({id, buttons, {}});
    mock_ready = false;
    return true;
  }
};
