import serial
import time
import random
from paho.mqtt.client import Client, CallbackAPIVersion

# Настройки UART
SERIAL_PORT = '/dev/ttyUSB1'  # !!! ЗАМЕНИТЕ НА СВОЙ ПОРТ Sensor MCU !!!
BAUD_RATE = 9600

# Настройки MQTT
BROKER = "broker.emqx.io"
LUMINOSITY_TOPIC = "laboratory/greenhouse/luminosity"
STATUS_TOPIC = "laboratory/greenhouse/status"
CLIENT_ID = f'SENSOR_PUB_{random.randint(10000, 99999)}'

def publish_status(client, msg):
    print(f"[PC1] Публикация статуса: {msg}")
    client.publish(STATUS_TOPIC, f"PC1_STATUS: {msg}", qos=1)

def run_pc1():
    print(f"Запуск PC1 (Publisher) с ID: {CLIENT_ID}")
    
    # Инициализация MQTT
    client = Client(callback_api_version=CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)
    try:
        client.connect(BROKER)
        client.loop_start()
        publish_status(client, "MQTT Connected")
    except Exception as e:
        print(f"Ошибка подключения к MQTT: {e}")
        return

    # Инициализация Serial
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)  # Ожидание установления соединения
        publish_status(client, f"Serial Connected to {SERIAL_PORT}")
    except serial.SerialException as e:
        print(f"Ошибка подключения к Serial {SERIAL_PORT}: {e}")
        publish_status(client, f"Serial Error: {e}")
        client.loop_stop()
        return

    # Запуск потоковой передачи на МК
    try:
        ser.write(b's')
        time.sleep(0.1)
        response = ser.readline().decode('utf-8').strip()
        print(f"MCU Response on 's': {response}")
        publish_status(client, "Sensor streaming started")
    except Exception as e:
        print(f"Ошибка отправки команды 's': {e}")
        publish_status(client, "Error starting stream")
        ser.close()
        client.loop_stop()
        return

    # Основной цикл чтения и публикации
    while True:
        try:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()
                if line.startswith("SENSOR_VALUE:"):
                    luminosity_val = line.split(":")[1]
                    # Публикация показаний
                    client.publish(LUMINOSITY_TOPIC, luminosity_val, qos=1)
                    print(f"[PC1] Опубликовано: {luminosity_val} в {LUMINOSITY_TOPIC}")
                else:
                    print(f"[PC1] MCU Raw: {line}")
            time.sleep(0.01) # Небольшая задержка
        except KeyboardInterrupt:
            print("\n[PC1] Завершение работы...")
            ser.write(b'q') # Отправляем команду остановки потока
            ser.close()
            client.loop_stop()
            client.disconnect()
            break
        except Exception as e:
            print(f"[PC1] Произошла ошибка: {e}")
            time.sleep(1)

if __name__ == "__main__":
    run_pc1()