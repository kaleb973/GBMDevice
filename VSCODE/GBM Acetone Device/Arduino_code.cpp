#include <Arduino.h>
#include <DHT.h>

// ---------- PINS ----------
#define MQ138_PIN 34        // ADC1 pin (IMPORTANT)
#define DHTPIN 15
#define DHTTYPE DHT22
#define PUMP 4
#define FLOW_PUMP 17
#define HEATERPIN 16

// ---------- PWM ----------
#define PWM_FREQ 5000
#define PWM_RES 8

// ---------- TARGET ----------
float targetOutsideTemp = 38.0;

// ---------- TIMING ----------
const unsigned long WARMUP_MS          = 600000;  // 10 min
const unsigned long BASELINE_MIN_MS    = 45000;
const unsigned long BASELINE_MAX_MS    = 90000;
const unsigned long WAIT_INJECT_MAX_MS = 45000;
const unsigned long STABLE_DURATION    = 10000;
const int STABLE_THRESHOLD             = 20;
const unsigned long MIN_HEATING_MS     = 20000;
const unsigned long MEASUREMENT_MS     = 10000;
const unsigned long FLUSH_MS           = 20000;

// ---------- PHASES ----------
enum Phase {
  WARMUP, READY, BASELINE, WAITING_FOR_INJECT,
  VAPOR_INJECTION, ADSORBTION, HEATING,
  MEASUREMENT, FLUSH
};

Phase phase = WARMUP;

DHT dht(DHTPIN, DHTTYPE);

// ---------- STATE ----------
unsigned long phaseStart = 0;
unsigned long stableStart = 0;

int baselineReading = 0;
int lastMQ = 0;
bool baselineReady = false;
bool extraFlush = false;

// ---------- DHT SAFETY ----------
float lastTemp = 0;
float lastHum = 0;
bool hasDHT = false;

// ---------- HELPERS ----------
void setPumps(bool mainOn, bool flowOn) {
  digitalWrite(PUMP, mainOn ? HIGH : LOW);
  digitalWrite(FLOW_PUMP, flowOn ? HIGH : LOW);
}

void handleCommand(String cmd) {
  cmd.trim();

  if (cmd == "START TEST" && phase == READY) {
    phase = BASELINE;
    phaseStart = millis();
    stableStart = 0;
    baselineReading = 0;
    baselineReady = false;
    lastMQ = 0;
    extraFlush = false;
  }

  else if (cmd == "INJECT" && baselineReady &&
          (phase == BASELINE || phase == WAITING_FOR_INJECT)) {
    phase = VAPOR_INJECTION;
    phaseStart = millis();
  }

  else if (cmd == "STOP") {
    phase = READY;
    extraFlush = false;
    setPumps(false, false);
    ledcWrite(HEATERPIN, 0);
  }

  else if (cmd == "FLUSH MORE" && phase == READY) {
    extraFlush = true;
  }

  else if (cmd == "STOP FLUSH" && phase == READY) {
    extraFlush = false;
  }

  else if (cmd == "SKIP WARMUP" && phase == WARMUP) {
    phase = READY;
    phaseStart = millis();
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

  // PWM (ESP32 core v3.x)
  ledcAttach(HEATERPIN, PWM_FREQ, PWM_RES);
  ledcWrite(HEATERPIN, 0);

  // ADC setup
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  phaseStart = millis();
}

void loop() {
  // ----- Read DHT safely -----
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

  // ----- Commands -----
  if (Serial.available()) {
    handleCommand(Serial.readStringUntil('\n'));
  }

  // ----- Output CSV ONLY -----
  int outMQ = (baselineReading == 0) ? mqRaw : (mqRaw - baselineReading);

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

  // ----- Phase logic -----
  switch (phase) {

    case WARMUP:
      setPumps(true, true);
      ledcWrite(HEATERPIN, 0);
      if (elapsed >= WARMUP_MS) {
        phase = READY;
        phaseStart = now;
      }
      break;

    case READY:
      ledcWrite(HEATERPIN, 0);
      if (extraFlush) setPumps(true, true);
      else setPumps(false, false);
      break;

    case BASELINE:
      setPumps(true, true);
      ledcWrite(HEATERPIN, 0);

      if (abs(mqRaw - lastMQ) <= STABLE_THRESHOLD) {
        if (stableStart == 0) stableStart = now;
        else if ((now - stableStart >= STABLE_DURATION) &&
                 (elapsed >= BASELINE_MIN_MS) && !baselineReady) {
          baselineReading = mqRaw;
          baselineReady = true;
          phase = WAITING_FOR_INJECT;
          phaseStart = now;
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
      ledcWrite(HEATERPIN, 0);
      if (elapsed >= WAIT_INJECT_MAX_MS) {
        phase = VAPOR_INJECTION;
        phaseStart = now;
      }
      break;

    case VAPOR_INJECTION:
      setPumps(true, false);
      ledcWrite(HEATERPIN, 0);
      if (elapsed > 5000) {
        phase = ADSORBTION;
        phaseStart = now;
      }
      break;

    case ADSORBTION:
      setPumps(false, false);
      ledcWrite(HEATERPIN, 0);
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
      ledcWrite(HEATERPIN, duty);

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
      ledcWrite(HEATERPIN, duty);

      if (elapsed >= MEASUREMENT_MS) {
        phase = FLUSH;
        phaseStart = now;
      }
      break;
    }

    case FLUSH:
      setPumps(true, true);
      ledcWrite(HEATERPIN, 0);
      if (elapsed >= FLUSH_MS) {
        phase = READY;
        phaseStart = now;
      }
      break;
  }

  delay(100);
}