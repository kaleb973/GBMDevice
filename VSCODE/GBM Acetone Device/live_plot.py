import serial
import sys
import time
import matplotlib
matplotlib.use('TkAgg')  # Use TkAgg backend for better interactive tools support
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import csv
import datetime
import threading
import queue
from queue import Empty
from serial.tools import list_ports

BAUD_RATE = 115200

# Global flag for stopping the trial
stop_trial = False
input_queue = queue.Queue()

def read_input():
    """Background thread to read user input without blocking"""
    while not stop_trial:
        try:
            user_input = input()
            input_queue.put(user_input.strip().upper())
        except:
            break

trial_time = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
csv_file = f"trial_{trial_time}.csv"

# List available ports
ports = [port.device for port in list_ports.comports()]
print(f"Available serial ports: {ports if ports else 'None found'}")

# Check if running in remote environment
import os
is_remote = os.path.exists('/.dockerenv') or os.path.exists('/run/.containerenv')
if is_remote and not ports:
    print("\n⚠️  WARNING: Running in remote environment but no USB ports detected!")
    print("   Make sure your devcontainer has USB device mapping configured.")
    print("   See .devcontainer/devcontainer.json for USB passthrough setup.\n")

# Try to auto-detect ESP32 device or allow manual input
SERIAL_PORT = None
if ports:
    # Look for common ESP32/Arduino identifiers
    for port in ports:
        if any(identifier in port.lower() for identifier in ['usb', 'ttyusb', 'ttyacm', 'esp32', 'ch340', 'ftdi']):
            SERIAL_PORT = port
            break
    # If no specific identifier found, ask user
    if not SERIAL_PORT:
        print("\nNo USB device detected automatically.")
        print(f"Available ports: {ports}")
        manual_port = input("Enter the port name (e.g., /dev/ttyUSB0 or COM4) or press Enter to use first port: ").strip()
        SERIAL_PORT = manual_port if manual_port else ports[0]
else:
    print("\nNo serial ports found!")
    if is_remote:
        print("💡 TROUBLESHOOTING FOR REMOTE CODESPACE:")
        print("   1. Verify device is connected to your host machine")
        print("   2. Rebuild container with USB support:")
        print("      - Press Ctrl+Shift+P and run 'Rebuild Container'")
        print("   3. OR manually connect: docker run --device /dev/ttyUSB0:/dev/ttyUSB0")
        print("   4. Check host USB permissions: lsusb")
    manual_port = input("Enter the port name manually (e.g., /dev/ttyUSB0, /dev/ttyACM0, or COM4): ").strip()
    if not manual_port:
        print("No port specified. Exiting.")
        sys.exit(1)
    SERIAL_PORT = manual_port

print(f"Attempting to connect to {SERIAL_PORT} at {BAUD_RATE} baud...")

try:
    print("Opening serial port (Ctrl+C to cancel)...")
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"✓ Successfully connected to {SERIAL_PORT}")
except KeyboardInterrupt:
    print("\nCancelled by user")
    sys.exit()
except FileNotFoundError:
    print(f"❌ Error: Port {SERIAL_PORT} not found!")
    if is_remote:
        print("\n💡 FOR REMOTE CODESPACE:")
        print("   Your device is NOT visible in the container.")
        print("   Steps to fix:")
        print("   1. Rebuild container: Ctrl+Shift+P → 'Rebuild Container'")
        print("   2. Reconnect your device to the host after rebuilding")
        print("   3. Check devcontainer.json has USB mount configuration")
    print(f"\nAvailable ports: {ports}")
    sys.exit(1)
except PermissionError:
    print(f"❌ Error: Permission denied accessing {SERIAL_PORT}")
    if is_remote:
        print("   Try: sudo usermod -a -G dialout $USER")
    sys.exit(1)
except Exception as e:
    print(f"❌ Error opening serial port {SERIAL_PORT}: {e}")
    print(f"Available ports: {ports}")
    sys.exit(1)

print("Waiting 2 seconds...")
time.sleep(2)

# --- Start background input thread immediately ---
input_thread = threading.Thread(target=read_input, daemon=True)
input_thread.start()

