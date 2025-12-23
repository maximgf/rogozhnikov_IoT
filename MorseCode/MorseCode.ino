//  КОНФИГУРАЦИЯ 
#define PIN_RX 2       // Вход
#define PIN_TX 3       // Выход
#define PIN_BUTTON 4   // Кнопка
#define PIN_DATA 9     // 74HC595 DS
#define PIN_LATCH 10   // 74HC595 ST_CP
#define PIN_CLOCK 11   // 74HC595 SH_CP
#define PIN_LED 13     // Индикация передачи

//  ВРЕМЕННЫЕ ПАРАМЕТРЫ (ms) 
const unsigned long TIME_UNIT = 100; // 1t
const unsigned long DOT_TIME = TIME_UNIT;
const unsigned long DASH_TIME = 3 * TIME_UNIT;
const unsigned long SYMBOL_GAP = TIME_UNIT;      // Пауза внутри буквы
const unsigned long LETTER_GAP = 3 * TIME_UNIT;  // Пауза между буквами
const unsigned long WORD_GAP = 7 * TIME_UNIT;    // Пауза между словами
const unsigned long DEBOUNCE_DELAY = 50;         // Антидребезг

// Допуски для распознавания (tolerance)
const unsigned long TOLERANCE = 50; 

//  МАРКЕРЫ ПРОТОКОЛА 
const String PROTOCOL_START = "-.-.-";
const String PROTOCOL_END = "...-.-"; 

//  Режимы и переменные 
enum InputMode {
  MODE_AUTO,      // Serial -> TX (FSM)
  MODE_MANUAL,    // Button -> Logic -> TX (FSM)
  MODE_RAW        // Button -> TX (Direct)
};

InputMode currentMode = MODE_AUTO;

// Переменные для ручного ввода
unsigned long buttonPressStart = 0;
bool buttonPressed = false;      // Текущее логическое состояние нажатия
bool buttonProcessed = false;    // Флаг, что нажатие уже обработано
String manualMorseSeq = "";      // Накопление последовательности
unsigned long lastButtonRelease = 0; // Для определения конца символа

const char* MORSE_LETTERS[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", // A-I
  ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", // J-R
  "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."        // S-Z
};
const char* MORSE_NUMBERS[] = {
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----." // 0-9
};

const byte SEG_FONT[] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, // 0-9
  0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71, 0x3D, 0x76, 0x06, 0x1E, // A-J
  0x76, 0x38, 0x55, 0x54, 0x3F, 0x73, 0x67, 0x50, 0x6D, 0x78, // K-T
  0x3E, 0x1C, 0x2A, 0x76, 0x6E, 0x5B                          // U-Z
};

//  ПЕРЕМЕННЫЕ TX 
char txBuffer[64];
int txHead = 0;
int txTail = 0;
String currentMorseSeq = "";
int txSeqIndex = 0;
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
TxState txReturnState = TX_IDLE;

//  ПЕРЕМЕННЫЕ RX 
volatile unsigned long rxPulseStart = 0;
volatile unsigned long rxPulseWidth = 0;
volatile bool rxPulseReady = false;
volatile unsigned long rxLastEdge = 0;
String rxBufferSeq = ""; 
unsigned long rxLastActivity = 0;
bool rxFrameActive = false; 

//  ФУНКЦИИ 
void rxISR(); 
void displayByte(byte data);
void displayChar(char c);

void setup() {
  Serial.begin(9600);
  
  pinMode(PIN_RX, INPUT);
  pinMode(PIN_TX, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  digitalWrite(PIN_TX, LOW);
  
  attachInterrupt(digitalPinToInterrupt(PIN_RX), rxISR, CHANGE);
  
  Serial.println("System Ready. Modes: !A (Auto), !M (Manual), !R (Raw)");
}

//  Чтение кнопки с Debounce 
bool readButtonDebounced() {
  static unsigned long lastDebounceTime = 0;
  static bool lastButtonState = HIGH;
  static bool buttonState = HIGH;
  
  bool reading = digitalRead(PIN_BUTTON);

  // Если состояние изменилось
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // Если состояние стабильно дольше задержки
    if (reading != buttonState) {
      buttonState = reading;
    }
  }

  lastButtonState = reading;
  
  // Возвращаем true, если кнопка нажата (LOW из-за pullup)
  return (buttonState == LOW);
}

