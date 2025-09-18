# RV1126 & RK3566 SDK Docker Environment

This repository provides a clean Docker-based development environment for **RK3566** & **RV1126** using a **Buildroot SDK.** It supports usage both via command-line and through **CLion Docker Toolchain** integration.

---

## 📥 Prerequisites

1. Download the SDK archive for RK3566: [aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz](https://gitlab.hard-tech.org.ua/-/project/54/uploads/e61180e057be710362a4255e997cd603/aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz)
2. Download the SDK archive for RV1126: [vision-sdk.tar.gz](https://gitlab.hard-tech.org.ua/-/project/2/uploads/2a38fb33f9dc972ef00b15b8155399ef/vision-sdk.tar.gz)
3. Extract it into the same directory as the `Dockerfile.*`:

```bash
tar -xf aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz
tar -xf vision-sdk.tar.gz
```
You should now have:
```bash
./aarch64-buildroot-linux-gnu_sdk-buildroot/
./vision-sdk/
./Dockerfile.rk3566
./Dockerfile.rv1126
./docker.sh
```

## 🐳 Build & Run with create-docker.sh

Use the helper script to build the image, create a container, and optionally run commands inside.

Run with arguments: ```rk3566` or `rv1126` to specify the target platform.

```bash
./docker.sh rv1126
# or
./docker.sh rk3566
```
`These will execute the command inside the container, then stop and remove it afterward.`

## CLion Integration

You can use this image in CLion via Docker Toolchain:
•	Image name: image-rk3566 or image-rv1126
•	Container mount path: /workspace
•	Working directory: /workspace
•	Environment setup: handled automatically via /entry.sh inside the image

### CLion Settings RK3566
![CLion Settings](png/1.png)

![CLion Settings](png/3.png)

![CLion Settings](png/5.png)

### CLion Settings RV1126
![CLion Settings](png/2.png)

![CLion Settings](png/4.png)

![CLion Settings](png/6.png)

### Run/Debug Configuration via remote GDB Server
![Run/Debug Configuration](png/7.png)

## Cleanup
To manually remove everything:

```bash
docker rm -f container-rk3566
docker rmi image-rk3566

docker rm -f container-rv1126
docker rmi image-rv1126
```

## VSCode Integration

VD-Link provides pre-configured workspace files for native VSCode development without Docker.

### Workspace Files

The project includes two workspace configurations in the root directory:
- `vd-link-drone.code-workspace` - For drone development (RV1126)
- `vd-link-gs.code-workspace` - For ground station development (RK3566)

### Setup Instructions

1. **Install Required Extensions:**
   - C/C++ Extension Pack
   - CMake Tools

2. **Install Native SDKs:**
   ```bash
   # For RK3566 (Ground Station)
   # Extract SDK to /opt/sdk-rk3566/
   # Run: /opt/sdk-rk3566/relocate-sdk.sh
   
   # For RV1126 (Drone)
   # Extract SDK to /opt/sdk-rv1126/
   # Run: /opt/sdk-rv1126/relocate-sdk.sh
   ```

3. **Open Workspace:**
   ```bash
   # For drone development
   code vd-link-drone.code-workspace
   
   # For ground station development
   code vd-link-gs.code-workspace
   ```

### Development Workflow

1. **Source SDK Environment:**
   ```bash
   # For drone
   source /opt/sdk-rv1126/environment-setup
   
   # For ground station
   source /opt/sdk-rk3566/environment-setup
   ```

2. **Open VSCode Workspace:**
   - Use the appropriate workspace file for your target platform
   - VSCode will use the native toolchain configuration

3. **Build Project:**
   - Use `Ctrl+Shift+P` → `CMake: Configure`
   - Use `Ctrl+Shift+P` → `CMake: Build`
   - Or use the build buttons in the status bar

### Features

- **IntelliSense:** Fully configured for cross-compilation toolchain
- **Native Performance:** Direct access to toolchain without Docker overhead
- **Build Integration:** Seamless CMake integration with native SDK
- **Platform Isolation:** Separate workspaces prevent configuration conflicts

### Troubleshooting

- **Toolchain not found:** Ensure SDK is installed and environment is sourced
- **Include errors:** Verify the correct workspace file is opened for your target platform
- **Build failures:** Check that SDK is properly relocated and environment variables are set

````
