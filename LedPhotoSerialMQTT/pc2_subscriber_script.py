import serial
import time
import random
from paho.mqtt.client import Client, MQTTMessage, CallbackAPIVersion

# Настройки UART
SERIAL_PORT = '/dev/ttyUSB0' # !!! ЗАМЕНИТЕ НА СВОЙ ПОРТ Actuator MCU !!!
BAUD_RATE = 9600

# Настройки MQTT
BROKER = "broker.emqx.io"
LUMINOSITY_TOPIC = "laboratory/greenhouse/luminosity"
STATUS_TOPIC = "laboratory/greenhouse/status"
CLIENT_ID = f'ACTUATOR_SUB_{random.randint(10000, 99999)}'
THRESHOLD = 1000 # Порог освещённости

# Переменные состояния
current_led_state = "unknown"
ser = None
client = None

def publish_status(client, msg):
    print(f"[PC2] Публикация статуса: {msg}")
    client.publish(STATUS_TOPIC, f"PC2_STATUS: {msg}", qos=1)

def send_command_to_mcu(command):
    global current_led_state
    if ser is None or not ser.is_open:
        print("[PC2] Serial не подключен для отправки команды.")
        return

    command_bytes = command.encode()
    ser.write(command_bytes)
    print(f"[PC2] Отправлено МК-исполнителю: {command}")
    
    # Ожидание и чтение ответа от МК
    time.sleep(0.1) 
    if ser.in_waiting > 0:
        response = ser.readline().decode('utf-8').strip()
        print(f"[PC2] MCU Response: {response}")
        publish_status(client, f"CMD_SENT:{command}, MCU_RESP:{response}")
        
        if response == "LED_GOES_ON":
            current_led_state = "on"
        elif response == "LED_GOES_OFF":
            current_led_state = "off"
        elif response == "LED_WILL_BLINK":
            current_led_state = "blinking"
    else:
        publish_status(client, f"CMD_SENT:{command}, NO_MCU_RESP")

def on_connect(client: Client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("[PC2] Успешное подключение к MQTT-брокеру!")
        client.subscribe(LUMINOSITY_TOPIC, qos=1)
        publish_status(client, "MQTT Connected and Subscribed")
    else:
        print(f"[PC2] Не удалось подключиться, код возврата {reason_code}")
        publish_status(client, f"MQTT Connection Failed: {reason_code}")

def on_message(client: Client, userdata, message: MQTTMessage):
    global current_led_state
    try:
        luminosity_str = message.payload.decode("utf-8")
        luminosity_val = int(luminosity_str)
        topic = message.topic
        print(f"[PC2] Получено: {luminosity_val} из топика {topic}")
        publish_status(client, f"Luminosity Recv: {luminosity_val}")
        
        # Логика управления освещением
        if luminosity_val < THRESHOLD and current_led_state != "on":
            print("[PC2] Освещенность низкая, Включаю LED (u)")
            send_command_to_mcu('u')
        elif luminosity_val >= THRESHOLD and current_led_state != "off":
            print("[PC2] Освещенность высокая, Выключаю LED (d)")
            send_command_to_mcu('d')

    except ValueError:
        print(f"[PC2] Некорректные данные: {message.payload.decode('utf-8')}")
    except Exception as e:
        print(f"[PC2] Ошибка в on_message: {e}")

def run_pc2():
    global ser, client
    print(f"Запуск PC2 (Subscriber/Actuator) с ID: {CLIENT_ID}")
    
    # Инициализация Serial
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        print(f"Serial Connected to {SERIAL_PORT}")
    except serial.SerialException as e:
        print(f"Ошибка подключения к Serial {SERIAL_PORT}: {e}")
        return

    # Инициализация MQTT
    client = Client(callback_api_version=CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(BROKER)
    except Exception as e:
        print(f"Ошибка подключения к MQTT: {e}")
        ser.close()
        return

    client.loop_start()
    
    try:
        # Удерживаем скрипт в рабочем состоянии
        while True:
            time.sleep(0.1)
    except KeyboardInterrupt:
        print("\n[PC2] Завершение работы...")
    
    client.loop_stop()
    client.disconnect()
    ser.close()
    print("[PC2] Отключено")

if __name__ == "__main__":
    run_pc2()