print("=== Connected to GBM Acetone Device ===")
print("Commands: Type 'START TEST' to begin trial | 'INJECT' for vapor injection | 'STOP' to end | 'PAUSE'/'RESUME' during trial")
print(f"Data will be saved to: {csv_file}")
print("Opening graph window...")

# --- Matplotlib figure with professional styling ---
plt.style.use('seaborn-v0_8-darkgrid')
fig, ax1 = plt.subplots(figsize=(14, 8))
fig.suptitle('GBM Acetone Device - Real-Time Sensor Data', fontsize=18, fontweight='bold', y=0.98)

# Add padding to prevent legend from overlapping graph
fig.subplots_adjust(left=0.08, right=0.88, top=0.92, bottom=0.1)

# Temperature axis (red)
ax1.set_xlabel("Time (seconds)", fontsize=13, fontweight='bold')
ax1.set_ylabel("Temperature (°C)", color="#d62728", fontsize=13, fontweight='bold')
line_temp, = ax1.plot([], [], color="#d62728", linewidth=2.5, label="Temperature", marker='o', markersize=3, markevery=10)
ax1.tick_params(axis='y', labelcolor="#d62728", labelsize=11)
ax1.tick_params(axis='x', labelsize=11)
ax1.grid(True, alpha=0.3)

# Humidity axis (blue)
ax2 = ax1.twinx()
ax2.set_ylabel("Humidity (%)", color="#1f77b4", fontsize=13, fontweight='bold')
line_hum, = ax2.plot([], [], color="#1f77b4", linewidth=2.5, label="Humidity", marker='s', markersize=3, markevery=10)
ax2.tick_params(axis='y', labelcolor="#1f77b4", labelsize=11)

# MQ138 axis (green)
ax3 = ax1.twinx()
ax3.spines['right'].set_position(('outward', 70))
ax3.set_ylabel("MQ138 Sensor Output (a.u.)", color="#2ca02c", fontsize=13, fontweight='bold')
line_mq, = ax3.plot([], [], color="#2ca02c", linewidth=2.5, label="MQ138 Gas Sensor", marker='^', markersize=3, markevery=10)
ax3.tick_params(axis='y', labelcolor="#2ca02c", labelsize=11)

# Improve spine visibility
ax1.spines['left'].set_color('#d62728')
ax1.spines['left'].set_linewidth(2)
ax2.spines['right'].set_color('#1f77b4')
ax2.spines['right'].set_linewidth(2)
ax3.spines['right'].set_color('#2ca02c')
ax3.spines['right'].set_linewidth(2)

times, temps, hums, mq = [], [], [], []

csvfile = open(csv_file, 'w', newline='')
csvwriter = csv.writer(csvfile)
csvwriter.writerow(['Time(s)', 'Temperature(C)', 'Humidity(%)', 'MQ138(a.u.)'])

current_phase = "WAITING"
baseline_value = 0
baseline_established = False

# Phase change tracking
phase_changes = []  # List of (time, phase_name) tuples
phase_lines = []    # List of vertical line objects

# Professional legend box with current values
legend_text = ax1.text(0.02, 0.98, "", transform=ax1.transAxes, fontsize=10, fontweight='bold',
                       verticalalignment='top', horizontalalignment='left',
                       bbox=dict(boxstyle='round,pad=0.6', facecolor='white', edgecolor='black', alpha=0.95, linewidth=2))

