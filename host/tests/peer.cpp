// The firmware's protocol code (core/host_proto.cpp) as a stand-in macropad for
// tests/interop.rs: 64-byte packets from the daemon on stdin, the device's
// replies on stdout, and what it understood as JSON on stderr at the end.
#include <stdio.h>

#include "core/host_proto.h"

int main() {
  mp::HostProto p;
  p.set_identity(2, 5, "peer firmware");
  uint8_t in[mp::HOST_PACKET], out[mp::HOST_PACKET];
  uint32_t now = 0;
  while (fread(in, 1, sizeof in, stdin) == sizeof in) {
    p.on_packet(in, sizeof in, now += 100);
    if (in[0] == mp::PKT_HELLO) {  // what the app does: report state, then a key goes down
      p.take_state_request();
      p.send_state(true, 3, 3);
      p.send_key(5, true);
    }
    while (p.next_tx(out)) fwrite(out, 1, sizeof out, stdout);
  }
  const mp::HostData& d = p.data();
  const mp::HostMetrics& m = d.m;
  fprintf(stderr, "{\"linked\": %d, \"hostname\": \"%s\", \"cpu\": %u, \"cores\": [", d.linked, d.hostname, m.cpu_pct);
  for (int i = 0; i < m.n_cores; i++) fprintf(stderr, "%s%u", i ? ", " : "", m.core_pct[i]);
  fprintf(stderr,
          "], \"mem_used\": %lu, \"mem_total\": %lu, \"swap\": %u, \"disk\": %u, \"temp10\": %d, \"load100\": %u, "
          "\"rx\": %lu, \"tx\": %lu, \"up\": %lu, \"gpu\": %u, \"gpu_temp10\": %d, \"h\": %u, \"m\": %u, "
          "\"text\": [\"%s\", \"%s\", \"%s\", \"%s\"]}\n",
          (unsigned long)m.mem_used_mib, (unsigned long)m.mem_total_mib, m.swap_pct, m.disk_pct, m.cpu_temp_c10,
          m.load1_x100, (unsigned long)m.net_rx_bps, (unsigned long)m.net_tx_bps, (unsigned long)m.uptime_s,
          m.gpu_pct, m.gpu_temp_c10, m.clock_h, m.clock_m, d.text[0], d.text[1], d.text[2], d.text[3]);
  return 0;
}
