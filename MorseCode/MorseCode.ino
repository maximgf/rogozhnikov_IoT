//  КОНФИГУРАЦИЯ 
#define PIN_RX 2       // Вход (Interrupt 0)
#define PIN_TX 3       // Выход
#define PIN_DATA 9     // 74HC595 DS
#define PIN_LATCH 10    // 74HC595 ST_CP
#define PIN_CLOCK 11   // 74HC595 SH_CP
#define PIN_LED 13     // Индикация передачи

//  ВРЕМЕННЫЕ ПАРАМЕТРЫ (ms) 
const unsigned long TIME_UNIT = 100; // 1t
const unsigned long DOT_TIME = TIME_UNIT;
const unsigned long DASH_TIME = 3 * TIME_UNIT;
const unsigned long SYMBOL_GAP = TIME_UNIT;      // Пауза внутри буквы
const unsigned long LETTER_GAP = 3 * TIME_UNIT;  // Пауза между буквами
const unsigned long WORD_GAP = 7 * TIME_UNIT;    // Пауза между словами

// Допуски для распознавания (tolerance)
const unsigned long TOLERANCE = 50; 

//  МАРКЕРЫ ПРОТОКОЛА 
const String PROTOCOL_START = "-.-.-"; // Start of Transmission
const String PROTOCOL_END = "...-.-";  // End of Work

//  ТАБЛИЦЫ 
const char* MORSE_LETTERS[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", // A-I
  ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", // J-R
  "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."        // S-Z
};
const char* MORSE_NUMBERS[] = {
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----." // 0-9
};

// Семисегментный шрифт (0-9, A-Z)
const byte SEG_FONT[] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, // 0-9
  0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71, 0x3D, 0x76, 0x06, 0x1E, // A-J
  0x76, 0x38, 0x55, 0x54, 0x3F, 0x73, 0x67, 0x50, 0x6D, 0x78, // K-T
  0x3E, 0x1C, 0x2A, 0x76, 0x6E, 0x5B                          // U-Z
};

//  ПЕРЕМЕННЫЕ TX (Передатчик) 
char txBuffer[64];      
int txHead = 0;
int txTail = 0;
String currentMorseSeq = "";
int txSeqIndex = 0;
bool txFrameActive = false; 
unsigned long txLastTime = 0;

enum TxState { 
  TX_IDLE, 
  TX_SEND_START, 
  TX_SEND_PAYLOAD, 
  TX_SEND_END, 
  TX_SIGNAL_HIGH, 
  TX_WAIT_HIGH,     
  TX_WAIT_LOW,    
  TX_NEXT_CHAR_WAIT 
};
TxState txState = TX_IDLE;
TxState txReturnState = TX_IDLE; // Куда вернуться после отправки символа

//  ПЕРЕМЕННЫЕ RX (Приемник) 
volatile unsigned long rxPulseStart = 0;
volatile unsigned long rxPulseWidth = 0;
volatile bool rxPulseReady = false;
volatile unsigned long rxLastEdge = 0;

String rxBufferSeq = ""; // Накопленные точки/тире
unsigned long rxLastActivity = 0;
bool rxFrameActive = false; 

void setup() {
  Serial.begin(9600);
  
  pinMode(PIN_RX, INPUT);
  pinMode(PIN_TX, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);

  digitalWrite(PIN_TX, LOW);
  
  // Привязка прерывания к смене состояния на RX пине
  attachInterrupt(digitalPinToInterrupt(PIN_RX), rxISR, CHANGE);
  
  Serial.println("System Ready.");
}

void loop() {
  handleSerialInput();
  runTransmitterFSM();
  runReceiverFSM();
}


void handleSerialInput() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    c = toupper(c); 
    
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
      int nextHead = (txHead + 1) % 64;
      if (nextHead != txTail) {
        txBuffer[txHead] = c;
        txHead = nextHead;
      }
    }
  }
}

char peekTxBuffer() {
  if (txHead == txTail) return 0;
  return txBuffer[txTail];
}

void popTxBuffer() {
  if (txHead != txTail) {
    txTail = (txTail + 1) % 64;
  }
}

String charToMorse(char c) {
  if (c >= 'A' && c <= 'Z') return String(MORSE_LETTERS[c - 'A']);
  if (c >= '0' && c <= '9') return String(MORSE_NUMBERS[c - '0']);
  return ""; 
}

