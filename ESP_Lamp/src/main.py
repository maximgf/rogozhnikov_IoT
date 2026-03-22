import time
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion
import questionary
from datetime import datetime

def run_scenario(topic):
    client = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2)
    
    try:
        client.connect("broker.hivemq.com", 1883, 60)
        client.loop_start()
    except Exception as e:
        print(f"Ошибка подключения к MQTT: {e}")
        return

    duration = 20  # Текущая длительность ON-интервала в секундах (20..10, затем сброс)
    last_sent_state = None
    
    try:
        print(f"Сценарий запущен для топика: {topic}")
        print("Нажмите Ctrl+C для выхода.")
        
        while True:
            now = datetime.now()
            current_sec = now.second
            
            # ON-интервал симметричен относительно 30-й секунды
            half = duration / 2
            start_sec = 30 - half
            end_sec = 30 + half
            
            current_state = "ON" if start_sec <= current_sec < end_sec else "OFF"
            
            # Публикуем только при смене состояния
            if current_state != last_sent_state:
                client.publish(topic, current_state, qos=1, retain=True)
                last_sent_state = current_state
                print(f"[{now.strftime('%H:%M:%S')}] >>> СЕТЬ: Отправлено {current_state}")

            # Локальный лог раз в секунду
            if now.microsecond < 100000:
                if current_state == "ON":
                    print(f"[{now.strftime('%H:%M:%S')}] Светильник ВКЛ | Интервал: {start_sec:.1f}-{end_sec:.1f} сек.")
                else:
                    print(f"[{now.strftime('%H:%M:%S')}] Светильник ВЫКЛ")
            
            # Каждую минуту уменьшаем длительность на 1 сек, при достижении 10 — сброс
            if current_sec == 0:
                duration -= 1
                if duration < 10:
                    duration = 20
                print(f"\n--- Новая минута. Установлена длительность: {duration} сек. ---")
                time.sleep(1.1)  # Защита от повторного срабатывания на 0-й секунде
                
            time.sleep(0.05)
            
    except KeyboardInterrupt:
        print("\nОстановка сценария...")
        client.publish(topic, "OFF")
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    topic_id = questionary.text(
        "Введите ID (топик) лампочки:",
        default="secrethashlamp8126371726369213697/default/power"
    ).ask()
    
    if topic_id:
        run_scenario(topic_id)