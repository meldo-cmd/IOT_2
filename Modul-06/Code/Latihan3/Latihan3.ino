#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// Kredensial Jaringan Wi-Fi (Gunakan Hotspot Pribadi Anda)
const char* ssid = "blablabla";
const char* password = "yayayaya";

// Konfigurasi Broker MQTT Publik
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;

// IDENTITAS KLIEN (Wajib Unik menggunakan NIM)
const char* clientId = "ESP8266_LabIoT_25782041_Tugas";

// Definisi Topik Mandiri
const char* topicTelemetry = "kampus/labiot/25782041/telemetri";
const char* topicControl   = "kampus/labiot/25782041/kendali";
const char* topicAlert     = "kampus/labiot/25782041/alert";
const char* topicStatus    = "kampus/labiot/25782041/status";

// Konfigurasi Pin Perangkat Keras
const byte dhtPin = 2;   // Pin D4 (GPIO 2)
const byte ledPin = 5;   // Pin D1 (GPIO 5)
#define DHTTYPE DHT11

DHT dht(dhtPin, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastTelemetryTime = 0;
const unsigned long telemetryInterval = 4000; // Siklus kirim 4 detik

// State Latch untuk mencegah spamming alert berulang pada rentang suhu panas
bool alertSent = false; 

// Fungsi Callback untuk menerima perintah kendali asinkron dari operator
void callback(char* topic, byte* payload, unsigned int length) {
  char incomingBuffer[length + 1];
  memcpy(incomingBuffer, payload, length);
  incomingBuffer[length] = '\0';

  Serial.println("----------------------------------------");
  Serial.printf("[KONTROL DITERIMA] Topik: %s | Payload: %s\n", topic, incomingBuffer);

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, incomingBuffer);
  if (err) {
    Serial.println("Gagal membedah payload JSON!");
    return;
  }

  // Evaluasi perintah manual override LED jika format JSON valid
  if (doc.containsKey("led")) {
    int val = doc["led"];
    digitalWrite(ledPin, val == 1 ? HIGH : LOW);
    Serial.printf("--> [MANUAL OVERRIDE] Status LED diubah: %s\n", val == 1 ? "MENYALA" : "PADAM");
  }
  Serial.println("----------------------------------------");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke MQTT Broker...");
    
    // Konfigurasi Last Will and Testament (LWT)
    // Topik: status, QoS: 1, Retain: true, Pesan: OFFLINE
    const char* lwtTopic = topicStatus;
    int lwtQos = 1;
    bool lwtRetain = true;
    const char* lwtMessage = "{\"status\":\"OFFLINE\",\"keterangan\":\"Perangkat Terputus Tak Terduga\"}";

    if (client.connect(clientId, nullptr, nullptr, lwtTopic, lwtQos, lwtRetain, lwtMessage)) {
      Serial.println(" Sukses Terhubung!");
      
      // Daftarkan kembali subskripsi topik kendali
      client.subscribe(topicControl);
      Serial.printf("Subskripsi Topik Kendali: %s\n", topicControl);

      // Publikasikan status ONLINE sesaat setelah berhasil terhubung
      StaticJsonDocument<256> statusDoc;
      statusDoc["status"] = "ONLINE";
      statusDoc["keterangan"] = "Perangkat Beroperasi Normal";
      char statusBuffer[256];
      serializeJson(statusDoc, statusBuffer);
      client.publish(topicStatus, statusBuffer, true); // Retain true untuk status aktif
      
    } else {
      Serial.printf(" Gagal (rc=%d). Coba lagi dalam 4 detik...\n", client.state());
      delay(4000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Terkoneksi!");
  Serial.print("Alamat IP Lokal: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Memelihara sesi TCP dan memproses callback masuk

  unsigned long now = millis();
  if (now - lastTelemetryTime >= telemetryInterval) {
    lastTelemetryTime = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      // Rule Engine: Klasifikasi Status Lingkungan (Ambang batas 31.0°C)
      String kondisiSuhu = (t <= 31.0) ? "NORMAL" : "PANAS";

      // 1. Publikasi Telemetri Rutin berformat JSON
      StaticJsonDocument<256> docOut;
      docOut["node_id"] = clientId;
      docOut["suhu"] = serialized(String(t, 2));
      docOut["kelembapan"] = serialized(String(h, 2));
      docOut["kondisi_suhu"] = kondisiSuhu;
      docOut["status_led"] = digitalRead(ledPin);

      char buffer[256];
      serializeJson(docOut, buffer);
      bool success = client.publish(topicTelemetry, buffer);
      if (success) {
        Serial.printf("[TELEMETRI TERKIRIM] %s\n", buffer);
      } else {
        Serial.println("[ERROR] Gagal memublikasikan telemetri!");
      }

      // 2. Kanal Peringatan Bahaya Mandiri (Dedicated Alert Topic & Safety Engine)
      if (t > 31.0) {
        if (!alertSent) {
          StaticJsonDocument<256> alertDoc;
          alertDoc["status"] = "BAHAYA";
          alertDoc["parameter"] = "OVERHEAT";
          alertDoc["nilai_suhu"] = serialized(String(t, 2));

          char alertBuffer[256];
          serializeJson(alertDoc, alertBuffer);
          
          if (client.publish(topicAlert, alertBuffer)) {
            Serial.printf("[ALERT TERKIRIM] -> Suhu Kritis Terdeteksi: %.2f°C\n", t);
          }
          
          // Otomatis nyalakan LED indikator bahaya secara lokal
          digitalWrite(ledPin, HIGH);
          alertSent = true; // Latch diaktifkan agar pesan tidak spamming setiap detik
        }
      } else {
        // Reset state latch jika suhu kembali normal
        if (alertSent) {
          Serial.println("[INFO] Kondisi suhu kembali normal.");
          alertSent = false;
        }
      }
    } else {
      Serial.println("Peringatan: Gagal membaca data dari sensor DHT!");
    }
  }
}