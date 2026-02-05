import serial
import sys
import time
import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import csv
import datetime
import threading
import queue
from queue import Empty
from serial.tools import list_ports
import os

BAUD_RATE = 115200

stop_trial = False
input_queue = queue.Queue()

def read_input():
    while not stop_trial:
        try:
            user_input = input()
            input_queue.put(user_input.strip().upper())
        except:
            break

# --- Ask for concentration + trial label so filenames auto-group for analysis ---
conc = input("Enter concentration (e.g., 300ppb, 900ppb, 1.2ppm): ").strip().lower()
trial_label = input("Enter trial label (e.g., T1, T2, T3): ").strip().upper()

trial_time = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
csv_file = f"{conc}_{trial_label}_{trial_time}.csv"

# List available ports
ports = [port.device for port in list_ports.comports()]
print(f"Available serial ports: {ports if ports else 'None found'}")

is_remote = os.path.exists('/.dockerenv') or os.path.exists('/run/.containerenv')
if is_remote and not ports:
    print("\n⚠️  WARNING: Running in remote environment but no USB ports detected!")
    print("   Make sure your devcontainer has USB device mapping configured.\n")

SERIAL_PORT = None
if ports:
    for port in ports:
        if any(identifier in port.lower() for identifier in ['usb', 'ttyusb', 'ttyacm', 'esp32', 'ch340', 'ftdi']):
            SERIAL_PORT = port
            break
    if not SERIAL_PORT:
        print("\nNo USB device detected automatically.")
        print(f"Available ports: {ports}")
        manual_port = input("Enter port (or Enter to use first): ").strip()
        SERIAL_PORT = manual_port if manual_port else ports[0]
else:
    print("\nNo serial ports found!")
    manual_port = input("Enter port manually (e.g., /dev/ttyUSB0 or COM4): ").strip()
    if not manual_port:
        print("No port specified. Exiting.")
        sys.exit(1)
    SERIAL_PORT = manual_port

print(f"Attempting to connect to {SERIAL_PORT} at {BAUD_RATE} baud...")

try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"✓ Connected to {SERIAL_PORT}")
except Exception as e:
    print(f"❌ Error opening serial port {SERIAL_PORT}: {e}")
    sys.exit(1)

print("Waiting 2 seconds...")
time.sleep(2)

input_thread = threading.Thread(target=read_input, daemon=True)
input_thread.start()

print("=== Connected to GBM Acetone Device ===")
print("Commands: START TEST | INJECT | PAUSE | RESUME | STOP")
print(f"Data will be saved to: {csv_file}")
print("Opening graph window...")

plt.style.use('seaborn-v0_8-darkgrid')
fig, ax1 = plt.subplots(figsize=(14, 8))
fig.suptitle('GBM Acetone Device - Real-Time Sensor Data', fontsize=18, fontweight='bold', y=0.98)
fig.subplots_adjust(left=0.08, right=0.88, top=0.92, bottom=0.1)

# Temperature axis
ax1.set_xlabel("Time (seconds)", fontsize=13, fontweight='bold')
ax1.set_ylabel("Temperature (°C)", color="#d62728", fontsize=13, fontweight='bold')
line_temp, = ax1.plot([], [], color="#d62728", linewidth=2.5, label="Temperature",
                      marker='o', markersize=3, markevery=10)
ax1.tick_params(axis='y', labelcolor="#d62728", labelsize=11)
ax1.tick_params(axis='x', labelsize=11)
ax1.grid(True, alpha=0.3)

# Humidity axis
ax2 = ax1.twinx()
ax2.set_ylabel("Humidity (%)", color="#1f77b4", fontsize=13, fontweight='bold')
line_hum, = ax2.plot([], [], color="#1f77b4", linewidth=2.5, label="Humidity",
                     marker='s', markersize=3, markevery=10)
ax2.tick_params(axis='y', labelcolor="#1f77b4", labelsize=11)

# MQ axis
ax3 = ax1.twinx()
ax3.spines['right'].set_position(('outward', 70))
ax3.set_ylabel("MQ138 Sensor Output (a.u.)", color="#2ca02c", fontsize=13, fontweight='bold')
line_mq, = ax3.plot([], [], color="#2ca02c", linewidth=2.5, label="MQ138 Gas Sensor",
                    marker='^', markersize=3, markevery=10)
ax3.tick_params(axis='y', labelcolor="#2ca02c", labelsize=11)

ax1.spines['left'].set_color('#d62728'); ax1.spines['left'].set_linewidth(2)
ax2.spines['right'].set_color('#1f77b4'); ax2.spines['right'].set_linewidth(2)
ax3.spines['right'].set_color('#2ca02c'); ax3.spines['right'].set_linewidth(2)

