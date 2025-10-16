import time
import random
from paho.mqtt.client import Client, MQTTMessage, CallbackAPIVersion

BROKER = "broker.emqx.io"
CLIENT_ID = f'MONITOR_{random.randint(10000, 99999)}'
ALL_TOPICS = "laboratory/greenhouse/#" 

def on_connect(client: Client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("[Monitor] Успешное подключение к MQTT-брокеру!")
        client.subscribe(ALL_TOPICS, qos=0)
        print(f"[Monitor] Подписка на топик: {ALL_TOPICS}")
    else:
        print(f"[Monitor] Не удалось подключиться, код возврата {reason_code}")

def on_message(client: Client, userdata, message: MQTTMessage):
    data = message.payload.decode("utf-8")
    topic = message.topic
    print(f"--------------------------------------------------")
    print(f"[Monitor] TOPIC: {topic}")
    print(f"[Monitor] DATA:  {data}")
    if "Luminosity" in topic and int(data) < 100:
        print(f"[Monitor CHECK] Низкая освещённость обнаружена: {data}")
    elif "STATUS" in topic and "Error" in data:
        print(f"[Monitor ALERT] !!! ОШИБКА СИСТЕМЫ !!!")

def run_monitor():
    print(f"Запуск Monitor с ID: {CLIENT_ID}")
    
    client = Client(callback_api_version=CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(BROKER)
    except Exception as e:
        print(f"Ошибка подключения к MQTT: {e}")
        return

    client.loop_start()
    
    try:
        time.sleep(3600) 
    except KeyboardInterrupt:
        print("\n[Monitor] Завершение работы...")
    
    client.loop_stop()
    client.disconnect()
    print("[Monitor] Отключено")

if __name__ == "__main__":
    run_monitor()