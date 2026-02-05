# GBM Acetone Device - Setup Instructions

## Online Development (GitHub Codespaces)

### Getting Started
1. Open the repository in GitHub Codespaces
2. Codespaces automatically has Python and necessary tools installed
3. Start editing files directly in the browser or VSCode

### Running Locally from Codespaces
```bash
# Install dependencies
pip install -r requirements.txt

# Run the live plot script
cd VSCODE/GBM\ Acetone\ Device
python live_plot.py
```

**Note:** Serial port detection will work if your device is USB-forwarded to the container.

---

## Local Development Setup

### Prerequisites
- Python 3.8+ installed on your machine
- Git installed
- Your GBM Acetone Device (Arduino board with USB connection)

### Installation Steps

1. **Clone the repository**
   ```bash
   git clone https://github.com/kaleb973/GBMDevice.git
   cd GBMDevice
   ```

2. **Create a Python virtual environment** (optional but recommended)
   ```bash
   # Windows
   python -m venv venv
   venv\Scripts\activate

   # macOS/Linux
   python3 -m venv venv
   source venv/bin/activate
   ```

3. **Install dependencies**
   ```bash
   pip install -r requirements.txt
   ```

### Running the Live Plot

```bash
cd VSCODE/GBM\ Acetone\ Device
python live_plot.py
```

The script will:
- Auto-detect your Arduino/ESP32 device on available USB ports
- Prompt you to select a port if multiple are found
- Connect at 115200 baud rate
- Start live plotting sensor data

### Uploading Arduino Code

1. Open Arduino IDE
2. Load `VSCODE/GBM Acetone Device/Arduino_code.cpp`
3. Select your board type and COM port
4. Click Upload

---

## Syncing Between Online & Local

### Push local changes to GitHub
```bash
git add .
git commit -m "Your message"
git push
```

### Pull changes from Codespaces
```bash
git pull
```

**Best Practice:** Before starting work, run `git pull` to get the latest changes.

---

## Troubleshooting

### Serial Port Issues
- **Windows**: Check Device Manager for COM port number
- **macOS/Linux**: Use `ls /dev/tty*` to list ports
- **Not detected in Codespaces?**: Rebuild the container with USB support

### Import Errors
```bash
# Make sure you activated the virtual environment, then reinstall
pip install --upgrade -r requirements.txt
```

### Permission Denied (Linux/macOS)
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Then log out and back in
```

---

## Project Structure
```
GBMDevice/
├── README.md                          # Project overview
├── requirements.txt                   # Python dependencies
├── SETUP_INSTRUCTIONS.md              # This file
├── VSCODE/
│   └── GBM Acetone Device/
│       ├── Arduino_code.cpp           # Firmware for the device
│       ├── live_plot.py               # Main data plotting script
│       └── Test Pump.cpp              # Pump testing code
└── trial_*.csv                        # Data files (generated)
```
