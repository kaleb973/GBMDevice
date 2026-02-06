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

# ---- Trial metadata ----
conc = input("Enter concentration (e.g., 300ppb, 900ppb, 1.2ppm): ").strip().lower()
trial_label = input("Enter trial label (e.g., T1, T2, T3): ").strip().upper()
trial_time = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
csv_file = f"{conc}_{trial_label}_{trial_time}.csv"

# ---- Serial setup ----
ports = [p.device for p in list_ports.comports()]
SERIAL_PORT = ports[0] if ports else input("Enter port: ").strip()

ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
time.sleep(2)

input_thread = threading.Thread(target=read_input, daemon=True)
input_thread.start()

print("=== Connected ===")
print("Commands: START TEST | INJECT | PAUSE | RESUME | STOP | FLUSH MORE | STOP FLUSH | SKIP WARMUP")
print("Saving to:", csv_file)

# ---- Plot setup ----
fig, ax1 = plt.subplots(figsize=(14, 8))
ax2 = ax1.twinx()
ax3 = ax1.twinx()
ax3.spines['right'].set_position(('outward', 70))

line_temp, = ax1.plot([], [], label="Temp (C)", color='#d62728', linewidth=2)
line_hum,  = ax2.plot([], [], label="Humidity (%)", color='#1f77b4', linewidth=2)
line_mq,   = ax3.plot([], [], label="MQ138", color='#2ca02c', linewidth=2)

ax1.set_xlabel("Time (s)", fontsize=12)
ax1.set_ylabel("Temperature (°C)", fontsize=12, color='#d62728')
ax1.tick_params(axis='y', labelcolor='#d62728')

ax2.set_ylabel("Humidity (%)", fontsize=12, color='#1f77b4')
ax2.tick_params(axis='y', labelcolor='#1f77b4')

ax3.set_ylabel("MQ138 (a.u.)", fontsize=12, color='#2ca02c')
ax3.tick_params(axis='y', labelcolor='#2ca02c')

times, temps, hums, mq = [], [], [], []
phase_lines = []
phase_texts = []
test_start_time = None
elapsed_text = None

csvfile = open(csv_file, 'w', newline='')
csvwriter = csv.writer(csvfile)
csvwriter.writerow(['Time(s)', 'Temperature(C)', 'Humidity(%)', 'MQ138(a.u.)', 'Phase'])

current_phase = None

def add_phase_marker(t, phase):
    line = ax1.axvline(t, linestyle='--', alpha=0.6)
    text = ax1.text(t, ax1.get_ylim()[1], phase, rotation=90,
                    va='top', ha='right', fontsize=8)
    phase_lines.append(line)
    phase_texts.append(text)

def update(frame):
    global stop_trial, current_phase, test_start_time, elapsed_text

    # ---- Commands ----
    while not input_queue.empty():
        cmd = input_queue.get()
        if cmd in ("STOP", "EXIT"):
            ser.write(b'STOP\n')
            stop_trial = True
            plt.close()
            return
        ser.write((cmd + "\n").encode())

    # ---- Serial read ----
    while ser.in_waiting:
        line = ser.readline().decode(errors='ignore').strip()
        parts = line.split(',')

        if len(parts) != 5:
            continue

        try:
            t = float(parts[0]) / 1000.0
            temp = float(parts[1])
            hum = float(parts[2])
            mqv = int(parts[3])      # RAW MQ ONLY
            phase = parts[4].strip()
        except:
            continue

        if current_phase != phase:
            current_phase = phase
            add_phase_marker(t, phase)
            phase_msg = f"PHASE_SWITCH: {phase} at {t:.2f}s"
            ser.write((phase_msg + "\n").encode())
            print(f"[{t:.2f}s] Phase changed to: {phase}")

        times.append(t)
        temps.append(temp)
        hums.append(hum)
        mq.append(mqv)

        if test_start_time is None:
            test_start_time = t

        csvwriter.writerow([t, temp, hum, mqv, phase])

    # ---- Plot update ----
    line_temp.set_data(times, temps)
    line_hum.set_data(times, hums)
    line_mq.set_data(times, mq)

    if times:
        ax1.set_xlim(max(0, times[-1]-60), times[-1]+5)

    if temps:
        ax1.set_ylim(min(temps)-1, max(temps)+1)
    if hums:
        ax2.set_ylim(max(0, min(hums)-5), min(100, max(hums)+5))
    if mq:
        ax3.set_ylim(max(0, min(mq)-50), max(mq)+50)

    # keep phase labels at top
    y_top = ax1.get_ylim()[1]
    for txt in phase_texts:
        x, _ = txt.get_position()
        txt.set_position((x, y_top))

    # Update info box with legend and elapsed time
    if times and test_start_time is not None:
        elapsed = times[-1] - test_start_time
        curr_temp = temps[-1] if temps else 0
        curr_hum = hums[-1] if hums else 0
        curr_mq = mq[-1] if mq else 0
        phase_display = current_phase if current_phase else "N/A"
        info_text = f"Temp: {curr_temp:.1f}°C\nHumidity: {curr_hum:.1f}%\nMQ138: {curr_mq}\n\nPhase: {phase_display}\nElapsed: {elapsed:.1f}s"
    else:
        info_text = f"Temp: --°C\nHumidity: --%\nMQ138: --\n\nPhase: N/A\nElapsed: 0.0s"
    
    if elapsed_text is not None:
        elapsed_text.remove()
    elapsed_text = ax1.text(0.02, 0.98, info_text,
                            transform=ax1.transAxes, fontsize=10,
                            verticalalignment='top',
                            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.9))

ani = FuncAnimation(fig, update, interval=100, blit=False)
plt.show()

stop_trial = True
csvfile.close()
ser.close()
print("Kaleb: Trial saved →", csv_file)