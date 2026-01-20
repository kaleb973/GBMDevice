// ESP32 Pump Test Code
// Using same pump configuration as Arduino_code.cpp

#define PUMP 4
#define BOOT_BUTTON 0

bool pumpState = false;
bool buttonPressed = false;

void setup() {
  Serial.begin(115200);
  
  pinMode(PUMP, OUTPUT);
  digitalWrite(PUMP, LOW);
  
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  
  Serial.println("Pump Test Ready");
  Serial.println("Press BOOT button to toggle pump");
}

void loop() {
  int buttonState = digitalRead(BOOT_BUTTON);
  
  // Button pressed (LOW due to pull-up)
  if (buttonState == LOW && !buttonPressed) {
    buttonPressed = true;
    delay(50);  // Debounce
    
    // Toggle pump
    pumpState = !pumpState;
    digitalWrite(PUMP, pumpState ? HIGH : LOW);
    
    Serial.print("Pump: ");
    Serial.println(pumpState ? "ON" : "OFF");
  }
  
  // Button released
  if (buttonState == HIGH && buttonPressed) {
    buttonPressed = false;
  }
  
  delay(10);
}
