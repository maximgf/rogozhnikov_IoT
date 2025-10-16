#define PHOTORESISTOR_PIN A0
bool streaming = false;
unsigned long previousMillis = 0;
const long interval = 1000; // Интервал потоковой передачи

void setup() {
  Serial.begin(9600);
  pinMode(PHOTORESISTOR_PIN, INPUT);
}

void loop() {
  handleSerialCommand();
  handleStreaming();
}

void handleSerialCommand() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    if (cmd == 'p') {
      int value = analogRead(PHOTORESISTOR_PIN);
      Serial.print("SENSOR_VALUE:");
      Serial.println(value);
      streaming = false;
    }
    else if (cmd == 's') {
      streaming = true;
      Serial.println("STREAM_STARTED");
    }
    else if (cmd == 'q') { // Добавим команду для остановки потока
      streaming = false;
      Serial.println("STREAM_STOPPED");
    }
  }
}

void handleStreaming() {
  if (streaming) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      int value = analogRead(PHOTORESISTOR_PIN);
      Serial.print("SENSOR_VALUE:");
      Serial.println(value);
    }
  }
}