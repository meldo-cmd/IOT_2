#include <ESP8266WiFi.h>

void setup() {
  Serial.begin(115200);
  Serial.println();
  
  // Mengaktifkan mode Station agar MAC address antarmuka STA aktif
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  Serial.print("ESP8266 MAC Address: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  // Tidak ada proses berulang
}