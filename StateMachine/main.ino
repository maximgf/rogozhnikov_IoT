#define TRIG_PIN A0
#define ECHO_PIN A1

#define DIR_LEFT 7
#define DIR_RIGHT 4
#define SPEED_RIGHT 5
#define SPEED_LEFT 6

#define FORWARD_LEFT LOW
#define FORWARD_RIGHT LOW

float Kp = 3.0;
float Kd = 1.2;
int setpoint = 14;

float error = 0, prevError = 0;
int baseSpeed = 120;

const int MAX_DIST = 200;

enum State {
  FOLLOW_WALL,
  REVERSE,
  TURN_RIGHT,
  CORNER_FWD,
  TURN_LEFT
};

State currentState = FOLLOW_WALL;
unsigned long stateTimer = 0;

const int REVERSE_TIME = 250;
const int TURN_TIME_90 = 400;
const int FORWARD_DELAY = 300;

void move(bool left_dir, int left_speed, bool right_dir, int right_speed) {
  digitalWrite(DIR_LEFT, left_dir);
  digitalWrite(DIR_RIGHT, right_dir);
  analogWrite(SPEED_RIGHT, right_speed);
  analogWrite(SPEED_LEFT, left_speed);
}

void forward(int left_speed, int right_speed) {
  move(FORWARD_LEFT, left_speed, FORWARD_RIGHT, right_speed);
}

void turn_left(int steepness) {
  forward(steepness, 255);
}

void turn_right(int steepness) {
  forward(255, steepness);
}

void rotate_left(int speed) {
  move(!FORWARD_LEFT, speed, FORWARD_RIGHT, speed);
}

void rotate_right(int speed) {
  move(FORWARD_LEFT, speed, !FORWARD_RIGHT, speed);
}

void setMotorsPID(int speedL, int speedR) {
  bool dirL = FORWARD_LEFT;
  bool dirR = FORWARD_RIGHT;

  if (speedL < 0) {
    dirL = !FORWARD_LEFT;
    speedL = -speedL;
  }
  if (speedR < 0) {
    dirR = !FORWARD_RIGHT;
    speedR = -speedR;
  }

  speedL = constrain(speedL, 0, 255);
  speedR = constrain(speedR, 0, 255);

  move(dirL, speedL, dirR, speedR);
}

void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(DIR_LEFT, OUTPUT);
  pinMode(DIR_RIGHT, OUTPUT);
  pinMode(SPEED_LEFT, OUTPUT);
  pinMode(SPEED_RIGHT, OUTPUT);

  forward(0, 0);
  delay(2000);
}

void loop() {
  int distance = getDistance();

  switch (currentState) {
    
    case FOLLOW_WALL:
      if (distance < 8) {
        currentState = REVERSE;
        stateTimer = millis();
        forward(0, 0); // Стоп
      }
      else if (distance > 25) {
        currentState = CORNER_FWD;
        stateTimer = millis();
        forward(0, 0); // Стоп
      }
      else {
        runPID(distance);
      }
      break;

    case REVERSE:
      move(!FORWARD_LEFT, baseSpeed, !FORWARD_RIGHT, baseSpeed); 
      
      if (millis() - stateTimer > REVERSE_TIME) {
        currentState = TURN_RIGHT;
        stateTimer = millis();
      }
      break;

    case TURN_RIGHT:
      rotate_right(baseSpeed + 20);
      
      if (millis() - stateTimer > TURN_TIME_90) {
        currentState = FOLLOW_WALL;
        resetPID();
      }
      break;

    case CORNER_FWD:
      forward(baseSpeed, baseSpeed); 
      
      if (millis() - stateTimer > FORWARD_DELAY) {
        currentState = TURN_LEFT;
        stateTimer = millis();
      }
      break;

    case TURN_LEFT:
      rotate_left(baseSpeed + 20); 
      
      if (millis() - stateTimer > TURN_TIME_90) {
        currentState = FOLLOW_WALL;
        resetPID();
      }
      break;
  }
  
  delay(30);
}

int getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 15000);

  if (duration == 0) return MAX_DIST;

  int dist = duration * 0.034 / 2;
  
  if (dist > MAX_DIST) return MAX_DIST;
  return dist;
}

void runPID(int currentDist) {
  error = setpoint - currentDist; 
  float derivative = error - prevError;
  
  float output = (Kp * error) + (Kd * derivative);
  prevError = error;

  int speedLeft = baseSpeed + output;
  int speedRight = baseSpeed - output;

  setMotorsPID(speedLeft, speedRight);
}

void resetPID() {
  error = 0;
  prevError = 0;
  forward(0, 0);
  delay(100); 
}