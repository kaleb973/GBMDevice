# GBM Device - Remote Codespace Setup Guide

## The Problem
COM ports (or `/dev/ttyUSB0` in Linux) don't work in remote codespaces by default because containers don't have access to USB devices.

## The Solution

### 1. **Automatic Setup (Recommended)**
The `.devcontainer/devcontainer.json` now includes USB device mapping. Just:
- Open VS Code command palette: `Ctrl+Shift+P`
- Run: **"Rebuild Container"**
- Reconnect your USB device to the host machine

### 2. **What This Does**
```json
"mounts": [
  "type=bind,source=/dev/bus/usb,target=/dev/bus/usb,readonly",
  "type=bind,source=/run/udev,target=/run/udev,readonly"
],
"runArgs": [
  "--device-cgroup-rule=a 189:* rmw",
  "--group-add=dialout"
]
```

- **Mounts USB buses** from host into container
- **Adds dialout group** so vscode user can access serial ports
- **Allows device access** with proper permissions

### 3. **Verify It Works**
In terminal, run:
```bash
# Check for USB devices
ls -la /dev/tty* | grep -E "USB|ACM"

# List all connected ports
python -c "from serial.tools import list_ports; print([p.device for p in list_ports.comports()])"
```

### 4. **Troubleshooting**

**No devices showing up?**
- Reconnect your USB device after rebuilding container
- Check host machine can see device: `lsusb`
- Try rebuilding container again

**Permission denied error?**
- Container already includes `dialout` group, shouldn't happen
- If it does: `sudo usermod -a -G dialout vscode` in container

**Still not working?**
- Ensure device is connected to **host machine**, not container VM
- For GitHub Codespaces: USB forwarding may need additional setup
- Check if your device uses CH340 or FTDI drivers (script auto-detects these)

## Running the Script

```bash
cd "/workspaces/GBMDevice/VSCODE/GBM Acetone Device"
python live_plot.py
```

The script will:
1. ✓ Auto-detect your ESP32/Arduino device
2. ✓ Show helpful errors if running in remote environment
3. ✓ Guide you through manual port selection if needed
4. ✓ Start real-time plotting of sensor data

## Environment Detection
The updated script automatically detects if it's running in a container and provides targeted troubleshooting hints for remote codespaces.
