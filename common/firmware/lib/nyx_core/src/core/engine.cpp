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
  if (service_taps(now)) changed = true;
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
    case ActType::Centre:
      host_.on_centre(uint8_t(act_arg(a)));
      break;
    case ActType::Wheel:
      wheel_ = int16_t(wheel_ + int8_t(act_arg(a) & 0xFF));
      pan_ = int16_t(pan_ + int8_t((act_arg(a) >> 8) & 0xFF));
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

// ------------------------------------------------------------------ knob
void Engine::set_encoder_presets(const EncoderDef* presets, uint8_t n) {
  presets_ = presets;
  n_presets_ = presets ? n : 0;
}

const EncoderDef& Engine::encoder_binding() const {
  static const EncoderDef kNone = {XX, XX, XX, ""};
  const uint32_t m = layer_mask();
  for (int l = NUM_LAYERS - 1; l >= 0; l--) {
    if (!(m & (1u << l))) continue;
    if (l == base_ && enc_preset_[l] < n_presets_) return presets_[enc_preset_[l]];
    if (act_type(LAYERS[l].centre.knob.cw) != ActType::Transparent) return LAYERS[l].centre.knob;
  }
  return n_presets_ ? presets_[0] : kNone;
}

uint8_t Engine::cycle_enc_preset(uint8_t base_layer) {
  if (base_layer >= MAX_LAYERS) return ENC_LAYER_DEFAULT;
  const uint8_t cur = enc_preset_[base_layer];
  const EncoderDef& own = LAYERS[base_layer < NUM_LAYERS ? base_layer : 0].centre.knob;
  const bool has_own = act_type(own.cw) != ActType::Transparent;
  uint8_t next = ENC_LAYER_DEFAULT;
  for (uint8_t i = cur < n_presets_ ? uint8_t(cur + 1) : 0; i < n_presets_; i++) {
    if (!has_own || !same_binding(presets_[i], own)) {  // skip presets identical to the layer's own
      next = i;
      break;
    }
  }
  enc_preset_[base_layer] = next;
  return next;
}

void Engine::set_enc_preset(uint8_t base_layer, uint8_t preset) {
  if (base_layer >= MAX_LAYERS) return;
  enc_preset_[base_layer] = preset < n_presets_ ? preset : uint8_t(ENC_LAYER_DEFAULT);
}

void Engine::encoder_step(int8_t dir, uint32_t now) {
  if (!dir) return;
  now_ = now;
  if (pending_.active) resolve_pending_as_hold(now);  // "hold LAYER + turn" = FN binding
  enc_pos_ += dir > 0 ? 1 : -1;
  const EncoderDef& b = encoder_binding();
  queue_tap(dir > 0 ? b.cw : b.ccw, now);
}

void Engine::encoder_button(bool pressed, uint32_t now) {
  now_ = now;
  if (pressed == enc_down_) return;
  enc_down_ = pressed;
  if (pressed) {
    if (pending_.active) resolve_pending_as_hold(now);
    enc_active_ = encoder_binding().press;
    if (act_type(enc_active_) == ActType::TapHold) enc_active_ = XX;  // tap-hold is for keys only
    press_action(enc_active_, now);
  } else {
    release_action(enc_active_);
    enc_active_ = XX;
  }
  rebuild();
}

void Engine::queue_tap(Action a, uint32_t now) {
  now_ = now;
  switch (act_type(a)) {
    case ActType::Key:
    case ActType::Consumer:
    case ActType::MouseBtn:
      if (tapq_n_ < TAP_QUEUE) {
        tapq_[(tapq_head_ + tapq_n_) % TAP_QUEUE] = a;
        tapq_n_++;
      }
      break;
    case ActType::None:
    case ActType::Transparent:
    case ActType::TapHold:
      break;
    default:  // layers, macros, wheel steps, system and mode commands: nothing to hold down
      press_action(a, now);
      release_action(a);
      break;
  }
  service_taps(now);
  rebuild();
}

bool Engine::service_taps(uint32_t now) {
  bool changed = false;
  if (tap_phase_ == TapPhase::Down && uint32_t(now - tap_t0_) >= TAP_MS) {
    release_action(tap_cur_);
    tap_phase_ = TapPhase::Gap;
    tap_t0_ = now;
    changed = true;
  }
  if (tap_phase_ == TapPhase::Gap && uint32_t(now - tap_t0_) >= TAP_MS) tap_phase_ = TapPhase::Idle;
  if (tap_phase_ == TapPhase::Idle && tapq_n_) {
    tap_cur_ = tapq_[tapq_head_];
    tapq_head_ = uint8_t((tapq_head_ + 1) % TAP_QUEUE);
    tapq_n_--;
    press_action(tap_cur_, now);
    tap_phase_ = TapPhase::Down;
    tap_t0_ = now;
    changed = true;
  }
  return changed;
}

void Engine::take_wheel(int8_t& wheel, int8_t& pan) {
  auto clamp8 = [](int16_t v) { return int8_t(v > 127 ? 127 : (v < -127 ? -127 : v)); };
  wheel = clamp8(wheel_);
  pan = clamp8(pan_);
  wheel_ = int16_t(wheel_ - wheel);
  pan_ = int16_t(pan_ - pan);
}

}  // namespace mp
