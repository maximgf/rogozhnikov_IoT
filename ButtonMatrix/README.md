#include <avr/interrupt.h>

// --- Конфигурация матрицы ---
#define ROW_COUNT 3
#define COL_COUNT 3
#define TOTAL_KEYS (ROW_COUNT * COL_COUNT)

// --- Тайминги ---
#define SCAN_PERIOD_MS 10
#define DEBOUNCE_THRESHOLD 3

// Данные для антидребезга и состояний (volatile для ISR)
volatile uint8_t keyDebounceCounters[TOTAL_KEYS] = {0};
volatile bool keyRawStates[TOTAL_KEYS] = {false};
volatile bool keyStableStates[TOTAL_KEYS] = {false};
volatile bool keyCurrentBuffer[TOTAL_KEYS] = {false};

// Переменные для логики нажатий в основном цикле
bool keyPreviousStates[TOTAL_KEYS] = {false};
unsigned long keyPressStartTime[TOTAL_KEYS] = {0};

volatile uint8_t activeRowIndex = 0;
volatile bool isScanCycleFinished = false;

void setup() {
  Serial.begin(9600);
  
  // Настройка строк (D2-D4) на выход
  DDRD |= (1 << DDD2) | (1 << DDD3) | (1 << DDD4);
  PORTD |= (1 << PORTD2) | (1 << PORTD3) | (1 << PORTD4);
  
  // Настройка столбцов (D5-D7) на вход с подтяжкой
  DDRD &= ~((1 << DDD5) | (1 << DDD6) | (1 << DDD7));
  PORTD |= (1 << PORTD5) | (1 << PORTD6) | (1 << PORTD7);
  
  initTimer1();
  Serial.println("Matrix Keyboard Initialized");
}

// Настройка аппаратного таймера для сканирования
void initTimer1() {
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  TCCR1B |= (1 << WGM12); // CTC mode
  
  // Расчет порога для 10мс при прерывателе 256
  uint16_t compareValue = (F_CPU / 256) * SCAN_PERIOD_MS / 1000 - 1;
  OCR1A = compareValue;
  
  TCCR1B |= (1 << CS12);  // Prescaler 256
  TIMSK1 |= (1 << OCIE1A); // Enable interrupt
  sei();
}

// Прерывание таймера: сканирование одной строки за раз
ISR(TIMER1_COMPA_vect) {
  const uint8_t rowPins[] = { (1 << PORTD2), (1 << PORTD3), (1 << PORTD4) };
  const uint8_t colPins[] = { (1 << PIND5), (1 << PIND6), (1 << PIND7) };
  
  // Сброс всех строк (HIGH) и активация текущей (LOW)
  PORTD |= rowPins[0] | rowPins[1] | rowPins[2];
  PORTD &= ~rowPins[activeRowIndex];
  
  // Короткая пауза для стабилизации сигнала
  asm volatile("nop\nnop\nnop\nnop");
  
  uint8_t pinSnapshot = PIND;
  
  for (uint8_t col = 0; col < COL_COUNT; col++) {
    uint8_t keyIdx = col + COL_COUNT * activeRowIndex;
    bool isPressed = !(pinSnapshot & colPins[col]);
    
    // Алгоритм фильтрации дребезга
    if (isPressed == keyRawStates[keyIdx]) {
      if (keyDebounceCounters[keyIdx] < DEBOUNCE_THRESHOLD) {
        keyDebounceCounters[keyIdx]++;
      }
      if (keyDebounceCounters[keyIdx] >= DEBOUNCE_THRESHOLD) {
        keyStableStates[keyIdx] = isPressed;
      }
    } else {
      keyDebounceCounters[keyIdx] = 0;
      keyRawStates[keyIdx] = isPressed;
    }
  }
  
  // Переход к следующей строке или завершение цикла
  activeRowIndex++;
  if (activeRowIndex >= ROW_COUNT) {
    activeRowIndex = 0;
    for (uint8_t i = 0; i < TOTAL_KEYS; i++) {
      keyCurrentBuffer[i] = keyStableStates[i];
    }
    isScanCycleFinished = true;
  }
}

void loop() {
  if (!isScanCycleFinished) return;
  
  // Атомарное копирование данных из ISR
  cli();
  isScanCycleFinished = false;
  bool localSnapshot[TOTAL_KEYS];
  for (uint8_t i = 0; i < TOTAL_KEYS; i++) {
    localSnapshot[i] = keyCurrentBuffer[i];
  }
  sei();
  
  unsigned long now = millis();
  bool hasChanges = false;
  
  // Обработка событий нажатия и отпускания
  for (uint8_t i = 0; i < TOTAL_KEYS; i++) {
    if (localSnapshot[i] != keyPreviousStates[i]) {
      hasChanges = true;
      
      if (localSnapshot[i]) {
        keyPressStartTime[i] = now; // Засекаем начало нажатия
      } else {
        // Вычисляем длительность при отпускании
        unsigned long duration = now - keyPressStartTime[i];
        logRelease(i + 1, duration, keyPressStartTime[i]);
      }
      keyPreviousStates[i] = localSnapshot[i];
    }
  }
  
  if (hasChanges) {
    logPressedKeys(localSnapshot);
  }
}

// Вывод списка всех зажатых кнопок
void logPressedKeys(bool* states) {
  bool found = false;
  Serial.print("Active: ");
  for (uint8_t i = 0; i < TOTAL_KEYS; i++) {
    if (states[i]) {
      if (found) Serial.print(", ");
      Serial.print(i + 1);
      found = true;
    }
  }
  if (!found) Serial.print("none");
  Serial.println();
}

// Вывод отчета о завершенном клике
void logRelease(uint8_t id, unsigned long ms, unsigned long start) {
  Serial.print("Key ");
  Serial.print(id);
  Serial.print(" | Duration: ");
  Serial.print(ms);
  Serial.print("ms | Start: ");
  Serial.println(start);
}