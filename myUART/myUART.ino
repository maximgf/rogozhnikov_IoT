#include <util/atomic.h>

#define UART_TX_PIN PD4 
#define UART_RX_PIN PD3 

// КОЛЬЦЕВЫЕ БУФЕРЫ
#define BUFFER_SIZE 64

// TX буфер: здесь хранятся байты, ожидающие отправки.
volatile char tx_buffer[BUFFER_SIZE];
volatile uint8_t tx_head = 0; // Индекс для записи нового байта в буфер.
volatile uint8_t tx_tail = 0; // Индекс для чтения байта из буфера для отправки.

// RX буфер: здесь хранятся принятые байты, ожидающие обработки программой.
volatile char rx_buffer[BUFFER_SIZE];
volatile uint8_t rx_head = 0; // Индекс для записи принятого байта.
volatile uint8_t rx_tail = 0; // Индекс для чтения байта основной программой.

// ПЕРЕМЕННЫЕ СОСТОЯНИЯ

// Общее
volatile uint16_t timer1_ticks_per_bit; // Количество тиков таймера, соответствующее длительности одного бита.
   
// Состояние передатчика (TX)
volatile bool tx_active = false;      // Флаг, указывающий, что процесс передачи байта активен.
volatile uint8_t tx_byte_to_send;     // Текущий байт, который передаётся побитово.
volatile uint8_t tx_bit_index = 0;    // Счётчик состояний для отправки одного байта.
                                      // 0: Фаза принятия решения и отправки старт-бита.
                                      // 1-8: Отправка 8 бит данных (от младшего к старшему).
                                      // 9: Отправка стоп-бита.

// Состояние приёмника (RX)
volatile bool rx_active = false;      // Флаг, указывающий, что процесс приёма байта активен.
volatile uint8_t rx_byte_received;    // Байт, который собирается из принятых бит.
volatile uint8_t rx_bit_index = 0;    // Счётчик принятых бит данных.
                                      // 0-7: Приём 8 бит данных.
                                      // 8: Сэмплирование стоп-бита.

void uart_set_baudrate(int baudrate) {
  // Рассчитываем, сколько тиков таймера должно пройти за время передачи одного бита.
  timer1_ticks_per_bit = F_CPU / (8UL * baudrate);
  
  // Настройка Timer1
  TCCR1A = 0; // Режим Normal, не используем ШИМ.
  TCCR1B = (1 << CS11); // Устанавливаем предделитель на 8. Таймер начинает тикать.
  TIMSK1 = 0; // Изначально все прерывания от Timer1 выключены.
}

// API ДЛЯ ПЕРЕДАЧИ ДАННЫХ (TX)

bool uart_send(char b) {
  uint8_t next_head = (tx_head + 1) % BUFFER_SIZE;
  if (next_head == tx_tail) {
    return false;
  }

  tx_buffer[tx_head] = b;
  tx_head = next_head;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (!tx_active) {
      tx_active = true;
      tx_bit_index = 0; 
      OCR1A = TCNT1 + 10; 
      TIMSK1 |= (1 << OCIE1A); // Включаем прерывание по совпадению с OCR1A.
    }
  }
  return true;
}

void uart_send_string(const char *msg) {
  while (*msg) {
    while (!uart_send(*msg)) {}
    msg++;
  }
}

// API ДЛЯ ПРИЁМА ДАННЫХ (RX)

uint8_t uart_available() {
  uint8_t count;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    count = (rx_head - rx_tail + BUFFER_SIZE) % BUFFER_SIZE;
  }
  return count;
}

char uart_read() {
  if (rx_head == rx_tail) return -1; // Буфер пуст.
  
  char b;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    b = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % BUFFER_SIZE;
  }
  return b;
}

// ОБРАБОТЧИКИ ПРЕРЫВАНИЙ