// Вспомогательная функция для отправки ручной последовательности через FSM
void sendManualMorse(String sequence) {
  // Если передатчик занят, игнорируем (или можно добавить в очередь, но здесь упрощаем)
  if (txState != TX_IDLE) return;
  
  Serial.print("Sending Manual: "); Serial.println(sequence);
  
  currentMorseSeq = sequence;
  txSeqIndex = 0;
  
  // Принудительно запускаем FSM на передачу этого символа
  txLastTime = millis();
  txState = TX_SIGNAL_HIGH;
  txReturnState = TX_IDLE;
}

//  Обработка Manual режима 
void handleManualMode() {
  bool isPressed = readButtonDebounced();
  unsigned long now = millis();
  
  // Обработка начала нажатия
  if (isPressed && !buttonPressed) {
    buttonPressed = true;
    buttonPressStart = now;
    digitalWrite(PIN_LED, HIGH); // Визуальная индикация ввода
  }
  
  // Обработка отпускания (конец ввода точки/тире)
  if (!isPressed && buttonPressed) {
    buttonPressed = false;
    unsigned long duration = now - buttonPressStart;
    digitalWrite(PIN_LED, LOW);
    
    // Определение точки или тире
    if (duration < 300) {
      manualMorseSeq += ".";
    } else {
      manualMorseSeq += "-";
    }
    
    lastButtonRelease = now;
    Serial.print("Buf: "); Serial.println(manualMorseSeq);
  }
  
  // Обработка паузы (отправка символа)
  // Если кнопка отпущена, буфер не пуст и прошло время межсимвольной паузы
  if (!isPressed && manualMorseSeq.length() > 0) {
    if (now - lastButtonRelease > LETTER_GAP) {
      sendManualMorse(manualMorseSeq);
      manualMorseSeq = ""; // Очистить локальный буфер
    }
  }
}

//  Обработка Raw режима 
void handleRawMode() {
  // Прямое чтение кнопки (с дебаунсом, чтобы убрать шум контактов)
  bool isPressed = readButtonDebounced();
  
  // Прямая передача на TX 
  // isPressed = true, значит хотим HIGH на выходе
  digitalWrite(PIN_TX, isPressed ? HIGH : LOW);
  
  // Индикация
  digitalWrite(PIN_LED, isPressed ? HIGH : LOW);
}

//  Переключение режимов через Serial 
// Добавьте эту переменную в начало кода к остальным глобальным переменным
bool isSettingMode = false; 

void handleSerialInput() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    // 1. Проверка на префикс команды
    if (c == '!') {
      isSettingMode = true;
      continue; // Ждем следующий символ в следующем проходе цикла
    }

    // 2. Если мы в процессе смены режима
    if (isSettingMode) {
      char mode = toupper(c);
      bool modeChanged = true;

      switch(mode) {
        case 'A': 
          currentMode = MODE_AUTO; 
          Serial.println(F("Mode: AUTO")); 
          break;
        case 'M': 
          currentMode = MODE_MANUAL; 
          Serial.println(F("Mode: MANUAL (Sequence buffered)")); 
          break;
        case 'R': 
          currentMode = MODE_RAW; 
          digitalWrite(PIN_TX, LOW); // Сброс выхода при входе в RAW
          Serial.println(F("Mode: RAW (Direct TX)")); 
          break;
        default:
          Serial.println(F("Unknown Mode"));
          modeChanged = false;
      }

      if (modeChanged) {
        // Очистка всех буферов при смене режима для стабильности
        manualMorseSeq = "";
        txHead = txTail; 
        txState = TX_IDLE;
        digitalWrite(PIN_LED, LOW);
      }

      isSettingMode = false; // Выходим из режима настройки
      continue;
    }

    // 3. Стандартная обработка текста для AUTO режима
    if (currentMode == MODE_AUTO) {
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
}

// Helper functions for TX FSM
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

