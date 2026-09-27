// Key processing engine: layers, tap-hold, macros -> HID report state.
// Pure C++ (no Arduino), unit-tested in test/test_core.
#pragma once
#include <stdint.h>

#include "debounce.h"
#include "keymap.h"

namespace mp {

struct HidState {
  uint8_t mods = 0;
  uint8_t keys[6] = {};
  uint16_t consumer = 0;
  uint8_t mouse_buttons = 0;

  bool operator==(const HidState& o) const {
    if (mods != o.mods || consumer != o.consumer || mouse_buttons != o.mouse_buttons) return false;
    for (int i = 0; i < 6; i++)
      if (keys[i] != o.keys[i]) return false;
    return true;
  }
  bool operator!=(const HidState& o) const { return !(*this == o); }
  bool has_key(uint8_t usage) const {
    for (uint8_t k : keys)
      if (k == usage) return true;
    return false;
  }
};

class EngineHost {
 public:
  virtual ~EngineHost() = default;
  virtual void on_sys(SysCmd cmd) = 0;
  virtual void on_joy_mode(JoyMode mode) = 0;
};

// Translate an ASCII character to (modifiers, usage) on a US layout.  usage 0 = unsupported.
void ascii_to_hid(char ch, uint8_t& mods, uint8_t& usage);

class Engine {
 public:
  explicit Engine(EngineHost& host) : host_(host) {}

  void process(const KeyEvent& ev);
  void tick(uint32_t now);

  const HidState& hid() const { return hid_; }
  uint8_t base_layer() const { return base_; }
  uint32_t layer_mask() const { return mask_ | (1u << base_); }
  uint8_t top_layer() const;
  void set_base_layer(uint8_t layer);
  bool key_down(uint8_t r, uint8_t c) const { return down_[r][c]; }
  bool macro_running() const { return macro_ != nullptr; }

  // Keys injected by other inputs (e.g. the joystick in Arrows mode).
  void set_extra_keys(const uint8_t* usages, uint8_t n);

  struct LastPress {
    uint8_t row = 0xFF, col = 0xFF, layer = 0;
    uint32_t time = 0;
  };
  const LastPress& last_press() const { return last_; }

  Action resolve(uint8_t r, uint8_t c) const;

 private:
  void press_action(Action a, uint32_t now);
  void release_action(Action a);
  void resolve_pending_as_hold(uint32_t now);
  void rebuild();

  EngineHost& host_;
  HidState hid_{};
  uint8_t base_ = 0;
  uint32_t mask_ = 0;  // momentary + toggled layers
  uint8_t mo_count_[MAX_LAYERS] = {};

  bool down_[ROWS][COLS] = {};
  Action active_[ROWS][COLS] = {};  // action bound to each held key

  // held actions that contribute to the report (keys, consumer, mouse)
  static constexpr uint8_t MAX_HELD = 16;
  Action held_[MAX_HELD] = {};
  uint8_t n_held_ = 0;

  // tap-hold resolution
  struct Pending {
    bool active = false;
    uint8_t row = 0, col = 0, index = 0;
    uint32_t t0 = 0;
  } pending_;
  Action tap_release_ = 0;  // tap action to release shortly after the tap
  bool tap_release_armed_ = false;
  uint32_t tap_t_ = 0;

  // macro playback
  const char* macro_ = nullptr;
  bool macro_key_down_ = false;
  uint32_t macro_next_ = 0;
  uint8_t macro_mods_ = 0, macro_usage_ = 0;

  uint8_t extra_[6] = {};
  uint8_t n_extra_ = 0;
  LastPress last_;
  uint32_t now_ = 0;
};

}  // namespace mp
