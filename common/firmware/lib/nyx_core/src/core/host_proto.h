// Host bridge protocol: the macropad <-> the daemon on the computer (host/, nyxd).
// host/src/protocol.rs is the daemon's side; host/tests/interop.rs runs it against this code.
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
//   0x05 WORKSPACES   sway: workspaces 1-8, focused window
//   0x06 PLAYER       MPRIS: status, volume, position, player name
//   0x07 PLAYER_TEXT  field (title / artist / album), text
//
//   WORKSPACES  [2] running  [3] focused 1..8 (0 other/none)  [4] open  [5] visible  [6] urgent
//               (masks: bit n-1 = workspace n)  [7] windows  [8] SwayLayout  [9..20] app_id  [21..60] title
//   PLAYER      [2] present  [3] PlayerStatus  [4] volume % (255 unknown)  [5..6] position s
//               [7..8] length s (0 unknown)  [9..24] player name
//   PLAYER_TEXT [2] PlayerField  [3..62] text
//   Strings are printable ASCII, NUL-padded.  running / present = 0: the rest is ignored.
//
// The device treats the host as connected while packets keep arriving
// (HOST_TIMEOUT_MS); the daemon sends METRICS every second.  If the link comes
// back without a HELLO (it timed out, e.g. while the computer slept), the device
// sends an unrequested HELLO_ACK and the daemon replays HELLO and its text.
// Packets of another protocol version or an unknown type are ignored.
// docs/host-bridge.md has the byte layouts.
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
  PKT_WORKSPACES = 0x05,
  PKT_PLAYER = 0x06,
  PKT_PLAYER_TEXT = 0x07,
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
constexpr uint8_t HOST_WORKSPACES = 8;      // sway workspaces 1..8: the HOST layer's right block
constexpr uint8_t HOST_APP_ID_LEN = 12;     // the focused window's app_id (XWayland: its class)
constexpr uint8_t HOST_TITLE_LEN = 40;      // the focused window's title
constexpr uint8_t HOST_PLAYER_NAME_LEN = 16;
constexpr uint8_t HOST_MEDIA_LEN = 60;      // a track's title, artist or album

enum SwayLayout : uint8_t { SWAY_UNKNOWN, SWAY_SPLITH, SWAY_SPLITV, SWAY_TABBED, SWAY_STACKED };
enum PlayerStatus : uint8_t { PLAYER_STOPPED, PLAYER_PLAYING, PLAYER_PAUSED };
enum PlayerField : uint8_t { PLAYER_TITLE, PLAYER_ARTIST, PLAYER_ALBUM };

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

// The window manager's state (sway), from WORKSPACES packets.  Bit n-1 of a mask = workspace n.
struct HostSway {
  bool running = false;  // the daemon talks to a running sway
  uint8_t focused = 0;   // 1..8; 0 = a workspace outside 1..8, or none
  uint8_t open = 0;      // workspaces that exist (windows on them, or shown)
  uint8_t visible = 0;   // shown on a monitor
  uint8_t urgent = 0;
  uint8_t windows = 0;   // on the focused workspace, tiling + floating
  uint8_t layout = SWAY_UNKNOWN;
  char app_id[HOST_APP_ID_LEN + 1] = {};
  char title[HOST_TITLE_LEN + 1] = {};
};

// The media player (MPRIS through playerctl), from PLAYER and PLAYER_TEXT packets.
struct HostPlayer {
  bool present = false;  // the daemon sees a media player
  uint8_t status = PLAYER_STOPPED;
  uint8_t volume = HOST_UNKNOWN;  // 0..100 %
  uint16_t position_s = 0, length_s = 0;  // length 0 = unknown
  char name[HOST_PLAYER_NAME_LEN + 1] = {};
  char title[HOST_MEDIA_LEN + 1] = {}, artist[HOST_MEDIA_LEN + 1] = {}, album[HOST_MEDIA_LEN + 1] = {};
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
  HostSway sway;
  HostPlayer player;
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
  void on_workspaces(const uint8_t* p);
  void on_player(const uint8_t* p);
  bool on_player_text(const uint8_t* p);
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
