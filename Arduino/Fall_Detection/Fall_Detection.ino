#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// -------------------- WIFI / MQTT --------------------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;

// Change this to a unique string to avoid topic collisions on a public broker.
#define FALL_TOPIC_ROOT "fall_demo_esp32_741852"

const char* TOPIC_SENSOR = FALL_TOPIC_ROOT "/sensor";
const char* TOPIC_STATUS = FALL_TOPIC_ROOT "/status";
const char* TOPIC_LED = FALL_TOPIC_ROOT "/control/led";
const char* TOPIC_BUZZER = FALL_TOPIC_ROOT "/control/buzzer";

// -------------------- HARDWARE PINS --------------------
constexpr int I2C_SDA = 21;
constexpr int I2C_SCL = 22;
constexpr int LED_PIN = 18;
constexpr int BUZZER_PIN = 16;
constexpr uint8_t MPU_ADDR = 0x68;

// -------------------- MPU6050 REGISTERS --------------------
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;

// ±2g range => 16384 LSB/g
constexpr float ACCEL_SCALE = 16384.0f;

// -------------------- FALL PARAMETERS --------------------
// Tune these thresholds using real measurements. They are demo values.
constexpr float IMPACT_THRESHOLD_G = 2.20f;
constexpr float TILT_THRESHOLD_DEG = 55.0f;
constexpr uint32_t FALL_SEQUENCE_WINDOW_MS = 2000;
constexpr uint32_t ALARM_HOLD_MS = 5000;

// -------------------- DATA STRUCTURE / QUEUES --------------------
typedef struct {
  float ax;
  float ay;
  float az;
  float accMagnitude;
  float roll;
  float pitch;
  uint32_t timestamp;
} SensorData;

QueueHandle_t dataQueue = nullptr; // sensor -> MQTT task
QeueHandle_t dataAlert = nullptr; // sensor -> alert task (latest sample)

// -------------------- MQTT CLIENT --------------------
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Shared command states are written by MQTT callback and read by Alert task.
volatile bool manualLed = false;
volatile bool manualBuzzer = false;
volatile bool fallDetected = false;
volatile bool mpuReady = false;

TaskHandle_t readTaskHandle = nullptr;
TaskHandle_t sendTaskHandle = nullptr;
TaskHandle_t alertTaskHandle = nullptr;

// -------------------- MPU6050 LOW-LEVEL FUNCTIONS --------------------
bool writeMPURegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readMPURegisters(uint8_t startReg, uint8_t* buffer, size_t length) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) return false;

  size_t received = Wire.requestFrom((int)MPU_ADDR, (int)length, (int)true);
  if (received != length) return false;

  for (size_t i = 0; i < length; i++) buffer[i] = Wire.read();
  return true;
}

bool initMPU6050() {
  // Wake up MPU6050; select ±2g accelerometer range.
  if (!writeMPURegister(REG_PWR_MGMT_1, 0x00)) return false;
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!writeMPURegister(REG_ACCEL_CONFIG, 0x00)) return false;
  return true;
}

bool readSensor(SensorData &out) {
  uint8_t raw[6];
  if (!readMPURegisters(REG_ACCEL_XOUT_H, raw, sizeof(raw))) return false;

  int16_t rawAx = (int16_t)((raw[0] << 8) | raw[1]);
  int16_t rawAy = (int16_t)((raw[2] << 8) | raw[3]);
  int16_t rawAz = (int16_t)((raw[4] << 8) | raw[5]);

  out.ax = rawAx / ACCEL_SCALE;
  out.ay = rawAy / ACCEL_SCALE;
  out.az = rawAz / ACCEL_SCALE;

  out.accMagnitude = sqrtf(out.ax * out.ax + out.ay * out.ay + out.az * out.az);

  // Tilt estimated from accelerometer. Dynamic acceleration can affect it.
  out.roll = atan2f(out.ay, out.az) * 180.0f / PI;
  out.pitch = atan2f(-out.ax, sqrtf(out.ay * out.ay + out.az * out.az))
              * 180.0f / PI;
  out.timestamp = millis();
  return true;
}

// -------------------- WIFI / MQTT --------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();
  message.toUpperCase();

  if (strcmp(topic, TOPIC_LED) == 0) {
    manualLed = (message == "ON" || message == "1" || message == "TRUE");
    Serial.printf("[MQTT] Manual LED: %s\n", manualLed ? "ON" : "OFF");
  } else if (strcmp(topic, TOPIC_BUZZER) == 0) {
    manualBuzzer = (message == "ON" || message == "1" || message == "TRUE");
    Serial.printf("[MQTT] Manual Buzzer: %s\n", manualBuzzer ? "ON" : "OFF");
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Connection timeout; will retry.");
  }
}

void connectMQTT() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  String clientId = "ESP32-Fall-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  Serial.print("[MQTT] Connecting...");
  if (mqtt.connect(clientId.c_str())) {
    Serial.println(" connected");
    mqtt.subscribe(TOPIC_LED);
    mqtt.subscribe(TOPIC_BUZZER);
    mqtt.publish(TOPIC_STATUS, "ONLINE", true);
  } else {
    Serial.printf(" failed, state=%d\n", mqtt.state());
  }
}

