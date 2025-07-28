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

Build
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

Build
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

---
# VD-Link Ground Station
## DRM Display Composer for GS

### Overview

**DRM Display Composer** is a high-performance dual-plane compositor for embedded Linux (tested on Rockchip RK3566/RK3588), designed for hardware-accelerated rendering of NV12 video frames (from DMA-BUF) with overlay ARGB8888 OSD, full atomic page-flipping, and automatic rotation support via RGA.  
It is ideal for real-time video display systems, digital FPV, and OSD overlays.

---

### Features

- **Dual hardware planes:**
    - Video: NV12 (DMA-BUF, zero-copy, supports dynamic stride/size)
    - OSD: ARGB8888 (triple-buffered, CPU or RGA rendered)
- **Atomic commits:** Tear-free updates of both planes in a single transaction
- **Rotation support:** 0/90/180/270° via RGA (auto-detect from device-tree)
- **Aspect ratio & centering:** Video and OSD always scaled and centered with letterbox/pillarbox logic
- **Flexible buffer pools:**
    - Triple-buffered OSD
    - Rotating pool for rotated NV12 frames
    - Up to 16 video buffers, FIFO logic
- **Page-flip event handler:** OSD/frame swaps, callback for animation sync
- **Robust resource cleanup:** All DRM FDs, mmap, DMA-BUFs are correctly managed and released
- **Test patterns:** Built-in rainbow/checkerboard for debugging without real video

---

### Typical Flow

1. **Initialize DRM:**  
   `drm_init("/dev/dri/card0", &cfg);`

2. **Push new OSD frame (ARGB8888):**  
   `drm_push_new_osd_frame(osd_rgba, width, height);`

3. **Push new video frame (NV12 DMA-BUF):**  
   `drm_push_new_video_frame(dma_fd, width, height, hor_stride, ver_stride);`

4. **Optionally set OSD frame-done callback:**  
   `drm_set_osd_frame_done_callback(my_cb);`

5. **Clean up on exit:**  
   `drm_close();`

---

### Work flow

- **Initialization:**
    - Opens DRM device, enables atomic/universal planes
    - Finds connected display, chooses mode/CRTC
    - Finds two hardware planes (NV12 & ARGB8888), fetches all property IDs
    - Detects screen rotation from `/proc/device-tree`
    - Allocates OSD triple-buffer pool

- **Video/OSD update:**
    - For each new frame: OSD buffer is rotated if needed (RGA), marked dirty
    - For each new video DMA-BUF: frame is optionally rotated (RGA, via pool), registered as DRM FB
    - Both planes are committed atomically, with correct z-order and aspect ratio

- **Page flip event:**
    - Swaps buffers, calls user callback if set
    - Issues next atomic commit if new data is available

---

### Code Structure

- `drm_display.c` — main implementation
- `drm_display.h` — API, core structs
- Buffer management: triple OSD, rotating video pool
- RGA integration for rotation/copy

---

### Example Usage

```c
drm_init("/dev/dri/card0", &cfg);

// On each OSD render:
drm_push_new_osd_frame(osd_rgba, 1280, 720);

// On each video frame decode:
drm_push_new_video_frame(video_dma_fd, 1280, 720, 1280, 720);

drm_close();
```
TODO: update another modules