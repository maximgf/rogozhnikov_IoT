# Разбор кода программной реализации UART

## 1. Структура проекта и основные компоненты

### 1.1. Заголовочные файлы и макросы
```cpp
#include <util/atomic.h>

#define UART_TX_PIN PD4 
#define UART_RX_PIN PD3 
#define BUFFER_SIZE 64
```
- **`util/atomic.h`** - обеспечивает атомарные операции для безопасной работы с разделяемыми переменными
- **Пины** - фиксированные номера пинов для TX и RX
- **Размер буфера** - 64 байта для каждого направления

### 1.2. Кольцевые буферы
```cpp
volatile char tx_buffer[BUFFER_SIZE];
volatile uint8_t tx_head = 0;
volatile uint8_t tx_tail = 0;

volatile char rx_buffer[BUFFER_SIZE];
volatile uint8_t rx_head = 0;
volatile uint8_t rx_tail = 0;
```
**Особенности реализации:**
- `volatile` - гарантирует, что компилятор не оптимизирует доступ к переменным, используемым в прерываниях
- **head** - индекс для записи нового элемента
- **tail** - индекс для чтения следующего элемента
- **Кольцевая структура**: `(index + 1) % BUFFER_SIZE`

## 2. Система состояний UART

### 2.1. Общие переменные состояния
```cpp
volatile uint16_t timer1_ticks_per_bit;
```
- Рассчитывается на основе F_CPU и baudrate
- Определяет временной интервал одного бита

### 2.2. Состояние передатчика (TX)
```cpp
volatile bool tx_active = false;
volatile uint8_t tx_byte_to_send;
volatile uint8_t tx_bit_index = 0;
```
**Конечный автомат TX:**
- **tx_active** - флаг активности передачи
- **tx_byte_to_send** - текущий передаваемый байт
- **tx_bit_index** - счётчик битов (0-9)

### 2.3. Состояние приёмника (RX)
```cpp
volatile bool rx_active = false;
volatile uint8_t rx_byte_received;
volatile uint8_t rx_bit_index = 0;
```
**Конечный автомат RX:**
- **rx_active** - флаг активности приёма
- **rx_byte_received** - собираемый байт
- **rx_bit_index** - счётчик принятых битов (0-8)

## 3. Детальный разбор функций API

### 3.1. Настройка скорости передачи
```cpp
void uart_set_baudrate(int baudrate) {
  timer1_ticks_per_bit = F_CPU / (8UL * baudrate);
  
  TCCR1A = 0;
  TCCR1B = (1 << CS11);
  TIMSK1 = 0;
}
```
**Расчёт временных параметров:**
- Формула: `F_CPU / (предделитель × baudrate)`
- Пример для 16 МГц, 9600 бод: `16000000 / (8 × 9600) = 208.33`
- Timer1 настраивается с предделителем 8

### 3.2. Функция передачи байта
```cpp
bool uart_send(char b) {
  uint8_t next_head = (tx_head + 1) % BUFFER_SIZE;
  if (next_head == tx_tail) {
    return false; // Буфер полон
  }

  tx_buffer[tx_head] = b;
  tx_head = next_head;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (!tx_active) {
      tx_active = true;
      tx_bit_index = 0; 
      OCR1A = TCNT1 + 10; 
      TIMSK1 |= (1 << OCIE1A);
    }
  }
  return true;
}
```
**Ключевые моменты:**
1. **Проверка переполнения**: сравнение следующего head с tail
2. **Атомарная операция**: защита от прерываний при проверке/установке tx_active
3. **Запуск передачи**: если передача не активна, инициализируется и запускается таймер

### 3.3. Функции приёма данных
```cpp
uint8_t uart_available() {
  uint8_t count;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    count = (rx_head - rx_tail + BUFFER_SIZE) % BUFFER_SIZE;
  }
  return count;
}
```
**Особенности:**
- Атомарное вычисление количества данных в буфере
- Формула учитывает кольцевую природу буфера

```cpp
char uart_read() {
  if (rx_head == rx_tail) return -1;
  
  char b;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    b = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % BUFFER_SIZE;
  }
  return b;
}
```
**Безопасное извлечение данных:**
- Проверка на пустоту буфера
- Атомарное чтение и обновление индекса

## 4. Обработчики прерываний - ядро системы

### 4.1. Обработчик внешнего прерывания (старт-бит)
```cpp
ISR(INT1_vect) {
  if (!rx_active) {
    EIMSK &= ~(1 << INT1); // Отключаем прерывание
    rx_active = true;
    rx_bit_index = 0;
    rx_byte_received = 0;
    
    OCR1B = TCNT1 + timer1_ticks_per_bit + (timer1_ticks_per_bit / 2);
    TIFR1 |= (1 << OCF1B);
    TIMSK1 |= (1 << OCIE1B);
  }
}
```
**Алгоритм обнаружения старта:**
1. **Проверка**: убеждаемся, что приём не активен
2. **Блокировка**: отключаем дальнейшее обнаружение старт-битов
3. **Инициализация**: сброс состояния приёмника
4. **Синхронизация**: установка таймера на середину первого бита (1.5 интервала)