// TX FSM
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
      txLastTime = currentMillis;
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
        txLastTime = currentMillis;
      }
      break;

    case TX_SEND_END:
      currentMorseSeq = PROTOCOL_END;
      txSeqIndex = 0;
      txReturnState = TX_IDLE; 
      txState = TX_SIGNAL_HIGH;
      txLastTime = currentMillis;
      break;

    case TX_SIGNAL_HIGH:
      {
        if (txSeqIndex >= currentMorseSeq.length()) {
           txLastTime = currentMillis;
           txState = TX_NEXT_CHAR_WAIT;
           return;
        }

        char signal = currentMorseSeq[txSeqIndex];
        digitalWrite(PIN_TX, HIGH);
        // В AUTO режиме LED управляется FSM, 
        // В MANUAL режиме LED уже мигал при вводе, но при передаче можно дублировать
        digitalWrite(PIN_LED, HIGH); 
        txLastTime = currentMillis;
        txState = TX_WAIT_HIGH; 
      }
      break;

    case TX_WAIT_HIGH: 
      {
        char signal = currentMorseSeq[txSeqIndex];
        unsigned long duration = (signal == '.') ? DOT_TIME : DASH_TIME;
        
        if (currentMillis - txLastTime >= duration) {
          digitalWrite(PIN_TX, LOW);
          digitalWrite(PIN_LED, LOW);
          txLastTime = currentMillis;
          txState = TX_WAIT_LOW; 
        }
      }
      break;

    case TX_WAIT_LOW: 
      if (currentMillis - txLastTime >= SYMBOL_GAP) {
        txSeqIndex++;
        txState = TX_SIGNAL_HIGH; 
        txLastTime = currentMillis;
      }
      break;

    case TX_NEXT_CHAR_WAIT:
      if (currentMillis - txLastTime >= LETTER_GAP) {
        txState = txReturnState; 
      }
      break;
  }
}

//  Главный цикл 
void loop() {
  handleSerialInput();
  
  // Обработка входных устройств в зависимости от режима
  switch(currentMode) {
    case MODE_AUTO:
      // Вход обрабатывается внутри handleSerialInput
      break;
      
    case MODE_MANUAL:
      handleManualMode();
      break;
      
    case MODE_RAW:
      handleRawMode();
      break;
  }
  
  // TX FSM запускается только если мы НЕ в raw режиме
  // В RAW режиме мы управляем PIN_TX напрямую
  if (currentMode != MODE_RAW) {
    runTransmitterFSM();
  }
  
  runReceiverFSM();
}

//  RX

void rxISR() {
  unsigned long now = millis();
  int state = digitalRead(PIN_RX);

  if (state == LOW) { 
    rxPulseWidth = now - rxPulseStart;
    rxPulseReady = true; 
    rxLastEdge = now;
  } else {
    rxPulseStart = now;
    rxLastEdge = now;
  }
}

void decodeMorseChar(String seq) {
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

  if (!rxFrameActive && currentMode == MODE_AUTO) return; 
  // Если хотим видеть символы в Raw режиме без заголовков протокола:
  if (!rxFrameActive && currentMode == MODE_RAW) {
     // Разрешаем декодирование в RAW режиме без фрейма для простоты теста
  } else if (!rxFrameActive) {
    return;
  }

  char decoded = '?';
  for (int i = 0; i < 26; i++) {
    if (seq == MORSE_LETTERS[i]) {
      decoded = 'A' + i;
      displayChar(decoded);
      Serial.print(decoded);
      return;
    }
  }
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
  else segment = 0b01000000; 

  displayByte(segment);
}

void displayByte(byte data) {
  digitalWrite(PIN_LATCH, LOW);
  shiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, data); 
  digitalWrite(PIN_LATCH, HIGH);
}

void runReceiverFSM() {
  unsigned long now = millis();

  if (rxPulseReady) {
    noInterrupts();
    unsigned long width = rxPulseWidth;
    rxPulseReady = false;
    interrupts();

    if (width > (DOT_TIME - TOLERANCE) && width < (DOT_TIME + TOLERANCE + 50)) {
      rxBufferSeq += ".";
    } else if (width > (DASH_TIME - TOLERANCE)) {
      rxBufferSeq += "-";
    }
    rxLastActivity = now;
  }

  if (digitalRead(PIN_RX) == LOW && rxBufferSeq.length() > 0) {
    unsigned long timeSinceLastPulse = now - rxLastEdge;

    if (timeSinceLastPulse > (LETTER_GAP - 50)) {
      decodeMorseChar(rxBufferSeq);
      rxBufferSeq = ""; 
    }
  }
}