// -------------------- TASK 1: READ SENSOR (CORE 1) --------------------
void Task_ReadData(void *parameter) {
  SensorData sample;

  for (;;) {
    if (mpuReady && readSensor(sample)) {
      Serial.printf(
        "AX=%+.2f g | AY=%+.2f g | AZ=%+.2f g | A=%.2f g | Roll=%+.1f deg | Pitch=%+.1f deg\n",
        sample.ax, sample.ay, sample.az, sample.accMagnitude,
        sample.roll, sample.pitch
      );

      // Queue for MQTT. If full, discard the oldest sample then insert latest.
      if (xQueueSend(dataQueue, &sample, 0) != pdPASS) {
        SensorData discarded;
        xQueueReceive(dataQueue, &discarded, 0);
        xQueueSend(dataQueue, &sample, 0);
      }

      // Alert queue length is 1: keep the newest sensor sample.
      xQueueOverwrite(dataAlert, &sample);
    } else if (!mpuReady) {
      Serial.println("[MPU6050] Sensor not ready.");
    } else {
      Serial.println("[MPU6050] Read error.");
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // sample every 50 ms
  }
}

// -------------------- TASK 2: SEND MQTT (CORE 0) --------------------
void Task_SendData(void *parameter) {
  SensorData sample;
  char json[256];

  for (;;) {
    connectWiFi();
    connectMQTT();

    if (mqtt.connected()) {
      mqtt.loop();

      if (xQueueReceive(dataQueue, &sample, pdMS_TO_TICKS(20)) == pdPASS) {
        snprintf(
          json, sizeof(json),
          "{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
          "\"accMagnitude\":%.3f,\"roll\":%.2f,\"pitch\":%.2f,"
          "\"fall\":%s,\"uptime\":%lu}",
          sample.ax, sample.ay, sample.az,
          sample.accMagnitude, sample.roll, sample.pitch,
          fallDetected ? "true" : "false",
          (unsigned long)sample.timestamp
        );

        if (!mqtt.publish(TOPIC_SENSOR, json)) {
          Serial.println("[MQTT] Publish failed.");
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Keep MQTT network state machine serviced frequently.
    if (mqtt.connected()) mqtt.loop();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// -------------------- TASK 3: FALL ALERT (CORE 1) --------------------
void Task_Alert(void *parameter) {
  SensorData sample;
  uint32_t lastImpactTime = 0;
  uint32_t alarmUntil = 0;
  bool impactSeen = false;
  bool automaticAlarm = false;

  for (;;) {
    if (xQueueReceive(dataAlert, &sample, portMAX_DELAY) == pdPASS) {
      uint32_t now = millis();

      // Step 1: detect a strong impact.
      if (sample.accMagnitude >= IMPACT_THRESHOLD_G) {
        lastImpactTime = now;
        impactSeen = true;
      }

      // Step 2: if a large tilt follows the impact within the time window,
      // classify it as a possible fall.
      float maxTilt = fmaxf(fabsf(sample.roll), fabsf(sample.pitch));
      if (impactSeen && (now - lastImpactTime <= FALL_SEQUENCE_WINDOW_MS)
          && maxTilt >= TILT_THRESHOLD_DEG) {
        automaticAlarm = true;
        alarmUntil = now + ALARM_HOLD_MS;
        impactSeen = false;
        fallDetected = true;
        Serial.println("[ALERT] POSSIBLE FALL DETECTED!");
      }

      if (impactSeen && now - lastImpactTime > FALL_SEQUENCE_WINDOW_MS) {
        impactSeen = false;
      }

      if (automaticAlarm && (int32_t)(now - alarmUntil) >= 0) {
        automaticAlarm = false;
        fallDetected = false;
      }

      // Manual control can turn each actuator on/off independently.
      digitalWrite(LED_PIN, (automaticAlarm || manualLed) ? HIGH : LOW);
      digitalWrite(BUZZER_PIN, (automaticAlarm || manualBuzzer) ? HIGH : LOW);
    }
  }
}

// -------------------- SETUP / LOOP --------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  mpuReady = initMPU6050();

  Serial.println("\n=== ESP32 FALL DETECTION ===");
  Serial.printf("MPU6050: %s\n", mpuReady ? "OK" : "NOT FOUND");

  dataQueue = xQueueCreate(10, sizeof(SensorData));
  dataAlert = xQueueCreate(1, sizeof(SensorData)); // overwrite queue: latest sample

  if (dataQueue == nullptr || dataAlert == nullptr) {
    Serial.println("[ERROR] Queue creation failed. Restarting...");
    delay(1000);
    ESP.restart();
  }

  // ESP32 dual-core task placement as required by the lab.
  BaseType_t ok1 = xTaskCreatePinnedToCore(
    Task_ReadData, "Task_ReadData", 4096, nullptr, 2, &readTaskHandle, 1
  );
  BaseType_t ok2 = xTaskCreatePinnedToCore(
    Task_SendData, "Task_SendData", 6144, nullptr, 1, &sendTaskHandle, 0
  );
  BaseType_t ok3 = xTaskCreatePinnedToCore(
    Task_Alert, "Task_Alert", 4096, nullptr, 3, &alertTaskHandle, 1
  );

  Serial.printf("Task creation: Read=%d Send=%d Alert=%d\n", ok1, ok2, ok3);
}

void loop() {
  // All application work runs in FreeRTOS tasks.
  vTaskDelay(pdMS_TO_TICKS(1000));
}