void runTransmitterFSM() {
  unsigned long currentMillis = millis();

  switch (txState) {
    case TX_IDLE:
      if (peekTxBuffer() != 0) {
        txState = TX_SEND_START;
      }
      break;

    case TX_SEND_START:
      currentMorseSeq = PROTOCOL_START;
      txSeqIndex = 0;
      txReturnState = TX_SEND_PAYLOAD;
      txState = TX_SIGNAL_HIGH;
      break;

    case TX_SEND_PAYLOAD:
      {
        char c = peekTxBuffer();
        if (c == 0) {
          txState = TX_SEND_END;
          return;
        }
        
        if (c == ' ') {
          popTxBuffer();
          txLastTime = currentMillis;
          txState = TX_NEXT_CHAR_WAIT; 
          return;
        }

        currentMorseSeq = charToMorse(c);
        txSeqIndex = 0;
        popTxBuffer(); 
        txReturnState = TX_SEND_PAYLOAD; 
        txState = TX_SIGNAL_HIGH;
      }
      break;

    case TX_SEND_END:
      currentMorseSeq = PROTOCOL_END;
      txSeqIndex = 0;
      txReturnState = TX_IDLE; 
      txState = TX_SIGNAL_HIGH;
      break;

    // Начало импульса
    case TX_SIGNAL_HIGH:
      {
        if (txSeqIndex >= currentMorseSeq.length()) {
           txLastTime = currentMillis;
           txState = TX_NEXT_CHAR_WAIT;
           return;
        }

        char signal = currentMorseSeq[txSeqIndex];
        digitalWrite(PIN_TX, HIGH);
        digitalWrite(PIN_LED, HIGH);
        txLastTime = currentMillis;
        
        // Переходим в состояние ожидания конца высокого уровня
        txState = TX_WAIT_HIGH; 
      }
      break;

    //  Ожидание окончания импульса (High duration) 
    case TX_WAIT_HIGH: 
      {
        char signal = currentMorseSeq[txSeqIndex];
        unsigned long duration = (signal == '.') ? DOT_TIME : DASH_TIME;
        
        if (currentMillis - txLastTime >= duration) {
          digitalWrite(PIN_TX, LOW);
          digitalWrite(PIN_LED, LOW);
          txLastTime = currentMillis;
          // Переходим в состояние ожидания паузы между точками/тире
          txState = TX_WAIT_LOW; 
        }
      }
      break;

    //  Ожидание окончания паузы внутри символа (Low duration) 
    case TX_WAIT_LOW: 
      if (currentMillis - txLastTime >= SYMBOL_GAP) {
        txSeqIndex++;
        txState = TX_SIGNAL_HIGH; // Возврат к следующему элементу знака
      }
      break;

    case TX_NEXT_CHAR_WAIT:
      if (currentMillis - txLastTime >= LETTER_GAP) {
        txState = txReturnState; 
      }
      break;
  }
}

// RX
// ISR (Interrupt Service Routine)
void rxISR() {
  unsigned long now = millis();
  int state = digitalRead(PIN_RX);

  if (state == LOW) { 
    // Упал в LOW -> закончился импульс HIGH
    rxPulseWidth = now - rxPulseStart;
    rxPulseReady = true; 
    rxLastEdge = now;
  } else {
    // Поднялся в HIGH -> начался импульс
    rxPulseStart = now;
    rxLastEdge = now;
  }
}

void decodeMorseChar(String seq) {
  // Сначала проверяем служебные команды
  if (seq == PROTOCOL_START) {
    rxFrameActive = true;
    Serial.println("\n[RX] Frame START");
    return;
  }
  if (seq == PROTOCOL_END) {
    rxFrameActive = false;
    Serial.println("\n[RX] Frame END");
    return;
  }

  // Если мы не в режиме приема кадра, игнорируем шум
  if (!rxFrameActive) return;

  char decoded = '?';
  
  // Поиск буквы
  for (int i = 0; i < 26; i++) {
    if (seq == MORSE_LETTERS[i]) {
      decoded = 'A' + i;
      displayChar(decoded);
      Serial.print(decoded);
      return;
    }
  }
  // Поиск цифры
  for (int i = 0; i < 10; i++) {
    if (seq == MORSE_NUMBERS[i]) {
      decoded = '0' + i;
      displayChar(decoded);
      Serial.print(decoded);
      return;
    }
  }
}

void displayChar(char c) {
  byte segment = 0;
  if (c >= '0' && c <= '9') segment = SEG_FONT[c - '0'];
  else if (c >= 'A' && c <= 'Z') segment = SEG_FONT[10 + (c - 'A')];
  else segment = 0b01000000; // Тире для неизвестных

  displayByte(segment);
}

void displayByte(byte data) {
  digitalWrite(PIN_LATCH, LOW);
  shiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, data);
  digitalWrite(PIN_LATCH, HIGH);
}

void runReceiverFSM() {
  unsigned long now = millis();

  // 1. Обработка принятого импульса (из ISR)
  if (rxPulseReady) {
    noInterrupts();
    unsigned long width = rxPulseWidth;
    rxPulseReady = false;
    interrupts();

    // Определяем точку или тире с учетом погрешности
    if (width > (DOT_TIME - TOLERANCE) && width < (DOT_TIME + TOLERANCE + 50)) {
      rxBufferSeq += ".";
    } else if (width > (DASH_TIME - TOLERANCE)) {
      rxBufferSeq += "-";
    }
    // Иначе шум
    
    rxLastActivity = now;
  }

  // 2. Обработка пауз (таймауты)
  // Мы проверяем, сколько времени прошло с последнего изменения уровня (rxLastEdge)
  if (digitalRead(PIN_RX) == LOW && rxBufferSeq.length() > 0) {
    unsigned long timeSinceLastPulse = now - rxLastEdge;

    // Если прошло времени как на паузу между буквами -> декодируем
    if (timeSinceLastPulse > (LETTER_GAP - 50)) {
      decodeMorseChar(rxBufferSeq);
      rxBufferSeq = ""; // Очистка
    }
  }
  
  // Опционально: обнаружение паузы между словами (Word Gap)
  if (digitalRead(PIN_RX) == LOW && rxFrameActive) {
     if (now - rxLastEdge > WORD_GAP) {
      Serial.print(" "); 
     }
  }
}