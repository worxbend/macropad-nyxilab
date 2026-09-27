#include "engine.h"

namespace mp {

void ascii_to_hid(char ch, uint8_t& mods, uint8_t& usage) {
  mods = 0;
  usage = 0;
  if (ch >= 'a' && ch <= 'z') {
    usage = uint8_t(hid::A + (ch - 'a'));
    return;
  }
  if (ch >= 'A' && ch <= 'Z') {
    usage = uint8_t(hid::A + (ch - 'A'));
    mods = MOD_LSHIFT;
    return;
  }
  if (ch >= '1' && ch <= '9') {
    usage = uint8_t(hid::N1 + (ch - '1'));
    return;
  }
  if (ch == '0') {
    usage = hid::N0;
    return;
  }
  struct Sym {
    char c;
    uint8_t usage;
    bool shift;
  };
  static const Sym kSyms[] = {
      {' ', hid::SPACE, false}, {'\n', hid::ENTER, false}, {'\t', hid::TAB, false}, {'!', hid::N1, true},
      {'@', hid::N2, true},     {'#', hid::N3, true},      {'$', hid::N4, true},    {'%', hid::N5, true},
      {'^', hid::N6, true},     {'&', hid::N7, true},      {'*', hid::N8, true},    {'(', hid::N9, true},
      {')', hid::N0, true},     {'-', hid::MINUS, false},  {'_', hid::MINUS, true}, {'=', hid::EQUAL, false},
      {'+', hid::EQUAL, true},  {'[', hid::LBRC, false},   {'{', hid::LBRC, true},  {']', hid::RBRC, false},
      {'}', hid::RBRC, true},   {'\\', hid::BSLS, false},  {'|', hid::BSLS, true},  {';', hid::SCLN, false},
      {':', hid::SCLN, true},   {'\'', hid::QUOT, false},  {'"', hid::QUOT, true},  {'`', hid::GRV, false},
      {'~', hid::GRV, true},    {',', hid::COMM, false},   {'<', hid::COMM, true},  {'.', hid::DOT, false},
      {'>', hid::DOT, true},    {'/', hid::SLSH, false},   {'?', hid::SLSH, true},
  };
  for (const Sym& s : kSyms) {
    if (s.c == ch) {
      usage = s.usage;
      mods = s.shift ? MOD_LSHIFT : 0;
      return;
    }
  }
}

uint8_t Engine::top_layer() const {
  const uint32_t m = layer_mask();
  for (int l = MAX_LAYERS - 1; l >= 0; l--)
    if (m & (1u << l)) return uint8_t(l);
  return 0;
}

void Engine::set_base_layer(uint8_t layer) {
  if (layer >= NUM_LAYERS) return;
  base_ = layer;
  mask_ = 0;  // TO()/next-layer semantics: drop toggled layers ...
  for (uint8_t l = 0; l < MAX_LAYERS; l++)
    if (mo_count_[l]) mask_ |= 1u << l;  // ... but keep layers whose MO key is still held
}

Action Engine::resolve(uint8_t r, uint8_t c) const {
  const uint32_t m = layer_mask();
  for (int l = NUM_LAYERS - 1; l >= 0; l--) {
    if (!(m & (1u << l))) continue;
    const Action a = LAYERS[l].keys[r][c].action;
    if (act_type(a) != ActType::Transparent) return a;
  }
  return XX;
}

void Engine::process(const KeyEvent& ev) {
  now_ = ev.time;
  const uint8_t r = ev.row, c = ev.col;
  if (r >= ROWS || c >= COLS) return;
  if (ev.pressed) {
    if (down_[r][c]) return;
    down_[r][c] = true;
    // Any other key while a tap-hold is undecided turns it into a hold,
    // so "hold LAYER + press key" works without waiting for the term.
    if (pending_.active) resolve_pending_as_hold(ev.time);
    const Action a = resolve(r, c);
    active_[r][c] = a;
    last_ = {r, c, top_layer(), ev.time};
    if (act_type(a) == ActType::TapHold && act_arg(a) < NUM_TAP_HOLDS) {
      pending_ = {true, r, c, uint8_t(act_arg(a)), ev.time};
      return;
    }
    press_action(a, ev.time);
  } else {
    if (!down_[r][c]) return;
    down_[r][c] = false;
    const Action a = active_[r][c];
    active_[r][c] = 0;
    if (pending_.active && pending_.row == r && pending_.col == c) {
      pending_.active = false;  // released within the term: it was a tap
      const TapHoldDef& th = TAP_HOLDS[pending_.index];
      press_action(th.tap, ev.time);
      tap_release_ = th.tap;
      tap_release_armed_ = true;
      tap_t_ = ev.time;
      rebuild();
      return;
    }
    if (act_type(a) == ActType::TapHold && act_arg(a) < NUM_TAP_HOLDS)
      release_action(TAP_HOLDS[act_arg(a)].hold);
    else
      release_action(a);
  }
  rebuild();
}

void Engine::resolve_pending_as_hold(uint32_t now) {
  if (!pending_.active) return;
  pending_.active = false;
  press_action(TAP_HOLDS[pending_.index].hold, now);
}

void Engine::tick(uint32_t now) {
  now_ = now;
  bool changed = false;
  if (pending_.active && uint32_t(now - pending_.t0) >= TAP_HOLDS[pending_.index].term_ms) {
    resolve_pending_as_hold(now);
    changed = true;
  }
  // keep a tapped key down for at least 10 ms so the host sees it
  if (tap_release_armed_ && uint32_t(now - tap_t_) >= 10) {
    release_action(tap_release_);
    tap_release_armed_ = false;
    changed = true;
  }
  if (macro_ && int32_t(now - macro_next_) >= 0) {
    if (!macro_key_down_) {
      while (*macro_) {
        ascii_to_hid(*macro_, macro_mods_, macro_usage_);
        if (macro_usage_) break;
        macro_++;  // skip characters we cannot type
      }
      if (*macro_) {
        macro_key_down_ = true;
      } else {
        macro_ = nullptr;
      }
    } else {
      macro_key_down_ = false;
      macro_++;
      if (!*macro_) macro_ = nullptr;
    }
    macro_next_ = now + 8;
    changed = true;
  }
  if (changed) rebuild();
}

void Engine::press_action(Action a, uint32_t now) {
  switch (act_type(a)) {
    case ActType::Key:
    case ActType::Consumer:
    case ActType::MouseBtn:
      if (n_held_ < MAX_HELD) held_[n_held_++] = a;
      break;
    case ActType::LayerMo: {
      const uint32_t l = act_arg(a);
      if (l < MAX_LAYERS) {
        mo_count_[l]++;
        mask_ |= 1u << l;
      }
      break;
    }
    case ActType::LayerTg: {
      const uint32_t l = act_arg(a);
      if (l < MAX_LAYERS) mask_ ^= 1u << l;
      break;
    }
    case ActType::LayerTo:
      set_base_layer(uint8_t(act_arg(a)));
      break;
    case ActType::LayerNext: {
      uint8_t n = base_;
      for (uint8_t i = 0; i < NUM_LAYERS; i++) {
        n = uint8_t((n + 1) % NUM_LAYERS);
        if (n != FN_LAYER) break;
      }
      set_base_layer(n);
      break;
    }
    case ActType::Macro:
      if (act_arg(a) < NUM_MACROS && !macro_) {
        macro_ = MACROS[act_arg(a)];
        macro_key_down_ = false;
        macro_next_ = now;
      }
      break;
    case ActType::Sys:
      host_.on_sys(SysCmd(act_arg(a)));
      break;
    case ActType::JoyMode:
      host_.on_joy_mode(JoyMode(act_arg(a)));
      break;
    default:
      break;
  }
}

void Engine::release_action(Action a) {
  switch (act_type(a)) {
    case ActType::Key:
    case ActType::Consumer:
    case ActType::MouseBtn:
      for (uint8_t i = 0; i < n_held_; i++) {
        if (held_[i] != a) continue;
        for (uint8_t j = i + 1; j < n_held_; j++) held_[j - 1] = held_[j];
        n_held_--;
        break;
      }
      break;
    case ActType::LayerMo: {
      const uint32_t l = act_arg(a);
      if (l < MAX_LAYERS && mo_count_[l] && --mo_count_[l] == 0) mask_ &= ~(1u << l);
      break;
    }
    default:
      break;
  }
}

void Engine::rebuild() {
  HidState h{};
  uint8_t n = 0;
  auto add_key = [&](uint8_t u) {
    if (!u) return;
    for (uint8_t i = 0; i < n; i++)
      if (h.keys[i] == u) return;
    if (n < 6) h.keys[n++] = u;
  };
  for (uint8_t i = 0; i < n_held_; i++) {
    const Action a = held_[i];
    switch (act_type(a)) {
      case ActType::Key:
        h.mods |= uint8_t(act_arg(a) >> 8);
        add_key(uint8_t(act_arg(a) & 0xFF));
        break;
      case ActType::Consumer:
        h.consumer = uint16_t(act_arg(a));  // the most recent one wins
        break;
      case ActType::MouseBtn:
        h.mouse_buttons |= uint8_t(act_arg(a));
        break;
      default:
        break;
    }
  }
  if (macro_ && macro_key_down_) {
    h.mods |= macro_mods_;
    add_key(macro_usage_);
  }
  for (uint8_t i = 0; i < n_extra_; i++) add_key(extra_[i]);
  hid_ = h;
}

void Engine::set_extra_keys(const uint8_t* usages, uint8_t n) {
  n_extra_ = n > 6 ? 6 : n;
  for (uint8_t i = 0; i < n_extra_; i++) extra_[i] = usages[i];
  rebuild();
}

}  // namespace mp