times, temps, hums, mq = [], [], [], []
phase_transitions = []  # Store (time, phase_name) for markers

csvfile = open(csv_file, 'w', newline='')
csvwriter = csv.writer(csvfile)
# --- IMPORTANT: include Phase column for automation ---
csvwriter.writerow(['Time(s)', 'Temperature(C)', 'Humidity(%)', 'MQ138(a.u.)', 'Phase'])

current_phase = "WAITING"
baseline_value = 0
baseline_established = False

# Pump compensation tracking
pump_state = {"main": False, "flow": False}  # Track pump states
pump_offset = 0  # Offset to apply when pumps change
LAST_MQ_BEFORE_PUMP = None  # MQ value just before pump change
PUMP_COMPENSATION = True  # Enable/disable compensation

legend_text = ax1.text(
    0.02, 0.98, "", transform=ax1.transAxes, fontsize=10, fontweight='bold',
    verticalalignment='top', horizontalalignment='left',
    bbox=dict(boxstyle='round,pad=0.6', facecolor='white', edgecolor='black', alpha=0.95, linewidth=2)
)

def update(frame):
    global stop_trial, current_phase, baseline_value, baseline_established
    global pump_state, pump_offset, LAST_MQ_BEFORE_PUMP

    if fig.canvas.toolbar and fig.canvas.toolbar.mode:
        return line_temp, line_hum, line_mq, legend_text

    # Process commands
    try:
        while True:
            try:
                command = input_queue.get_nowait()
            except:
                break

            if command in ("STOP", "EXIT", "QUIT"):
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
                print("Master: Paused.")
                ser.write(b'PAUSE\n')
            elif command == "RESUME":
                print("Master: Resuming.")
                ser.write(b'RESUME\n')
    except:
        pass

    # Read serial
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line:
            continue

        # Baseline messages
        if "Baseline established:" in line or "Baseline (max time):" in line:
            try:
                baseline_value = int(line.split(':')[-1].strip())
                baseline_established = True
                print(f"Baseline captured: {baseline_value}")
            except:
                pass
            continue

        # Parse CSV-like data line
        try:
            t, temp, hum, mqv, phase = line.split(',')
            t = float(t) / 1000.0
            temp = float(temp)
            hum = float(hum)
            mqv = int(mqv)  # already corrected (or raw before baseline in your Arduino)
            new_phase = phase.strip()

            # Track phase transitions for markers
            if new_phase != current_phase:
                phase_transitions.append((t, new_phase))
                print(f"Phase transition detected: {current_phase} -> {new_phase} at {t:.1f}s")
                
                # Reset compensation when leaving BASELINE phase (transition to real test)
                if current_phase == "BASELINE" and new_phase == "WAITING_FOR_INJECT":
                    pump_offset = 0
                    LAST_MQ_BEFORE_PUMP = None
                    pump_state = {"main": False, "flow": False}
                    print("Transition to WAITING_FOR_INJECT - resetting pump compensation")
            
            current_phase = new_phase
            
            # Determine pump states based on phase (from Arduino code)
            new_pump_state = {"main": False, "flow": False}
            if new_phase in ["WARMUP", "BASELINE", "FLUSH"]:
                new_pump_state["main"] = True
                new_pump_state["flow"] = True
            elif new_phase == "VAPOR_INJECTION":
                new_pump_state["main"] = True
                new_pump_state["flow"] = False
            elif new_phase == "MEASUREMENT":
                new_pump_state["main"] = True
                new_pump_state["flow"] = True
            # WAITING_FOR_INJECT, ADSORBTION, HEATING, READY: both pumps off
            
            # Only apply compensation AFTER baseline phase (skip during BASELINE and WAITING_FOR_INJECT)
            if PUMP_COMPENSATION and new_phase not in ["BASELINE", "WAITING_FOR_INJECT"] and mq:
                pump_changed = (new_pump_state["main"] != pump_state["main"] or 
                               new_pump_state["flow"] != pump_state["flow"])
                
                if pump_changed and len(mq) > 0:
                    # Pumps ON (power draw) suppress MQ reading -> subtract offset
                    # Pumps OFF (no power draw) let MQ spike -> add offset
                    # So: if pumps turning ON, reduce future readings by adding negative offset
                    #     if pumps turning OFF, readings will spike, so subtract that spike
                    
                    pump_effect = 0
                    pumps_turning_on = False
                    
                    if new_pump_state["main"] and not pump_state["main"]:
                        pumps_turning_on = True
                        pump_effect -= 80  # Main pump turning ON suppresses reading
                    elif not new_pump_state["main"] and pump_state["main"]:
                        pump_effect += 80  # Main pump turning OFF causes spike
                    
                    if new_pump_state["flow"] and not pump_state["flow"]:
                        pumps_turning_on = True
                        pump_effect -= 60  # Flow pump turning ON suppresses reading
                    elif not new_pump_state["flow"] and pump_state["flow"]:
                        pump_effect += 60  # Flow pump turning OFF causes spike
                    
                    if pump_effect != 0:
                        pump_offset += pump_effect
                        print(f"Pump change detected: Compensation applied: {pump_effect:+d} (total offset: {pump_offset:+d})")
                    
                    LAST_MQ_BEFORE_PUMP = mqv
                    pump_state = new_pump_state.copy()
                else:
                    LAST_MQ_BEFORE_PUMP = mqv
                    pump_state = new_pump_state.copy()
            elif new_phase in ["BASELINE", "WAITING_FOR_INJECT"]:
                # During BASELINE and WAITING_FOR_INJECT, track pump state but don't apply compensation
                pump_state = new_pump_state.copy()
            
            # Apply pump compensation to MQ value (only after baseline and waiting)
            mqv_compensated = mqv - pump_offset if (PUMP_COMPENSATION and new_phase not in ["BASELINE", "WAITING_FOR_INJECT"]) else mqv

            times.append(t)
            temps.append(temp)
            hums.append(hum)
            mq.append(mqv_compensated)

            # --- IMPORTANT: save phase to CSV ---
            csvwriter.writerow([t, temp, hum, mqv_compensated, new_phase])
        except:
            continue

    # Update plot
    line_temp.set_data(times, temps)
    line_hum.set_data(times, hums)
    line_mq.set_data(times, mq)
    
    # Draw phase transition markers
    # Clear old markers and redraw (simple approach)
    for line in ax1.get_lines():
        if hasattr(line, '_phase_marker'):
            line.remove()
    
    # Also clear old text annotations (labels)
    for text in ax1.texts[:]:
        if hasattr(text, '_phase_label'):
            text.remove()
    
    if times and phase_transitions:
        xlim = ax1.get_xlim()
        for t_phase, phase_name in phase_transitions:
            if times and xlim[0] <= t_phase <= xlim[1]:
                # Draw vertical line for phase transition
                phase_line = ax1.axvline(t_phase, color='gray', linestyle='--', 
                                        linewidth=1.5, alpha=0.7, zorder=1)
                phase_line._phase_marker = True  # Tag so we can remove later
                
                # Add phase label
                ylim = ax1.get_ylim()
                y_pos = ylim[0] + (ylim[1] - ylim[0]) * 0.95
                phase_text = ax1.text(t_phase, y_pos, phase_name, rotation=90, 
                        verticalalignment='top', horizontalalignment='right',
                        fontsize=8, color='gray', alpha=0.8,
                        bbox=dict(boxstyle='round,pad=0.3', facecolor='white', 
                                 edgecolor='gray', alpha=0.6))
                phase_text._phase_label = True  # Tag so we can remove later

    if times:
        ax1.set_xlim(max(0, times[-1]-60), times[-1]+5)

        if temps:
            temp_min, temp_max = min(temps), max(temps)
            temp_range = max(2, temp_max - temp_min)
            ax1.set_ylim(temp_min - temp_range*0.1, temp_max + temp_range*0.1)

        if hums:
            hum_min, hum_max = min(hums), max(hums)
            hum_range = max(5, hum_max - hum_min)
            ax2.set_ylim(max(0, hum_min - hum_range*0.1), min(100, hum_max + hum_range*0.1))

        if mq:
            mq_min, mq_max = min(mq), max(mq)
            mq_range = max(100, mq_max - mq_min)
            ax3.set_ylim(max(0, mq_min - mq_range*0.1), mq_max + mq_range*0.1)

    if temps and hums and mq:
        elapsed_time = times[-1] if times else 0
        pump_status = "🟢" if pump_state["main"] else "🔴"
        flow_status = "🟢" if pump_state["flow"] else "🔴"
        legend_text.set_text(
            f"CONC: {conc} | TRIAL: {trial_label}\n"
            f"PHASE: {current_phase}\n"
            f"Temp: {temps[-1]:.2f}°C\n"
            f"Humidity: {hums[-1]:.2f}%\n"
            f"MQ138: {mq[-1]} a.u.\n"
            f"Baseline set: {'YES' if baseline_established else 'NO'}\n"
            f"Pump:{pump_status} Flow:{flow_status} | Offset: {pump_offset:+d}\n"
            f"Elapsed: {elapsed_time:.1f}s"
        )

    return line_temp, line_hum, line_mq, legend_text

ani = FuncAnimation(fig, update, interval=100, blit=False, cache_frame_data=False, save_count=None)
plt.show()

stop_trial = True
try:
    csvfile.close()
    ser.close()
except:
    pass

print("Master: Trial ended. Data saved to " + csv_file)
input("Press Enter to exit…")