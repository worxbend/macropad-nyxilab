#include "host_proto.h"

#include <string.h>

namespace mp {
namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

// Copy a NUL-padded string, keeping printable ASCII only (the display fonts have nothing else).
void copy_text(char* dst, uint8_t cap, const uint8_t* src, uint8_t n) {
  uint8_t o = 0;
  for (uint8_t i = 0; i < n && src[i] && o < cap; i++) dst[o++] = (src[i] >= 0x20 && src[i] < 0x7F) ? char(src[i]) : '?';
  dst[o] = 0;
}

uint8_t pct(uint8_t v) { return v <= 100 ? v : HOST_UNKNOWN; }

}  // namespace

void HostProto::set_identity(uint8_t edition, uint8_t n_layers, const char* firmware) {
  edition_ = edition;
  n_layers_ = n_layers;
  strncpy(firmware_, firmware ? firmware : "", sizeof firmware_ - 1);
  firmware_[sizeof firmware_ - 1] = 0;
}

void HostProto::reset_data() { data_ = HostData{}; }

void HostProto::link() {
  data_.linked = true;  // so the reply is queued
  uint8_t ack[HOST_PACKET] = {PKT_HELLO_ACK, PROTO_VERSION, edition_, n_layers_, PROTO_VERSION};
  memcpy(ack + 5, firmware_, strlen(firmware_));
  push(ack);
  state_request_ = true;
}

bool HostProto::on_packet(const uint8_t* buf, uint16_t len, uint32_t now) {
  uint8_t p[HOST_PACKET] = {};
  memcpy(p, buf, len < HOST_PACKET ? len : HOST_PACKET);
  if (p[1] != PROTO_VERSION) return false;
  const bool was_linked = data_.linked;
  switch (p[0]) {
    case PKT_HELLO:
      reset_data();  // a (re)started daemon sends everything again
      copy_text(data_.hostname, HOST_HOSTNAME_LEN, p + 2, HOST_HOSTNAME_LEN);
      link();
      break;
    case PKT_METRICS: {
      HostMetrics& m = data_.m;
      m.cpu_pct = pct(p[2]);
      m.n_cores = p[3] <= HOST_MAX_CORES ? p[3] : HOST_MAX_CORES;
      for (uint8_t i = 0; i < HOST_MAX_CORES; i++) m.core_pct[i] = i < m.n_cores ? pct(p[4 + i]) : 0;
      m.mem_used_mib = rd32(p + 20);
      m.mem_total_mib = rd32(p + 24);
      if (m.mem_used_mib > m.mem_total_mib) m.mem_used_mib = m.mem_total_mib;
      m.swap_pct = pct(p[28]);
      m.disk_pct = pct(p[29]);
      m.cpu_temp_c10 = int16_t(rd16(p + 30));
      m.load1_x100 = rd16(p + 32);
      m.net_rx_bps = rd32(p + 34);
      m.net_tx_bps = rd32(p + 38);
      m.uptime_s = rd32(p + 42);
      m.gpu_pct = pct(p[46]);
      m.gpu_temp_c10 = int16_t(rd16(p + 47));
      m.clock_h = p[49] < 24 ? p[49] : HOST_UNKNOWN;
      m.clock_m = p[50] < 60 ? p[50] : HOST_UNKNOWN;
      memmove(data_.cpu_hist, data_.cpu_hist + 1, HOST_HISTORY - 1);
      memmove(data_.mem_hist, data_.mem_hist + 1, HOST_HISTORY - 1);
      const uint8_t mem = m.mem_pct();
      data_.cpu_hist[HOST_HISTORY - 1] = m.cpu_pct <= 100 ? m.cpu_pct : 0;
      data_.mem_hist[HOST_HISTORY - 1] = mem <= 100 ? mem : 0;
      data_.has_metrics = true;
      break;
    }
    case PKT_TEXT:
      if (p[2] >= HOST_TEXT_SLOTS) return false;
      copy_text(data_.text[p[2]], HOST_TEXT_LEN, p + 3, HOST_TEXT_LEN);
      break;
    case PKT_BYE:
      reset_data();
      tx_n_ = 0;
      return true;
    default:
      return false;
  }
  if (!was_linked && p[0] != PKT_HELLO) link();  // back after a timeout: ask for HELLO again
  last_rx_ = now;
  return true;
}

void HostProto::tick(uint32_t now) {
  if (data_.linked && uint32_t(now - last_rx_) >= HOST_TIMEOUT_MS) {
    reset_data();
    tx_n_ = 0;
  }
}

void HostProto::push(const uint8_t (&pkt)[HOST_PACKET]) {
  if (!data_.linked || tx_n_ >= TX_QUEUE) return;
  memcpy(tx_[(tx_head_ + tx_n_) % TX_QUEUE], pkt, HOST_PACKET);
  tx_n_++;
}

void HostProto::send_key(uint8_t id, bool pressed) {
  const uint8_t pkt[HOST_PACKET] = {PKT_KEY, PROTO_VERSION, id, uint8_t(pressed ? 1 : 0)};
  push(pkt);
}

void HostProto::send_state(bool host_view, uint8_t base, uint8_t top) {
  const uint8_t pkt[HOST_PACKET] = {PKT_STATE, PROTO_VERSION, uint8_t(host_view ? 1 : 0), base, top};
  push(pkt);
}

bool HostProto::next_tx(uint8_t (&out)[HOST_PACKET]) {
  if (!tx_n_) return false;
  memcpy(out, tx_[tx_head_], HOST_PACKET);
  tx_head_ = uint8_t((tx_head_ + 1) % TX_QUEUE);
  tx_n_--;
  return true;
}

bool HostProto::take_state_request() {
  const bool r = state_request_;
  state_request_ = false;
  return r;
}

}  // namespace mp
