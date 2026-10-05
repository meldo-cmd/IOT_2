#include <painlessMesh.h>
#include <DHT.h>
#include <ArduinoJson.h>

#define MESH_PREFIX   "iyaiyadeh"       // Nama jaringan Mesh bersama
#define MESH_PASSWORD "bismillah"  // Kata sandi jaringan Mesh
#define MESH_PORT     2222               // Port komunikasi TCP Mesh

// Konfigurasi pin dan tipe sensor DHT
#define DHTPIN 2       // Pin D4 (GPIO 2)
#define DHTTYPE DHT22  // Ganti DHT22 jika menggunakan varian DHT22
DHT dht(DHTPIN, DHTTYPE);

// Identitas unik node untuk mempermudah identifikasi teks
const char* nodeName = "Node-3"; 

Scheduler userScheduler;
painlessMesh mesh;

// Prototipe fungsi pengiriman pesan berkala
void sendMessage();
Task taskSendMessage(TASK_SECOND * 3, TASK_FOREVER, &sendMessage);

void sendMessage() {
  // Pembacaan fisik data suhu dan kelembapan dari sensor DHT
  float suhu = dht.readTemperature();
  float kelembapan = dht.readHumidity();

  // Validasi pembacaan sensor
  if (isnan(suhu) || isnan(kelembapan)) {
    Serial.println("[DHT Error] Gagal membaca data dari sensor DHT!");
    return;
  }

  // Rangkai data telemetri ke dalam format JSON menggunakan ArduinoJson
  StaticJsonDocument<200> doc;
  doc["node"] = nodeName;
  doc["chipId"] = mesh.getNodeId();
  doc["suhu"] = suhu;
  doc["kelembapan"] = kelembapan;

  String msg;
  serializeJson(doc, msg);

  // Siarkan paket JSON ke seluruh jaringan mesh (multi-hop broadcast)
  mesh.sendBroadcast(msg);

  Serial.print("[KIRIM MESH] ");
  Serial.println(msg);
}

// Callback otomatis saat menerima pesan dari node manapun di jaringan mesh (Modifikasi Latihan 3)
void receivedCallback(uint32_t from, String &msg) {
  // Parsing muatan teks JSON yang diterima
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, msg);

  if (!error) {
    const char* sender = doc["node"];
    uint32_t chipId   = doc["chipId"];
    float suhu        = doc["suhu"];
    float kelembapan  = doc["kelembapan"];

    // Ambang batas suhu kritis (32.0 °C)
    if (suhu > 32.0) {
      // Jika suhu di atas 32.0 °C, cetak kartu telemetri penuh
      Serial.println("========================================");
      Serial.printf("[TERIMA DARI] %s (Node ID: %u | Chip ID: %u)\n", sender, from, chipId);
      Serial.printf("Suhu       : %.2f °C\n", suhu);
      Serial.printf("Kelembapan : %.2f %%\n", kelembapan);
      Serial.println("========================================");
    } else {
      // Jika suhu normal (<= 32.0 °C), cetak status singkat satu baris
      Serial.printf("[NORMAL] Telemetri dari Node %u aman.\n", chipId);
    }

  } else {
    Serial.printf("[TERIMA DATA MENTAH DARI %u]: %s\n", from, msg.c_str());
  }
}

// Callback otomatis saat ada perangkat baru bergabung ke mesh
void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("--> Koneksi Baru Terdeteksi! Node ID: %u\n", nodeId);
}

// Callback otomatis saat topologi rantai mesh berubah (node keluar/masuk)
void changedConnectionCallback() {
  Serial.println("--> Topologi rantai mesh telah diperbarui");
}

void setup() {
  Serial.begin(115200);

  // Inisialisasi sensor DHT
  dht.begin();

  // Atur level log debugging (hanya tampilkan error dan status startup)
  mesh.setDebugMsgTypes(ERROR | STARTUP);

  // Inisialisasi jaringan mesh
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  
  // Daftarkan event callback
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);

  // Jadwalkan tugas pengiriman data berkala menggunakan TaskScheduler
  userScheduler.addTask(taskSendMessage);
  taskSendMessage.enable();

  Serial.printf("Mesh Node [%s] Berjalan. Menunggu pembentukan topologi...\n", nodeName);
}

void loop() {
  // Wajib dipanggil rutin untuk memproses perutean paket dan sinkronisasi waktu
  mesh.update();
}