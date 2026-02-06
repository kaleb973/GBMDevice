#include <DHT.h>
#include <Arduino.h>

// ---------- PINS (ESP32 DEVKIT V1) ----------
#define MQ138_PIN 34          // ADC1 pin (IMPORTANT)
#define DHTPIN 15
#define DHTTYPE DHT22
#define PUMP 4
#define FLOW_PUMP 17
#define HEATERPIN 16

// ---------- PWM ----------
#define PWM_CHANNEL 0
#define PWM_FREQ 5000
#define PWM_RES 8

// ---------- TARGET ----------
float targetOutsideTemp = 38.0;

// ---------- TIMING ----------
const unsigned long WARMUP_MS        = 600000;  // 10 min
const unsigned long BASELINE_MIN_MS  = 45000;   // 45s
const unsigned long BASELINE_MAX_MS  = 90000;   // 90s
const unsigned long WAIT_INJECT_MAX_MS = 45000; // 45s
const unsigned long STABLE_DURATION  = 10000;   // 10s stable
const int STABLE_THRESHOLD           = 20;      // delta threshold
const unsigned long MIN_HEATING_MS   = 20000;   // 20s minimum heating
const unsigned long MEASUREMENT_MS   = 10000;   // 10s
const unsigned long FLUSH_MS         = 20000;   // 20s

// ---------- PHASES ----------
enum Phase { WARMUP, READY, BASELINE, WAITING_FOR_INJECT, VAPOR_INJECTION, ADSORBTION, HEATING, MEASUREMENT, FLUSH };
Phase phase = WARMUP;

DHT dht(DHTPIN, DHTTYPE);

// ---------- STATE ----------
unsigned long phaseStart = 0;
unsigned long stableStart = 0;

int baselineReading = 0;
int lastMQ = 0;
bool baselineReady = false;

bool extraFlush = false;

// DHT robustness
float lastTemp = 0;
float lastHum = 0;
bool hasDHT = false;

