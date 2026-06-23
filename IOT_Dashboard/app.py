import paho.mqtt.client as mqtt
import json
import csv
import ssl
import threading
from datetime import datetime
from flask import Flask, render_template, jsonify

# --- CẤU HÌNH MQTT ---
MQTT_BROKER = "2cf8a119a7c74d3891fc09c9ca7136f9.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_TOPIC = "hust/kien/air_quality"
MQTT_USER = "esp32_kien"
MQTT_PASSWORD = "Kien123456@"
CSV_FILE = "long_term_air_quality.csv"

app = Flask(__name__)

# Bộ nhớ đệm RAM chứa 20 điểm dữ liệu gần nhất để gửi cho Web khi load trang
data_history = {
    "labels": [], "temp": [], "hum": [], "pm25": [], "pm10": []
}
MAX_POINTS = 20

# Khởi tạo file CSV
try:
    with open(CSV_FILE, mode='x', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(["Thoi Gian", "Nhiet Do (C)", "Do Am (%)", "PM2.5 (ug/m3)", "PM10 (ug/m3)"])
except FileExistsError:
    pass

# --- CÁC HÀM XỬ LÝ MQTT ---
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("Đã kết nối MQTT Broker để lưu dữ liệu ngầm!")
        client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode('utf-8'))
        temp = payload.get("temperature", 0.0)
        hum = payload.get("humidity", 0.0)
        pm25 = payload.get("pm25", 0)
        pm10 = payload.get("pm10", 0)
        
        current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        short_time = datetime.now().strftime("%H:%M:%S")

        # 1. Ghi vào CSV
        with open(CSV_FILE, mode='a', newline='', encoding='utf-8') as f:
            csv.writer(f).writerow([current_time, temp, hum, pm25, pm10])

        # 2. Cập nhật bộ nhớ đệm (Giữ tối đa 20 điểm)
        if len(data_history["labels"]) >= MAX_POINTS:
            data_history["labels"].pop(0)
            data_history["temp"].pop(0)
            data_history["hum"].pop(0)
            data_history["pm25"].pop(0)
            data_history["pm10"].pop(0)

        data_history["labels"].append(short_time)
        data_history["temp"].append(temp)
        data_history["hum"].append(hum)
        data_history["pm25"].append(pm25)
        data_history["pm10"].append(pm10)

    except Exception as e:
        print(f"Lỗi: {e}")

def start_mqtt():
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    client.tls_set(tls_version=ssl.PROTOCOL_TLSv1_2)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    client.loop_forever()

# --- CÁC ĐƯỜNG DẪN CỦA WEB SERVER (FLASK) ---
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/history')
def get_history():
    # API cung cấp dữ liệu lịch sử cho Web
    return jsonify(data_history)

if __name__ == '__main__':
    # Chạy MQTT ở một luồng riêng biệt để không làm treo Web
    threading.Thread(target=start_mqtt, daemon=True).start()
    print("Mở trình duyệt và truy cập: http://127.0.0.1:5000")
    app.run(host='0.0.0.0', port=5000, debug=False)