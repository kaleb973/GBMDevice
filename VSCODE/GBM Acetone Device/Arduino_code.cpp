#include <DHT.h>

#define MQ138 13  // analog pin
#define DHTTYPE DHT22
#define DHTPIN 15
#define PUMP 4
#define FLOW_PUMP 17  // Flow pump for promoting flow between chambers
#define HEATERPIN 16

#define PWM_CHANNEL 0
#define PWM_FREQ 5000
#define PWM_RES 8

DHT dht(DHTPIN, DHTTYPE);
float targetOutsideTemp = 38.0;  // max temperature

enum Phase { WARMUP, READY, BASELINE, WAITING_FOR_INJECT, VAPOR_INJECTION, ADSORBTION, HEATING, MEASUREMENT, FLUSH };
Phase phase = WARMUP;
bool isPaused = false;

unsigned long phaseStart = 0;
unsigned long pauseStart = 0;

// Baseline tracking
int baselineReading = 0;
int lastMQ = 0;
unsigned long stableStartTime = 0;
const int STABLE_THRESHOLD = 20;     // MQ change less than this = stable (tighter)
const unsigned long STABLE_DURATION = 2500;  // Must be stable for 10 seconds (longer)
bool baselineReady = false;  // True when baseline is established and ready for injection

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(PUMP, OUTPUT);
  digitalWrite(PUMP, LOW);
  pinMode(FLOW_PUMP, OUTPUT);
  digitalWrite(FLOW_PUMP, LOW);
  pinMode(MQ138, INPUT);

  // Heater pin with pull-down resistor
  pinMode(HEATERPIN, OUTPUT);
  digitalWrite(HEATERPIN, LOW);
  
  ledcAttach(HEATERPIN, PWM_FREQ, PWM_RES);
  ledcWrite(HEATERPIN, 0);

  Serial.println("=== GBM Acetone Device ===");
  Serial.println("Starting 60-second sensor warmup...");
  phaseStart = millis();
}