ISR(INT1_vect) {
  if (!rx_active) {
    EIMSK &= ~(1 << INT1); 
    rx_active = true;
    rx_bit_index = 0;
    rx_byte_received = 0;
    
    OCR1B = TCNT1 + timer1_ticks_per_bit + (timer1_ticks_per_bit / 2);
    TIFR1 |= (1 << OCF1B);   // Сбрасываем флаг прерывания (на случай, если он уже был установлен).
    TIMSK1 |= (1 << OCIE1B); // Включаем прерывание по совпадению B для сэмплирования бит.
  }
}

ISR(TIMER1_COMPA_vect) {
  
  OCR1A += timer1_ticks_per_bit;

  // Конечный автомат для передачи байта
  if (tx_bit_index == 0) { // Фаза 0: Решение и отправка старт-бита
    // Проверяем, есть ли что-то ещё в буфере для отправки
    if (tx_head == tx_tail) {
      // Буфер пуст. Завершаем передачу.
      tx_active = false;
      TIMSK1 &= ~(1 << OCIE1A); // Отключаем прерывания для TX.
      return;
    }
    // Если есть данные, достаём следующий байт из буфера
    tx_byte_to_send = tx_buffer[tx_tail];
    tx_tail = (tx_tail + 1) % BUFFER_SIZE;
    
    // Отправляем старт-бит (линия в низкий уровень)
    PORTD &= ~(1 << UART_TX_PIN);
    tx_bit_index++;
    
  } else if (tx_bit_index <= 8) { // Фазы 1-8: Отправка 8 бит данных
    if (tx_byte_to_send & 1) {
      PORTD |= (1 << UART_TX_PIN); // '1'
    } else {
      PORTD &= ~(1 << UART_TX_PIN); // '0'
    }
    tx_byte_to_send >>= 1; // Сдвигаем байт вправо, чтобы в следующий раз отправить следующий бит.
    tx_bit_index++;
    
  } else { // Фаза 9: Отправка стоп-бита
    PORTD |= (1 << UART_TX_PIN); // Стоп-бит всегда '1' (линия в высокий уровень).
    tx_bit_index = 0; // Сбрасываем счётчик. В следующем прерывании мы вернёмся в фазу 0
                      // и либо начнём передачу нового байта, либо завершим работу.
  }
}

ISR(TIMER1_COMPB_vect) {
  // Планируем следующее считывание (сэмплирование) через один битовый интервал.
  OCR1B += timer1_ticks_per_bit;

  if (rx_bit_index < 8) { // Принимаем 8 бит данных
    // Считываем состояние пина и устанавливаем соответствующий бит в `rx_byte_received`.
    if (PIND & (1 << UART_RX_PIN)) {
      rx_byte_received |= (1 << rx_bit_index);
    }
    rx_bit_index++;
    
  } else { // Сэмплирование стоп-бита
    // Проверяем, что стоп-бит имеет высокий уровень (как и положено).
    if (PIND & (1 << UART_RX_PIN)) {
      // Если стоп-бит корректен, сохраняем принятый байт в буфер.
      uint8_t next_head = (rx_head + 1) % BUFFER_SIZE;
      if (next_head != rx_tail) { // Проверка, что буфер не переполнен.
        rx_buffer[rx_head] = rx_byte_received;
        rx_head = next_head;
      }
      }
    
    rx_active = false;
    TIMSK1 &= ~(1 << OCIE1B); // Отключаем прерывания по совпадению B.
    EIFR |= (1 << INTF1);     // Сбрасываем флаг внешнего прерывания INT1.
    EIMSK |= (1 << INT1);     // Снова включаем внешнее прерывание для ожидания нового старт-бита.
  }
}


void setup() {
  
  DDRD |= (1 << UART_TX_PIN);
  
  PORTD |= (1 << UART_TX_PIN);

  DDRD &= ~(1 << UART_RX_PIN);
  
  PORTD |= (1 << UART_RX_PIN);

  uart_set_baudrate(9600);

  EICRA |= (1 << ISC11);
  EICRA &= ~(1 << ISC10);
  EIMSK |= (1 << INT1);
  
  sei();
  
  uart_send_string("Start");
}

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
