#define NUM_LEDS 5
#define START_LED 2

// Периоды мигания для каждого светодиода (в тиках).
const int led_periods[NUM_LEDS] = {50, 100, 150, 200, 250};

// Массив для хранения текущих значений счётчиков для каждого светодиода.
volatile int led_counters[NUM_LEDS] = {0};


void setup() {
  //Отключение прерываний на время настройки  
  cli(); 
  // pinMode(OUTPUT) для пинов 2..7
  DDRD = 0b00111110;
  
  // Сброс регистров управления таймером
  TCCR1A = 0;
  TCCR1B = 0;

  // Частота МК = 16 000 000 Гц.
  // Предделитель = 64.
  // Частота тиков таймера = 16 000 000 / 64 = 250 000 Гц.
  // Частота прерываний 100 Гц.
  // Значение для сравнения (OCR1A) = 250 000 / 100 - 1 = 2500 - 1 = 2499.
  
  OCR1A = 2499;

  // Включаем CTC Mode
  TCCR1B |= (1 << WGM12);

  // Предделитель 64 - 011
  TCCR1B |= (1 << CS11) | (1 << CS10);
  
  // Прерывание по совпадению с OCR1A
  TIMSK1 |= (1 << OCIE1A);

  sei(); // Включаем прерывания
}

// Обработчик прерывания по совпадению таймера 1
ISR(TIMER1_COMPA_vect) {
for (int i = START_LED; i < START_LED + NUM_LEDS; i++) {
  
    int led_number = i - START_LED;
    led_counters[led_number]++;
    
    if (led_counters[led_number] >= led_periods[led_number]) {
      // Операция конвертирования бита.
      PORTD ^= (1 << i);
      
      // Сброс счётчика, чтобы он начал считать заново для конкретного пина
      led_counters[led_number] = 0;
    }
  }
}

void loop() {
}
