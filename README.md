# ESP32 Fall Detection - MPU6050 + FreeRTOS + MQTT

## Nội dung
- `Arduino/Fall_Detection.ino`: firmware ESP32, 3 FreeRTOS tasks, 2 queues, MQTT.
- `Web/`: dashboard HTML/CSS/JavaScript, biểu đồ và nút điều khiển.
- `Wokwi/diagram.json`: sơ đồ kết nối tham khảo.
- `Wokwi/libraries.txt`: thư viện Wokwi.

## Kết nối phần cứng
| Thiết bị | Chân | ESP32 |
|---|---|---|
| MPU6050 | VCC | 3V3 |
| MPU6050 | GND | GND |
| MPU6050 | SDA | GPIO 21 |
| MPU6050 | SCL | GPIO 22 |
| LED (qua điện trở 220Ω) | Anode | GPIO 2 |
| LED | Cathode | GND |
| Buzzer | + | GPIO 25 |
| Buzzer | - | GND |

## Cài thư viện Arduino IDE
Cài `PubSubClient` từ Library Manager. `Wire`, `WiFi` và FreeRTOS đã có trong gói ESP32 Arduino core.

## Chạy firmware
1. Mở `Arduino/Fall_Detection.ino`.
2. Chọn board ESP32 Dev Module.
3. Giữ WiFi `Wokwi-GUEST` và mật khẩu rỗng nếu mô phỏng Wokwi.
4. Nạp chương trình, mở Serial Monitor ở 115200 baud.

## Chạy Web dashboard
1. Mở `Web/index.html` bằng trình duyệt hoặc chạy bằng Live Server.
2. Đảm bảo `ROOT` trong `Web/app.js` giống `FALL_TOPIC_ROOT` trong file `.ino`.
3. Dashboard kết nối MQTT qua WebSocket, nhận dữ liệu và publish lệnh điều khiển.

## MQTT topics
- `fall_demo_esp32_741852/sensor`
- `fall_demo_esp32_741852/status`
- `fall_demo_esp32_741852/control/led` (ON/OFF)
- `fall_demo_esp32_741852/control/buzzer` (ON/OFF)

## FreeRTOS tasks
- `Task_ReadData`: Core 1, lấy mẫu MPU6050 mỗi 50 ms, gửi vào `dataQueue` và `dataAlert`.
- `Task_SendData`: Core 0, kết nối WiFi/MQTT và publish dữ liệu.
- `Task_Alert`: Core 1, đánh giá chuỗi va chạm + góc nghiêng, điều khiển LED/Buzzer.

## Lưu ý
- MQTT broker công cộng không có bảo mật và topic có thể bị người khác đọc/ghi. Chỉ dùng để học tập; đổi `FALL_TOPIC_ROOT` thành chuỗi riêng, không gửi dữ liệu nhạy cảm.
- Ngưỡng té ngã trong code là ngưỡng minh họa, phải hiệu chỉnh theo dữ liệu thử nghiệm. MPU6050 chỉ dùng gia tốc kế để ước lượng góc nghiêng nên chuyển động động có thể làm sai số.
- Hệ thống chỉ là mô hình học tập, không thay thế thiết bị y tế hoặc thiết bị an toàn được chứng nhận.
