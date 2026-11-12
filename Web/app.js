// Must match FALL_TOPIC_ROOT in Arduino/Fall_Detection.ino
const ROOT = "fall_demo_esp32_741852";
const TOPIC_SENSOR = `${ROOT}/sensor`;
const TOPIC_STATUS = `${ROOT}/status`;
const TOPIC_LED = `${ROOT}/control/led`;
const TOPIC_BUZZER = `${ROOT}/control/buzzer`;

// EMQX public MQTT over WebSocket (unencrypted demo endpoint).
const MQTT_URL = "ws://broker.emqx.io:8083/mqtt";

document.getElementById("topicLabel").textContent = TOPIC_SENSOR;

const connectionEl = document.getElementById("connection");
const client = mqtt.connect(MQTT_URL, {
  clientId: "web_fall_" + Math.random().toString(16).slice(2),
  clean: true,
  connectTimeout: 8000,
  reconnectPeriod: 2000
});

client.on("connect", () => {
  connectionEl.textContent = "MQTT đã kết nối";
  connectionEl.className = "pill online";
  client.subscribe([TOPIC_SENSOR, TOPIC_STATUS]);
});
client.on("reconnect", () => {
  connectionEl.textContent = "Đang kết nối lại...";
  connectionEl.className = "pill offline";
});
client.on("error", err => console.error("MQTT error:", err));
client.on("message", (topic, payload) => {
  const message = payload.toString();
  if (topic === TOPIC_STATUS) return;
  if (topic === TOPIC_SENSOR) {
    try { updateDashboard(JSON.parse(message)); }
    catch (e) { console.error("JSON không hợp lệ:", e, message); }
  }
});

function setText(id, value, digits=2) {
  document.getElementById(id).textContent =
    Number.isFinite(Number(value)) ? Number(value).toFixed(digits) : "--";
}

const labels = [];
const accX = [], accY = [], accZ = [];
const rollData = [], pitchData = [];

function makeChart(canvasId, datasets, min, max) {
  return new Chart(document.getElementById(canvasId), {
    type: "line",
    data: { labels, datasets: datasets.map(d => ({
      label: d.label, data: d.data, borderWidth: 2, pointRadius: 0,
      tension: 0.2, fill: false
    }))},
    options: {
      responsive: true, animation: false,
      interaction: { intersect: false, mode: "index" },
      scales: { y: { min, max } },
      plugins: { legend: { position: "bottom" } }
    }
  });
}
const accelerationChart = makeChart("accChart", [
  {label:"Ax",data:accX},{label:"Ay",data:accY},{label:"Az",data:accZ}
], -3, 3);
const angleChart = makeChart("angleChart", [
  {label:"Roll",data:rollData},{label:"Pitch",data:pitchData}
], -180, 180);

function updateDashboard(d) {
  setText("ax", d.ax); setText("ay", d.ay); setText("az", d.az);
  setText("acc", d.accMagnitude); setText("roll", d.roll, 1); setText("pitch", d.pitch, 1);

  const isFall = d.fall === true || d.fall === "true";
  const status = document.getElementById("fallStatus");
  const panel = document.querySelector(".status-panel");
  status.textContent = isFall ? "PHÁT HIỆN DẤU HIỆU TÉ NGÃ" : "Hoạt động bình thường";
  panel.classList.toggle("alert", isFall);
  document.getElementById("statusIcon").textContent = isFall ? "!" : "●";

  labels.push(new Date().toLocaleTimeString());
  accX.push(Number(d.ax)); accY.push(Number(d.ay)); accZ.push(Number(d.az));
  rollData.push(Number(d.roll)); pitchData.push(Number(d.pitch));
  if (labels.length > 30) {
    labels.shift(); accX.shift(); accY.shift(); accZ.shift();
    rollData.shift(); pitchData.shift();
  }
  accelerationChart.update();
  angleChart.update();
  document.getElementById("lastUpdate").textContent =
    "Cập nhật lúc: " + new Date().toLocaleTimeString();
}

const ledBtn = document.getElementById("ledBtn");
const buzzerBtn = document.getElementById("buzzerBtn");
let ledOn = false, buzzerOn = false;

function configureButton(button, label, enabled) {
  button.textContent = (enabled ? "TẮT " : "BẬT ") + label;
  button.className = enabled ? "on" : "off";
}
ledBtn.addEventListener("click", () => {
  ledOn = !ledOn;
  client.publish(TOPIC_LED, ledOn ? "ON" : "OFF");
  configureButton(ledBtn, "LED", ledOn);
  document.getElementById("ledState").textContent = ledOn ? "Đã gửi lệnh bật" : "Đã gửi lệnh tắt";
});
buzzerBtn.addEventListener("click", () => {
  buzzerOn = !buzzerOn;
  client.publish(TOPIC_BUZZER, buzzerOn ? "ON" : "OFF");
  configureButton(buzzerBtn, "CÒI", buzzerOn);
  document.getElementById("buzzerState").textContent = buzzerOn ? "Đã gửi lệnh bật" : "Đã gửi lệnh tắt";
});
