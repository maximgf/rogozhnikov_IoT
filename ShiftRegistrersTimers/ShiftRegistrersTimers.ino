#define OUT_L_PORT PORTD
#define OUT_L_DDR  DDRD
#define OUT_L_MSK  (1 << 5)

#define OUT_C_PORT PORTD
#define OUT_C_DDR  DDRD
#define OUT_C_MSK  (1 << 3)

#define OUT_D_PORT PORTD
#define OUT_D_DDR  DDRD
#define OUT_D_MSK  (1 << 7)

volatile int t_count = 0;
volatile int u_val = -1;
volatile bool active = false;

const uint8_t MAP[10] = {
  0xBB, 0x0A, 0x73, 0x5B, 0xCA, 0xD9, 0xFD, 0x0B, 0xFB, 0xDF
};

void push_bit(uint8_t data) {
  for (int i = 7; i >= 0; i--) {
    if (data & (1 << i)) {
      OUT_D_PORT |= OUT_D_MSK;
    } else {
      OUT_D_PORT &= ~OUT_D_MSK;
    }
    OUT_C_PORT |= OUT_C_MSK;
    OUT_C_PORT &= ~OUT_C_MSK;
  }
}

uint8_t get_hex(int n) {
  if (n < 0 || n > 9) return 0;
  return MAP[n];
}

void setup() {
  OUT_L_DDR |= OUT_L_MSK;
  OUT_C_DDR |= OUT_C_MSK;
  OUT_D_DDR |= OUT_D_MSK;

  Serial.begin(9600);

  cli();
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0;
  TCCR1B |= (1 << WGM12) | (1 << CS12) | (1 << CS10);
  OCR1A = 15624;
  TIMSK1 |= (1 << OCIE1A);
  sei();
}

ISR(TIMER1_COMPA_vect) {
  int view;
  int cur_u;

  cli();
  cur_u = u_val;
  u_val = -1;
  sei();

  if (!active) {
    if (cur_u != -1) {
      t_count = cur_u;
      active = true;
    }
    view = t_count;
  } else {
    t_count++;
    if (t_count >= 60) t_count = 0;
    view = (cur_u != -1) ? cur_u : t_count;
  }

  view %= 60;

  OUT_L_PORT &= ~OUT_L_MSK;
  push_bit(get_hex(view / 10));
  push_bit(get_hex(view % 10));
  OUT_L_PORT |= OUT_L_MSK;
}

void loop() {
  static char b[4];
  static byte p = 0;
  static unsigned long t = 0;

  if (Serial.available() > 0) {
    char c = Serial.read();
    if (isDigit(c) && p < 2) {
      b[p++] = c;
      t = millis();
    }
  }

  if (p > 0 && (millis() - t > 25)) {
    b[p] = '\0';
    int v = atoi(b);
    cli();
    u_val = v;
    sei();
    p = 0;
  }
}