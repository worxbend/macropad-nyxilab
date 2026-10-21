// Host bridge protocol: the macropad <-> the daemon on the computer (host/nyxd).
// Pure C++ (no Arduino), unit-tested in common/firmware/test/test_core.
//
// Transport: a vendor HID interface (usage page 0xFF4E, usage 0x58) with 64-byte
// input and output reports and no report id.  Every packet is 64 bytes:
//
//   [0] type   [1] protocol version (PROTO_VERSION)   [2..63] payload, little-endian
//
//   host -> device                         device -> host
//   0x01 HELLO   hostname                  0x81 HELLO_ACK  edition, layers, firmware name
//   0x02 METRICS cpu, cores, memory, ...   0x82 KEY        HOST(id) pressed / released
//   0x03 TEXT    slot, text                0x83 STATE      host view shown, base + top layer
//   0x04 BYE     the daemon is stopping
//
// The device treats the host as connected while packets keep arriving
// (HOST_TIMEOUT_MS); the daemon sends METRICS every second.  If the link comes
// back without a HELLO (it timed out, e.g. while the computer slept), the device
// sends an unrequested HELLO_ACK and the daemon replays HELLO and its text.
// Packets of another protocol version are ignored.  docs/host-bridge.md has the
// byte layouts.
#pragma once
#include <stdint.h>

namespace mp {

constexpr uint8_t PROTO_VERSION = 1;
constexpr uint8_t HOST_PACKET = 64;
constexpr uint16_t HOST_USAGE_PAGE = 0xFF4E, HOST_USAGE = 0x58;
constexpr uint32_t HOST_TIMEOUT_MS = 5000;

enum HostPacket : uint8_t {
  PKT_HELLO = 0x01,
  PKT_METRICS = 0x02,
  PKT_TEXT = 0x03,
  PKT_BYE = 0x04,
  PKT_HELLO_ACK = 0x81,
  PKT_KEY = 0x82,
  PKT_STATE = 0x83,
};

constexpr uint8_t HOST_UNKNOWN = 0xFF;      // a percentage / clock field the host could not measure
constexpr int16_t HOST_NO_TEMP = -32768;    // a temperature the host could not measure
constexpr uint8_t HOST_MAX_CORES = 16;      // the daemon averages more cores into 16 groups
constexpr uint8_t HOST_TEXT_SLOTS = 4;
constexpr uint8_t HOST_TEXT_LEN = 60;       // printable ASCII; the daemon transliterates
constexpr uint8_t HOST_HOSTNAME_LEN = 31;
constexpr uint8_t HOST_HISTORY = 36;        // one sample per METRICS packet (~1 s)

struct HostMetrics {
  uint8_t cpu_pct = HOST_UNKNOWN;
  uint8_t n_cores = 0;
  uint8_t core_pct[HOST_MAX_CORES] = {};
  uint32_t mem_used_mib = 0, mem_total_mib = 0;
  uint8_t swap_pct = HOST_UNKNOWN;
  uint8_t disk_pct = HOST_UNKNOWN;
  int16_t cpu_temp_c10 = HOST_NO_TEMP;  // 0.1 degC
  uint16_t load1_x100 = 0;
  uint32_t net_rx_bps = 0, net_tx_bps = 0;  // bytes / s
  uint32_t uptime_s = 0;
  uint8_t gpu_pct = HOST_UNKNOWN;
  int16_t gpu_temp_c10 = HOST_NO_TEMP;
  uint8_t clock_h = HOST_UNKNOWN, clock_m = HOST_UNKNOWN;  // host local time

  uint8_t mem_pct() const {
    return mem_total_mib ? uint8_t(uint64_t(mem_used_mib) * 100 / mem_total_mib) : HOST_UNKNOWN;
  }
};

// Everything the displays need, copied into the UI snapshot.
struct HostData {
  bool linked = false;       // a daemon is talking to us
  bool has_metrics = false;  // at least one METRICS packet since it connected
  char hostname[HOST_HOSTNAME_LEN + 1] = {};
  HostMetrics m;
  char text[HOST_TEXT_SLOTS][HOST_TEXT_LEN + 1] = {};
  uint8_t cpu_hist[HOST_HISTORY] = {};  // oldest first
  uint8_t mem_hist[HOST_HISTORY] = {};
};

class HostProto {
 public:
  // What HELLO_ACK reports: edition (1 joystick, 2 rotary), layer count, a short firmware name.
  void set_identity(uint8_t edition, uint8_t n_layers, const char* firmware);

  // A packet from the host (any length; short packets are zero-padded).  Returns true if it was valid.
  bool on_packet(const uint8_t* buf, uint16_t len, uint32_t now);
  void tick(uint32_t now);  // drops the link after HOST_TIMEOUT_MS of silence

  bool linked() const { return data_.linked; }
  const HostData& data() const { return data_; }

  // Device -> host.  Queued only while linked (nobody listens otherwise); a full queue drops.
  void send_key(uint8_t id, bool pressed);
  void send_state(bool host_view, uint8_t base, uint8_t top);
  bool next_tx(uint8_t (&out)[HOST_PACKET]);  // pops the next packet to send
  // True once after a HELLO: the app should report its state again.
  bool take_state_request();

  static constexpr uint8_t TX_QUEUE = 8;

 private:
  void reset_data();
  void link();  // queue HELLO_ACK and a state report
  void push(const uint8_t (&pkt)[HOST_PACKET]);

  HostData data_;
  uint32_t last_rx_ = 0;
  bool state_request_ = false;
  uint8_t edition_ = 0, n_layers_ = 0;
  char firmware_[HOST_PACKET - 5] = {};
  uint8_t tx_[TX_QUEUE][HOST_PACKET] = {};
  uint8_t tx_head_ = 0, tx_n_ = 0;
};

}  // namespace mp