def update(frame):
    global times, temps, hums, mq, stop_trial, current_phase, baseline_value, baseline_established, phase_changes, phase_lines
    
    # Skip update if user is interacting with toolbar
    if fig.canvas.toolbar and fig.canvas.toolbar.mode:
        return line_temp, line_hum, line_mq, legend_text
    
    try:
        # Check for user commands
        while True:
            try:
                command = input_queue.get_nowait()
            except:
                # Queue is empty, move on
                break
                
            if command == "STOP" or command == "EXIT" or command == "QUIT":
                print("Master: Stop command received. Ending trial...")
                ser.write(b'STOP\n')
                stop_trial = True
                plt.close()
                return line_temp, line_hum, line_mq, legend_text
            elif command == "START TEST":
                print("Master: Starting trial...")
                ser.write(b'START TEST\n')
            elif command == "INJECT":
                print("Master: Injecting vapor...")
                ser.write(b'INJECT\n')
            elif command == "PAUSE":
                print("Master: Trial paused. Type 'RESUME' to continue or 'STOP' to end.")
                ser.write(b'PAUSE\n')
            elif command == "RESUME":
                print("Master: Resuming trial...")
                ser.write(b'RESUME\n')
        
        # Read serial data
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue
            
            # Check for baseline establishment message
            if "Baseline established:" in line or "Baseline (max time):" in line:
                try:
                    baseline_value = int(line.split(':')[-1].strip())
                    baseline_established = True
                    print(f"Baseline captured: {baseline_value}")
                except:
                    pass
                continue
            
            try:
                t, temp, hum, mqv, phase = line.split(',')
                t = float(t)/1000.0
                temp = float(temp)
                hum = float(hum)
                mqv = int(mqv)  # This is corrected value from Arduino
                new_phase = phase.strip()
                
                # Detect phase change and mark it
                if new_phase != current_phase and times:
                    phase_changes.append((t, new_phase))
                    # Add vertical line at phase change
                    vline = ax1.axvline(x=t, color='red', linestyle='--', linewidth=2, alpha=0.7)
                    phase_lines.append(vline)
                    # Add text label
                    ax1.text(t, ax1.get_ylim()[1], f' {new_phase}', 
                            rotation=90, verticalalignment='top', 
                            fontsize=9, color='red', fontweight='bold')
                
                current_phase = new_phase

                times.append(t)
                temps.append(temp)
                hums.append(hum)
                mq.append(mqv)

                csvwriter.writerow([t, temp, hum, mqv])
            except:
                continue

        # Update plot data
        line_temp.set_data(times, temps)
        line_hum.set_data(times, hums)
        line_mq.set_data(times, mq)

        if times:
            # Set x-axis range (60-second window, with padding)
            ax1.set_xlim(max(0, times[-1]-60), times[-1]+5)
            
            # Set y-axis ranges with smart scaling
            if temps:
                temp_min, temp_max = min(temps), max(temps)
                temp_range = temp_max - temp_min
                if temp_range < 2:  # If range is too small, use fixed padding
                    temp_range = 2
                ax1.set_ylim(temp_min - temp_range*0.1, temp_max + temp_range*0.1)
            
            if hums:
                hum_min, hum_max = min(hums), max(hums)
                hum_range = hum_max - hum_min
                if hum_range < 5:
                    hum_range = 5
                ax2.set_ylim(max(0, hum_min - hum_range*0.1), min(100, hum_max + hum_range*0.1))
            
            if mq:
                mq_min, mq_max = min(mq), max(mq)
                mq_range = mq_max - mq_min
                if mq_range < 100:
                    mq_range = 100
                ax3.set_ylim(max(0, mq_min - mq_range*0.1), mq_max + mq_range*0.1)

        # Update legend with statistics
        if temps and hums and mq:
            elapsed_time = times[-1] if times else 0
            raw_mq = mq[-1] + baseline_value if baseline_established else mq[-1]
            baseline_status = f"YES ({baseline_value})" if baseline_established else "Not Set"
            
            legend_text.set_text(
                f"=== PHASE: {current_phase} ===\n"
                f"Temp: {temps[-1]:.2f}°C\n"
                f"Humidity: {hums[-1]:.2f}%\n"
                f"MQ138 (corrected): {mq[-1]} a.u.\n"
                f"MQ138 (raw): {raw_mq} a.u.\n"
                f"Baseline: {baseline_status}\n"
                f"Elapsed: {elapsed_time:.1f}s\n"
                f"-------------------\n"
                f"Min/Max (corrected):\n"
                f"T: {min(temps):.1f}°C / {max(temps):.1f}°C\n"
                f"H: {min(hums):.1f}% / {max(hums):.1f}%\n"
                f"MQ: {min(mq)} / {max(mq)} a.u."
            )

        return line_temp, line_hum, line_mq, legend_text
    except Exception as e:
        # Silently handle any remaining exceptions to prevent animation crashes
        return line_temp, line_hum, line_mq, legend_text

ani = FuncAnimation(fig, update, interval=100, blit=False, cache_frame_data=False, save_count=None)
plt.show()

stop_trial = True
csvfile.close()
ser.close()
print("Master: Trial ended. Data saved to " + csv_file)
input("Press Enter to exit…")