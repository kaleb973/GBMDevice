#include <DHT.h>

#define MQ138 13  // analog pin
#define DHTTYPE DHT22
#define DHTPIN 15
#define PUMP 4
#define HEATERPIN 16

#define PWM_CHANNEL 0
#define PWM_FREQ 5000
#define PWM_RES 8

DHT dht(DHTPIN, DHTTYPE);
float targetOutsideTemp = 38.0;  // max temperature

enum Phase { WARMUP, WAITING, BASELINE, VAPOR_INJECTION, ADSORBTION, HEATING, MEASUREMENT, FLUSH, READY };
Phase phase = WARMUP;
bool isPaused = false;

unsigned long phaseStart = 0;
unsigned long pauseStart = 0;

// Baseline tracking
int baselineReading = 0;
int lastMQ = 0;
unsigned long stableStartTime = 0;
const int STABLE_THRESHOLD = 5;     // MQ change less than this = stable
const unsigned long STABLE_DURATION = 5000;  // Must be stable for 5 seconds

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(PUMP, OUTPUT);
  digitalWrite(PUMP, LOW);
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
        isPaused = false;
        Serial.println("--- Starting BASELINE phase ---");
      } else {
        Serial.println("System not ready. Wait for warmup to complete.");
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
      ledcWrite(HEATERPIN, 0);
      Serial.println("--- STOPPED - Ready for next trial ---");
      return;
    }
  }

  // CSV / CMD output for live plotting
  String phaseStr;
  switch (phase) {
    case WARMUP: phaseStr = "WARMUP"; break;
    case WAITING: phaseStr = "WAITING"; break;
    case BASELINE: phaseStr = "BASELINE"; break;
    case VAPOR_INJECTION: phaseStr = "VAPOR_INJECTION"; break;
    case ADSORBTION: phaseStr = "ADSORBTION"; break;
    case HEATING: phaseStr = "HEATING"; break;
    case MEASUREMENT: phaseStr = "MEASUREMENT"; break;
    case FLUSH: phaseStr = "FLUSH"; break;
    case READY: phaseStr = "READY"; break;
  }

  // Output baseline-corrected value during trial
  int correctedMQ = mqvalue - baselineReading;
  
  Serial.print(t); Serial.print(",");
  Serial.print(temp); Serial.print(",");
  Serial.print(hum); Serial.print(",");
  Serial.print(correctedMQ); Serial.print(",");
  Serial.println(phaseStr);

  Serial.print("Time: "); Serial.print(t/1000.0, 1);
  Serial.print(" s | Temp: "); Serial.print(temp, 1);
  Serial.print("°C | Hum: "); Serial.print(hum, 1);
  Serial.print("% | MQ138: "); Serial.print(correctedMQ);
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
      ledcWrite(HEATERPIN, 0);
      break;

    case BASELINE:
      // Establish baseline with clean air
      digitalWrite(PUMP, HIGH);
      ledcWrite(HEATERPIN, 0);
      
      // Check if baseline is stable
      if (abs(mqvalue - lastMQ) <= STABLE_THRESHOLD) {
        if (stableStartTime == 0) {
          stableStartTime = millis();
        } else if (millis() - stableStartTime >= STABLE_DURATION) {
          // Stable for required duration
          if (elapsed >= 30000) {  // Minimum 30 seconds
            baselineReading = mqvalue;
            phase = VAPOR_INJECTION;
            phaseStart = millis();
            Serial.print("Baseline established: ");
            Serial.println(baselineReading);
            Serial.println("--- Phase: VAPOR_INJECTION ---");
          }
        }
      } else {
        stableStartTime = 0;  // Reset if unstable
      }
      
      // Force proceed at 60 seconds max
      if (elapsed >= 60000) {
        baselineReading = mqvalue;
        phase = VAPOR_INJECTION;
        phaseStart = millis();
        Serial.print("Baseline (max time): ");
        Serial.println(baselineReading);
        Serial.println("--- Phase: VAPOR_INJECTION ---");
      }
      
      lastMQ = mqvalue;
      break;

    case VAPOR_INJECTION:
      digitalWrite(PUMP, HIGH);
      if (elapsed > 5000) {
        phase = ADSORBTION;
        phaseStart = millis();
        Serial.println("--- Phase: ADSORBTION ---");
      }
      break;

    case ADSORBTION:
      digitalWrite(PUMP, LOW);
      if (elapsed > 7000) {
        phase = HEATING;
        phaseStart = millis();
        Serial.println("--- Phase: HEATING ---");
      }
      break;

    case HEATING:
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