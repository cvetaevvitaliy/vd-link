# VD-Link

**VD-Link** is a high-performance real-time video system focused on low-latency digital FPV streaming with hardware-accelerated video decoding and dynamic On-Screen Display (OSD) based on MSP Displayport.



> This project uses **[MSP-OSD](https://github.com/fpv-wtf/msp-osd)** for rendering overlay graphics. Fonts and drawing logic are adapted, but VD-Link is an independent system, not a port or fork of MSP-OSD.

## Target Hardware & Platform

VD-Link is **developed and tested on Rockchip RK3566, RV1126 SoC**.

> VD-Link can be built on other Rockchip SoCs.

Confirmed working on:

- **[Radxa ZERO 3W](https://radxa.com/products/zeros/zero3w/)** — small SBC with RK3566
- **Powkiddy X55** — budget handheld gaming console with 1280x720 DSI display, RK3566, 2GB RAM, 8GB eMMC
- **RV1126-based vision board** - small SBC with RV1126 SoC, 512MB RAM, 8GB eMMC, 30x30mm form factor, 2x CSI cameras
---

## Features

- Hardware-accelerated video decoding: H.264/H.265
- Zero-copy rendering via MPP -> RGA (for rotate frame) -> DRM (Rockchip)
- OSD overlays using PNG fonts and MSP Display Port
- Font system with support for **Betaflight/INAV/ArduPilot** styles

---

## How to Build

### Ground station
### Prerequisites
- Download SDK archive: [aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz](https://gitlab.hard-tech.org.ua/-/project/54/uploads/e61180e057be710362a4255e997cd603/aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz)
- Extract  to `/opt/sdk-rk3566/`
- Relocate SDK `/opt/sdk-rk3566/relocate-sdk.sh`

```bash
source /opt/sdk-rk3566/environment-setup
cd /path/to/vd-link
mkdir build-gs
cd build-gs
cmake -DPLATFORM=gs ..
make

```

### Drone
### Prerequisites
- Download SDK archive: [vision-sdk.tar.gz](https://gitlab.hard-tech.org.ua/-/project/2/uploads/2a38fb33f9dc972ef00b15b8155399ef/vision-sdk.tar.gz)
- Extract to `/opt/sdk-rv1126/`
- Relocate SDK `/opt/sdk-rv1126/relocate-sdk.sh`

```bash
source /opt/sdk-rv1126/environment-setup
cd /path/to/vd-link
mkdir build-drone
cd build-drone
cmake -DPLATFORM=drone ..
make
```

Docker build see [docker/readme.md](docker/readme.md)

### External Projects
- OSD drawing: [MSP-OSD](https://github.com/fpv-wtf/msp-osd)
- RTP stack: [media-server](https://github.com/ireader/media-server)