// ---------- PWM COMPAT (handles ESP32 core v2 vs v3) ----------
static inline void pwmInit() {
#if defined(ARDUINO_ESP32_VERSION_MAJOR) && (ARDUINO_ESP32_VERSION_MAJOR >= 3)
  ledcAttach(HEATERPIN, PWM_FREQ, PWM_RES);
  ledcWrite(HEATERPIN, 0);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(HEATERPIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);
#endif
}
static inline void pwmWriteDuty(int duty) {
#if defined(ARDUINO_ESP32_VERSION_MAJOR) && (ARDUINO_ESP32_VERSION_MAJOR >= 3)
  ledcWrite(HEATERPIN, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

void setPumps(bool mainOn, bool flowOn) {
  digitalWrite(PUMP, mainOn ? HIGH : LOW);
  digitalWrite(FLOW_PUMP, flowOn ? HIGH : LOW);
}

void handleCommand(String cmd) {
  cmd.trim();

  if (cmd == "START TEST") {
    if (phase == READY) {
      phase = BASELINE;
      phaseStart = millis();
      stableStart = 0;
      baselineReading = 0;
      baselineReady = false;
      lastMQ = 0;
      extraFlush = false;
    }
    return;
  }

  if (cmd == "INJECT") {
    if (baselineReady && (phase == BASELINE || phase == WAITING_FOR_INJECT)) {
      phase = VAPOR_INJECTION;
      phaseStart = millis();
    }
    return;
  }

  if (cmd == "STOP") {
    phase = READY;
    extraFlush = false;
    setPumps(false, false);
    pwmWriteDuty(0);
    return;
  }

  if (cmd == "FLUSH MORE") {
    if (phase == READY) extraFlush = true;
    return;
  }

  if (cmd == "STOP FLUSH") {
    if (phase == READY) extraFlush = false;
    return;
  }

  if (cmd == "SKIP WARMUP") {
    if (phase == WARMUP) {
      phase = READY;
      phaseStart = millis();
    }
    return;
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(PUMP, OUTPUT);
  pinMode(FLOW_PUMP, OUTPUT);
  pinMode(HEATERPIN, OUTPUT);

  digitalWrite(PUMP, LOW);
  digitalWrite(FLOW_PUMP, LOW);
  digitalWrite(HEATERPIN, LOW);

  pwmInit();

  // ADC config (helps stability)
  analogReadResolution(12);        // 0..4095
  analogSetAttenuation(ADC_11db);  // wider input range

  phaseStart = millis();
}

void loop() {
  // Read DHT safely
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    if (!hasDHT) return;
    temp = lastTemp;
    hum  = lastHum;
  } else {
    lastTemp = temp;
    lastHum  = hum;
    hasDHT   = true;
  }

  int mqRaw = analogRead(MQ138_PIN);
  unsigned long now = millis();

  // Commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }

  // Baseline corrected output (raw until baseline set)
  int outMQ = (baselineReading == 0) ? mqRaw : (mqRaw - baselineReading);

  // CSV ONLY: t,temp,hum,mq,phase
  Serial.print(now); Serial.print(",");
  Serial.print(temp); Serial.print(",");
  Serial.print(hum); Serial.print(",");
  Serial.print(outMQ); Serial.print(",");

  switch (phase) {
    case WARMUP: Serial.println("WARMUP"); break;
    case READY: Serial.println("READY"); break;
    case BASELINE: Serial.println("BASELINE"); break;
    case WAITING_FOR_INJECT: Serial.println("WAITING_FOR_INJECT"); break;
    case VAPOR_INJECTION: Serial.println("VAPOR_INJECTION"); break;
    case ADSORBTION: Serial.println("ADSORBTION"); break;
    case HEATING: Serial.println("HEATING"); break;
    case MEASUREMENT: Serial.println("MEASUREMENT"); break;
    case FLUSH: Serial.println("FLUSH"); break;
  }

  unsigned long elapsed = now - phaseStart;

  // Phase logic
  switch (phase) {
    case WARMUP:
      setPumps(true, true);
      pwmWriteDuty(0);
      if (elapsed >= WARMUP_MS) {
        phase = READY;
        phaseStart = now;
      }
      break;

    case READY:
      pwmWriteDuty(0);
      if (extraFlush) setPumps(true, true);
      else setPumps(false, false);
      break;

    case BASELINE:
      setPumps(true, true);
      pwmWriteDuty(0);

      if (abs(mqRaw - lastMQ) <= STABLE_THRESHOLD) {
        if (stableStart == 0) stableStart = now;
        else {
          if ((now - stableStart >= STABLE_DURATION) && (elapsed >= BASELINE_MIN_MS) && !baselineReady) {
            baselineReading = mqRaw;
            baselineReady = true;
            phase = WAITING_FOR_INJECT;
            phaseStart = now;
          }
        }
      } else {
        stableStart = 0;
      }

      if (elapsed >= BASELINE_MAX_MS && baselineReady) {
        phase = VAPOR_INJECTION;
        phaseStart = now;
      }

      lastMQ = mqRaw;
      break;

    case WAITING_FOR_INJECT:
      setPumps(false, false);
      pwmWriteDuty(0);
      if (elapsed >= WAIT_INJECT_MAX_MS) {
        phase = VAPOR_INJECTION;
        phaseStart = now;
      }
      break;

    case VAPOR_INJECTION:
      setPumps(true, false);
      pwmWriteDuty(0);
      if (elapsed > 5000) {
        phase = ADSORBTION;
        phaseStart = now;
      }
      break;

    case ADSORBTION:
      setPumps(false, false);
      pwmWriteDuty(0);
      if (elapsed > 7000) {
        phase = HEATING;
        phaseStart = now;
      }
      break;

    case HEATING: {
      setPumps(false, false);
      int duty = 0;
      if (temp < targetOutsideTemp - 1) duty = 255;
      else if (temp < targetOutsideTemp) duty = 128;
      else duty = 0;
      pwmWriteDuty(duty);

      if (elapsed >= MIN_HEATING_MS && temp >= targetOutsideTemp) {
        phase = MEASUREMENT;
        phaseStart = now;
      }
      break;
    }

    case MEASUREMENT: {
      setPumps(true, true);
      int duty = 0;
      if (temp < targetOutsideTemp - 1) duty = 200;
      else if (temp < targetOutsideTemp) duty = 100;
      else duty = 0;
      pwmWriteDuty(duty);

      if (elapsed >= MEASUREMENT_MS) {
        phase = FLUSH;
        phaseStart = now;
      }
      break;
    }

    case FLUSH:
      setPumps(true, true);
      pwmWriteDuty(0);
      if (elapsed >= FLUSH_MS) {
        phase = READY;
        phaseStart = now;
      }
      break;
  }

  delay(100);
}