### 4.2. Обработчик передачи (TIMER1_COMPA_vect)
```cpp
ISR(TIMER1_COMPA_vect) {
  OCR1A += timer1_ticks_per_bit;

  if (tx_bit_index == 0) {
    // Фаза 0: Решение и старт-бит
    if (tx_head == tx_tail) {
      tx_active = false;
      TIMSK1 &= ~(1 << OCIE1A);
      return;
    }
    tx_byte_to_send = tx_buffer[tx_tail];
    tx_tail = (tx_tail + 1) % BUFFER_SIZE;
    
    PORTD &= ~(1 << UART_TX_PIN); // Старт-бит (0)
    tx_bit_index++;
    
  } else if (tx_bit_index <= 8) {
    // Фазы 1-8: биты данных
    if (tx_byte_to_send & 1) {
      PORTD |= (1 << UART_TX_PIN); // '1'
    } else {
      PORTD &= ~(1 << UART_TX_PIN); // '0'
    }
    tx_byte_to_send >>= 1;
    tx_bit_index++;
    
  } else {
    // Фаза 9: стоп-бит
    PORTD |= (1 << UART_TX_PIN); // Стоп-бит (1)
    tx_bit_index = 0;
  }
}
```
**Конечный автомат передачи:**
- **Фаза 0**: Проверка наличия данных → отправка старт-бита
- **Фазы 1-8**: Последовательная отправка битов (LSB first)
- **Фаза 9**: Отправка стоп-бита и сброс состояния

### 4.3. Обработчик приёма (TIMER1_COMPB_vect)
```cpp
ISR(TIMER1_COMPB_vect) {
  OCR1B += timer1_ticks_per_bit;

  if (rx_bit_index < 8) {
    // Приём битов данных
    if (PIND & (1 << UART_RX_PIN)) {
      rx_byte_received |= (1 << rx_bit_index);
    }
    rx_bit_index++;
    
  } else {
    // Проверка стоп-бита
    if (PIND & (1 << UART_RX_PIN)) {
      uint8_t next_head = (rx_head + 1) % BUFFER_SIZE;
      if (next_head != rx_tail) {
        rx_buffer[rx_head] = rx_byte_received;
        rx_head = next_head;
      }
    }
    
    rx_active = false;
    TIMSK1 &= ~(1 << OCIE1B);
    EIFR |= (1 << INTF1);
    EIMSK |= (1 << INT1);
  }
}
```
**Алгоритм приёма:**
- **Биты 0-7**: Считывание и упаковка в rx_byte_received
- **Бит 8**: Проверка стоп-бита и сохранение в буфер
- **Завершение**: восстановление обнаружения старт-бита

## 5. Инициализация и демонстрационная логика

### 5.1. Функция setup()
```cpp
void setup() {
  DDRD |= (1 << UART_TX_PIN);  // TX как выход
  PORTD |= (1 << UART_TX_PIN); // Подтяжка к HIGH (idle)
  
  DDRD &= ~(1 << UART_RX_PIN); // RX как вход
  PORTD |= (1 << UART_RX_PIN); // Включение подтяжки

  uart_set_baudrate(9600);

  EICRA |= (1 << ISC11);       // Прерывание по спадающему фронту
  EICRA &= ~(1 << ISC10);
  EIMSK |= (1 << INT1);        // Разрешение прерывания INT1
  
  sei();                       // Глобальное разрешение прерываний
  
  uart_send_string("Start");   // Тестовое сообщение
}
```

### 5.2. Демонстрационный цикл (эхо-сервер)
```cpp
char input_buffer[BUFFER_SIZE];
uint8_t buffer_index = 0;

void loop() {
  if (uart_available()) {
    char c = uart_read();
    
    if (c == '\n' || c == '\r') {
      if (buffer_index > 0) { 
        input_buffer[buffer_index] = '\0'; 
        uart_send_string("Echo: ");
        uart_send_string(input_buffer);
        uart_send_string("\r\n");
      }
      buffer_index = 0; 
    } else {
      if (buffer_index < BUFFER_SIZE - 1) {
        input_buffer[buffer_index++] = c;
      }
    }
  }
}
```

## 6. Соответствие требованиям

### ✅ Полностью выполненные требования:

1. **Асинхронная работа** - TX и RX полностью управляются прерываниями
2. **Кольцевые буферы** - реализованы с проверкой переполнения
3. **Таймерная синхронизация** - точные временные интервалы через Timer1
4. **Внешнее прерывание для старта** - INT1 на спадающем фронте
5. **Прямая работа с регистрами** - только DDRx/PORTx/PINx
6. **Эффективные ISR** - минимальный код в прерываниях
7. **Volatile переменные** - корректное взаимодействие с main

