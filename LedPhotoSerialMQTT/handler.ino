#define LED_PIN 13
bool blinking = false;
unsigned long blinkPreviousMillis = 0;
const long blinkInterval = 500; 

void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); }

void loop() {
  handleSerialCommand();
  handleBlinking();
}

void handleSerialCommand() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    if (cmd == 'u') {
      digitalWrite(LED_PIN, HIGH);
      blinking = false;
      Serial.println("LED_GOES_ON");
    }
    else if (cmd == 'd') {
      digitalWrite(LED_PIN, LOW);
      blinking = false;
      Serial.println("LED_GOES_OFF");
    }
    else if (cmd == 'b') {
      blinking = true;
      Serial.println("LED_WILL_BLINK");
    }
  }
}

void handleBlinking() {
  if (blinking) {
    unsigned long currentMillis = millis();
    if (currentMillis - blinkPreviousMillis >= blinkInterval) {
      blinkPreviousMillis = currentMillis;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }
}