void loop() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  if (isnan(temp) || isnan(hum)) return;

  int mqvalue = analogRead(MQ138);
  unsigned long t = millis();

  // Check for commands from Python
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "START TEST") {
      if (phase == READY) {
        phase = BASELINE;
        phaseStart = millis();
        stableStartTime = 0;
        baselineReading = 0;  // Reset baseline for new trial
        lastMQ = 0;           // Reset last MQ reading
        baselineReady = false; // Reset injection flag
        isPaused = false;
        Serial.println("--- Starting BASELINE phase ---");
      } else {
        Serial.println("System not ready. Wait for warmup to complete.");
      }
    } else if (cmd == "INJECT") {
      if ((phase == BASELINE || phase == WAITING_FOR_INJECT) && baselineReady) {
        phase = VAPOR_INJECTION;
        phaseStart = millis();
        Serial.println("--- Phase: VAPOR_INJECTION ---");
      } else if (phase != BASELINE && phase != WAITING_FOR_INJECT) {
        Serial.println("INJECT only works during BASELINE or WAITING_FOR_INJECT phase.");
      } else {
        Serial.println("Baseline not ready yet. Wait for stable reading.");
      }
    } else if (cmd == "PAUSE") {
      isPaused = true;
      pauseStart = millis();
      Serial.println("--- PAUSED ---");
    } else if (cmd == "RESUME") {
      isPaused = false;
      phaseStart += (millis() - pauseStart);
      Serial.println("--- RESUMED ---");
    } else if (cmd == "STOP") {
      phase = READY;
      digitalWrite(PUMP, LOW);
      digitalWrite(FLOW_PUMP, LOW);
      ledcWrite(HEATERPIN, 0);
      Serial.println("--- STOPPED - Ready for next trial ---");
      return;
    }
  }

  // CSV / CMD output for live plotting
  String phaseStr;
  switch (phase) {
    case WARMUP: phaseStr = "WARMUP"; break;
    case READY: phaseStr = "READY"; break;
    case BASELINE: phaseStr = "BASELINE"; break;
    case WAITING_FOR_INJECT: phaseStr = "WAITING_FOR_INJECT"; break;
    case VAPOR_INJECTION: phaseStr = "VAPOR_INJECTION"; break;
    case ADSORBTION: phaseStr = "ADSORBTION"; break;
    case HEATING: phaseStr = "HEATING"; break;
    case MEASUREMENT: phaseStr = "MEASUREMENT"; break;
    case FLUSH: phaseStr = "FLUSH"; break;
  }

  // Output corrected value after baseline is established
  // Once baseline is set, keep using it for consistency (no jumps in graph)
  int outputValue;
  if (baselineReading == 0) {
    outputValue = mqvalue;  // Raw value if no baseline established yet
  } else {
    outputValue = mqvalue - baselineReading;  // Always use baseline correction once set
  }
  
  Serial.print(t); Serial.print(",");
  Serial.print(temp); Serial.print(",");
  Serial.print(hum); Serial.print(",");
  Serial.print(outputValue); Serial.print(",");
  Serial.println(phaseStr);

  Serial.print("Time: "); Serial.print(t/1000.0, 1);
  Serial.print(" s | Temp: "); Serial.print(temp, 1);
  Serial.print("°C | Hum: "); Serial.print(hum, 1);
  Serial.print("% | MQ138: "); Serial.print(mqvalue);
  Serial.print(" | Phase: "); Serial.println(phaseStr);

  // Don't run phase logic if paused
  if (isPaused) {
    delay(100);
    return;
  }

  int duty = 0;
  unsigned long elapsed = t - phaseStart;

  // --- Phase logic ---
  switch (phase) {
    case WARMUP:
      // One-time warmup on power-up
      digitalWrite(PUMP, HIGH);  // Clean air during warmup
      digitalWrite(FLOW_PUMP, HIGH);  // Flow pump on during warmup
      ledcWrite(HEATERPIN, 0);   // No heating
      if (elapsed > 60000) {     // 60 seconds
        phase = READY;
        Serial.println("=== Warmup complete. Ready for trials ===");
        Serial.println("Type 'START TEST' to begin first trial.");
      }
      break;

    case READY:
      // Waiting for START TEST command
      digitalWrite(PUMP, LOW);
      digitalWrite(FLOW_PUMP, LOW);
      ledcWrite(HEATERPIN, 0);
      break;

    case BASELINE:
      // Establish baseline with clean air
      digitalWrite(PUMP, HIGH);
      digitalWrite(FLOW_PUMP, HIGH);  // Flow pump on to promote flow
      ledcWrite(HEATERPIN, 0);
      
      // Check if baseline is stable
      if (abs(mqvalue - lastMQ) <= STABLE_THRESHOLD) {
        if (stableStartTime == 0) {
          stableStartTime = millis();
        } else if (millis() - stableStartTime >= STABLE_DURATION && elapsed >= 45000) {
          // Stable for required duration and minimum time reached
          if (!baselineReady) {
            baselineReading = mqvalue;
            baselineReady = true;
            phase = WAITING_FOR_INJECT;
            phaseStart = millis();
            Serial.print("Baseline established: ");
            Serial.println(baselineReading);
            Serial.println("Type 'INJECT' to start vapor injection phase.");
          }
        }
      } else {
        stableStartTime = 0;  // Reset if unstable
      }
      
      // Force proceed at 90 seconds max (user must type INJECT or auto-inject at timeout)
      if (elapsed >= 90000 && baselineReady) {
        phase = VAPOR_INJECTION;
        phaseStart = millis();
        Serial.println("--- Phase: VAPOR_INJECTION (auto-start timeout) ---");
      }
      
      lastMQ = mqvalue;
      break;

    case WAITING_FOR_INJECT:
      // Waiting for INJECT command - turn pumps off to avoid drift
      digitalWrite(PUMP, LOW);
      digitalWrite(FLOW_PUMP, LOW);
      ledcWrite(HEATERPIN, 0);
      
      // Auto-inject after 45 seconds of waiting
      if (elapsed >= 45000) {
        phase = VAPOR_INJECTION;
        phaseStart = millis();
        Serial.println("--- Phase: VAPOR_INJECTION (waiting timeout) ---");
      }
      
      lastMQ = mqvalue;
      break;

    case VAPOR_INJECTION:
      digitalWrite(PUMP, HIGH);
      digitalWrite(FLOW_PUMP, LOW);  // Flow pump off during vapor injection
      if (elapsed > 5000) {
        phase = ADSORBTION;
        phaseStart = millis();
        Serial.println("--- Phase: ADSORBTION ---");
      }
      break;

    case ADSORBTION:
      digitalWrite(PUMP, LOW);
      digitalWrite(FLOW_PUMP, LOW);  // Flow pump off during adsorption
      if (elapsed > 7000) {
        phase = HEATING;
        phaseStart = millis();
        Serial.println("--- Phase: HEATING ---");
      }
      break;

    case HEATING:
      digitalWrite(FLOW_PUMP, LOW);  // Flow pump off during heating
      if (temp < targetOutsideTemp - 1) {
        duty = 255;
      } else if (temp < targetOutsideTemp) {
        duty = 128;
      } else {
        duty = 0;  // Safety: don't exceed target
      }
      ledcWrite(HEATERPIN, duty);
      if (temp >= targetOutsideTemp && elapsed > 8000) {
        phase = MEASUREMENT;
        phaseStart = millis();
        Serial.println("--- Phase: MEASUREMENT ---");
      }
      break;

    case MEASUREMENT:
      digitalWrite(PUMP, HIGH);
      digitalWrite(FLOW_PUMP, HIGH);  // Flow pump on for measurement
      // Keep temperature stable, but don't exceed target
      if (temp < targetOutsideTemp - 1) {
        ledcWrite(HEATERPIN, 200);  // Moderate heating
      } else if (temp < targetOutsideTemp) {
        ledcWrite(HEATERPIN, 100);  // Gentle heating
      } else {
        ledcWrite(HEATERPIN, 0);  // Safety: don't exceed target
      }
      if (elapsed > 10000) {
        phase = FLUSH;
        phaseStart = millis();
        Serial.println("--- Phase: FLUSH ---");
      }
      break;

    case FLUSH:
      // Flush chamber and wait for recovery
      digitalWrite(PUMP, HIGH);
      digitalWrite(FLOW_PUMP, HIGH);  // Flow pump on during flush
      ledcWrite(HEATERPIN, 0);
      if (elapsed > 20000) {  // 20 seconds flush
        phase = READY;
        phaseStart = millis();
        Serial.println("=== Trial complete. Ready for next trial ===");
        Serial.println("Type 'START TEST' to begin next trial.");
      }
      break;
  }

  delay(100);
}