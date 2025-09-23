# Low-Level Design (LLD): UAV Digital Video Link

## 1. Repository Layout
vd-link/         # project root
  drone/         # drone application
  gs/            # ground station application
  vdctl/         # CLI tool for config
libs/
  ini/           # INI parser
  librtp/        # RTP library
  core/          # mux, qos, ini, log, metrics
  link/          # protobuf (currently proprietary)
  endpoints/     # rtp, osd, rc, config, telem
  hwio/          # uart (MSP), hid
  net/           # udp/quic wrappers
adapters/        # wfbng, eth, quic
assets/          # fonts, icons, NN models
  configs/       # ini files
  init.d/        # init.d units
  models/        # NN models
```

## 2. Adapter API
```c
typedef struct {
  uint64_t t_us;
  char name[16];
  float rssi_dbm, snr_db, noise_dbm;
  float rtt_ms, jitter_ms, loss_pct;
  uint32_t txrate_kbps, rxrate_kbps;
  uint16_t mtu;
  uint8_t  mcs;
  uint32_t qlen;
} vd_link_metrics_t;

typedef struct vd_adapter_vtbl {
  int (*open)(void *self);
  int (*close)(void *self);
  int (*send)(void *self, const uint8_t *buf, size_t len, int tos, uint64_t deadline_us);
  int (*poll_recv)(void *self, uint8_t *buf, size_t *len, vd_link_metrics_t *opt_m);
  int (*get_metrics)(void *self, vd_link_metrics_t *out);
} vd_adapter_vtbl_t;
```

## 3. FSMs

### Link Manager Path
- DISCOVER → ACTIVE → DEGRADED → FAIL → DISCOVER

### RC Failsafe
- VALID → STALE → FAILSAFE → VALID

## 5. INI Config Example (Drone)
```ini
[general]
node_id = drone-01

[channels.video]
enable = yes
codec = h265
bitrate_kbps = 8000
fps = 60
local_rtp_src = 127.0.0.1:5600

[adapter.wfbng]
enable = yes
type = wfbng_udp
endpoint = 127.0.0.1:5602
```

## 6. INI Config Example (GS)
```ini
[general]
role = gs
node_id = gs-01

[channels.video]
enable = yes
local_rtp_sink = 127.0.0.1:5600

[adapter.wfbng]
enable = yes
type = wfbng_udp
endpoint = 127.0.0.1:5602
```

## 7. INI Config Example (Proxy)
```ini
[general]
listen = 0.0.0.0:443
alpn = vdlink-1

[routing]
pair = drone-01 <-> gs-01
```

## 8. Build & Deployment
- **Build**: CMake, ASAN/UBSAN, unit/fuzz/integration tests.
- **Deployment**:
```bash
vd-link --config /etc/vd-link/drone.ini
vd-link --config /etc/vd-link/gs.ini
vd-proxy --config /etc/vd-link/proxy.ini
```

Systemd units provided in `packaging/systemd`.

## 9. Testing
- Loopback integration test: two vd-link instances (drone+gs) on one host.
- Chaos tests with `tc netem` for packet loss/delay.
- Fuzzing parsers: INI, mux framing, RTP depay.

## 10. Future Extensions
- Multiple video streams (VIDEO#2..N).
- SRT/RIST transport modes.
- Remote firmware update via Config RPC.
- Advanced telemetry dashboards.
