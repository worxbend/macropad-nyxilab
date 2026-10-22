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
inline bool mock_vendor_ready = true;  // the host bridge's own interface and endpoint
inline std::vector<std::array<uint8_t, 64>> mock_vendor_reports;
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
struct Adafruit_USBD_HID;
inline std::vector<Adafruit_USBD_HID*> mock_hids;  // in begin() order
struct Adafruit_USBD_HID {
  using SetReport = void (*)(uint8_t, hid_report_type_t, uint8_t const*, uint16_t);
  SetReport set_report = nullptr;
  bool vendor = false;
  void setPollInterval(int) {}
  void setReportDescriptor(const uint8_t*, int) {}
  void setStringDescriptor(const char*) {}
  void enableOutEndpoint(bool on) { vendor = on; }
  void setReportCallback(decltype(nullptr), SetReport f) { set_report = f; }
  void begin() { mock_hids.push_back(this); }
  bool ready() { return vendor ? mock_vendor_ready : mock_ready; }
  bool sendReport(int, const void* data, int len) {
    if (!mock_accept || len != 64) return false;
    std::array<uint8_t, 64> r{};
    for (int i = 0; i < 64; ++i) r[i] = static_cast<const uint8_t*>(data)[i];
    mock_vendor_reports.push_back(r);
    mock_vendor_ready = false;
    return true;
  }
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
