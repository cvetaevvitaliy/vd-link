# High-Level Design (HLD): UAV Digital Video Link

## 1. Goals & Scope
- **Low latency** video streaming (≤120 ms end-to-end).
- **Reliable RC uplink** with failsafe handling.
- **Flexible transport**: WFB-NG, Ethernet, Fiber, RNDIS, Cellular.
- **Modularity**: shared codebase for Drone and Ground Station, one daemon `vd-link`.
- **Real-time reconfiguration**: config changes via RPC.
- **Telemetry**: link quality metrics available on GS.

## 2. System Overview

*Note: QUIC protocol is not yet implemented.*

```
Drone:
  VD-Link [Camera → ISP → Encoder → RTP → ] → Adapter(s)
  FC ↔ UART (MSP RC, OSD)
  Channels: Video, OSD, RC uplink, RPC, Telemetry

Ground Station:
  Adapters → VD-Link → [ → Video decoder → Display (DRI)]
  OSD overlay compositor
  HID joystick input → MSP RC uplink
  Config UI/CLI to Drone
  Telemetry exporter (maybe use Prometheus)
```

## 3. Channels
- **VIDEO**: RTP/UDP (UDP, SRTP or QUIC datagrams).
- **OSD**: UDP or RTP substream, synchronized with video.
- **RC**: MSP RC uplink over QUIC stream or UDP (WFB-NG direct).
- **CONFIG**: protobuf over QUIC stream.
- **LINK-TELEM**: link metrics periodically via UDP or QUIC datagrams.

## 4. Link Manager (Virtual Link)
- Bonding (per-packet, reorder window 1–2 frames).
- Failover policies: LowLatency, Balanced, HighReliability.
- DSCP/TOS marking for QoS (Video=EF, RC=CS5, OSD=AF41, Config=AF21, Telem=CS1).
- MTU discovery, FEC (Raptor/ULP), adaptive bitrate.

## 5. Security
- Video channel must be encrypted to avoid compromising the drone launch location.
- RC/Config/Telemetry must be authenticated and encrypted to avoid the MIM attack.
- QUIC/TLS1.3 with ALPN `vdlink-1` for RC/Config/Telemetry.
- SRTP (DTLS-SRTP) or QUIC datagrams for video.
- Key bootstrap via Config RPC, session key rotation, cert pinning.

## 6. Deployment
### Drone
- One daemon `vd-link-drone`, INI-based config.
- Separate `vd-adapter`.
- init.d units: `S90vd-link`, `S91vd-adapter`.
### Ground Station
- One daemon `vd-link-gs`, INI-based config.
- init.d unit: `S90vd-link`, `S91vd-adapter`.

## 7. Time Synchronization
- RTCP SR/RR for RTP streams.
- QUIC timestamps for RC/OSD alignment.
- Target OSD-video skew < 20 ms.

## 8. Diagrams

### Overall Architecture
```mermaid
flowchart LR
  subgraph Drone
    ENC[Video Encoder] --> VDL[VD-Link]
    OSD[OSD Generator] --> VDL
    RCIN[RC In (UART MSP)] <--> VDL
    CFGD[Config Daemon] <--> VDL
    LMON[Link Monitor] --> VDL
    VDL --> WFB[WFB-NG Adapter]
    VDL --> ETH[Ethernet/Fiber Adapter]
    VDL --> CELL[Cellular QUIC Adapter]
  end

  subgraph GroundStation
    GWFB[WFB-NG Adapter] --> GVDL[VD-Link]
    GETH[Ethernet Adapter] --> GVDL
    GCELL[Cellular Adapter] --> GVDL
    GVDL --> VRX[Video Decoder]
    GVDL --> OSDM[OSD Mixer]
    HID[HID Controller] --> GVDL
    GVDL <--> CFGUI[Config UI/CLI]
    GVDL --> METR[Telemetry Export]
  end
```

### Cellular NAT Traversal
```
Drone ──QUIC──> Proxy <──QUIC── GS
 (outbound)             (outbound)
```

