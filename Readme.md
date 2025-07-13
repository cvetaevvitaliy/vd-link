# VD-Link

**VD-Link** is a high-performance real-time video system focused on low-latency digital FPV streaming with hardware-accelerated video decoding and dynamic On-Screen Display (OSD) based on MSP Displayport.



> This project uses **[MSP-OSD](https://github.com/fpv-wtf/msp-osd)** for rendering overlay graphics. Fonts and drawing logic are adapted, but VD-Link is an independent system, not a port or fork of MSP-OSD.

## Target Hardware & Platform

VD-Link is **developed and tested on Rockchip RK3566 SoC**.

> VD-Link can be built on other Rockchip SoCs.

Confirmed working on:

- **[Radxa ZERO 3W](https://radxa.com/products/zeros/zero3w/)** — small SBC with RK3566
- **Powkiddy X55** — budget handheld gaming console with 1280x720 DSI display, RK3566, 2GB RAM, 8GB eMMC
---

## Features

- Hardware-accelerated video decoding: H.264/H.265
- Zero-copy rendering via MPP -> RGA (for rotate frame) -> DRM (Rockchip)
- OSD overlays using PNG fonts and MSP Display Port
- Font system with support for **Betaflight/INAV/ArduPilot** styles

---

## How to Build

### Prerequisites
- Download SDK archive: [aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz](https://gitlab.hard-tech.org.ua/-/project/54/uploads/e61180e057be710362a4255e997cd603/aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz)
- Extract  to `/opt/sdk/`

Build
```bash
source /opt/sdk/environment-setup
cd /path/to/vd-link
mkdir build
cd build
cmake ..
make

```

Docker build see [docker/readme.md](docker/readme.md)

### External Projects
- OSD drawing: [MSP-OSD](https://github.com/fpv-wtf/msp-osd)
- RTP stack: [media-server](https://github.com/ireader/media-server)
