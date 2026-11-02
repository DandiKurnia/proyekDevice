// Sistem Kapasitas Kendaraan ESP32 + Firebase RTDB
// Sensor 1 = pintu masuk  (counter +1, servo 1 buka, buzzer bunyi, ditolak jika penuh)
// Sensor 2 = pintu keluar (counter -1, servo 2 buka)
// Lampu: hijau (<5), kuning (5-9), merah (10 = penuh)
// Firebase: counter disimpan di /kendaraan, dibaca balik via FirebaseStream

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>
#include "time.h"

#define WIFI_SSID     "A56"
#define WIFI_PASSWORD "12345678"

#define DATABASE_URL "https://parking-2357d-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define API_KEY      "AIzaSyApFod9L59CliC8C40aJAgI6C0xBdd-dKY"

#define TRIG1 25
#define ECHO1 34
#define TRIG2 26
#define ECHO2 35
#define SERVO1_PIN 18
#define SERVO2_PIN 19
#define BUZZER_PIN 27
#define LAMPU_MERAH 16
#define LAMPU_KUNING 17
#define LAMPU_HIJAU 4

const int KAPASITAS_MAX = 10;
const int BATAS_HIJAU = 5;             // counter < 5 -> hijau
const int JARAK_DETEKSI = 3;          // cm
const int SUDUT_TUTUP = 0;
const int SUDUT_BUKA = 90;
const unsigned long WAKTU_BUKA = 2000; // ms
const unsigned long WAKTU_LEPAS = 1000; // ms bebas dari deteksi sebelum dianggap pergi
const int NADA_BUZZER = 2000;          // Hz
const unsigned long LAMA_BUNYI = 300;  // ms

FirebaseData stream; // khusus stream (baca)
FirebaseData fbdo;   // khusus tulis
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseAktif = false;
volatile bool perluKirim = false; // counter lokal berubah, belum terkirim

Servo servo1;
Servo servo2;
ESP32PWM buzzer; // timer LEDC terpisah dari servo
bool buzzerBunyi = false;
unsigned long waktuBunyi = 0;

volatile int counter = 0; // diakses loop & task callback stream
bool terdeteksi1 = false;
bool terdeteksi2 = false;
unsigned long waktuTerlihat1 = 0;      // waktu terakhir sensor 1 melihat objek
unsigned long waktuTerlihat2 = 0;      // waktu terakhir sensor 2 melihat objek
bool servo1Buka = false;
bool servo2Buka = false;
unsigned long waktuBuka1 = 0;
unsigned long waktuBuka2 = 0;

float bacaJarak(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long durasi = pulseIn(echoPin, HIGH, 30000);
  if (durasi == 0) return 999;
  return durasi * 0.0343 / 2;
}

void updateLampu() {
  digitalWrite(LAMPU_MERAH, LOW);
  digitalWrite(LAMPU_KUNING, LOW);
  digitalWrite(LAMPU_HIJAU, LOW);

  if (counter >= KAPASITAS_MAX) {
    digitalWrite(LAMPU_MERAH, HIGH);   // MERAH: penuh
  } else if (counter < BATAS_HIJAU) {
    digitalWrite(LAMPU_HIJAU, HIGH);   // HIJAU: masih banyak slot
  } else {
    digitalWrite(LAMPU_KUNING, HIGH);  // KUNING: hampir penuh
  }
}

// Data dari Firebase (snapshot awal / diubah dari web/app) -> counter lokal
void streamCallback(FirebaseStream data) {
  if (perluKirim) return; // perubahan sensor lokal belum terkirim, jangan ditimpa

  int nilai = -1;
  if (data.dataType() == "json") { // snapshot awal "/" : {"kendaraan": 7}
    FirebaseJsonData jsonData;
    if (data.jsonObject().get(jsonData, "kendaraan")) nilai = jsonData.intValue;
  } else if (data.dataPath() == "/kendaraan") { // perubahan tunggal
    nilai = data.intData();
  }

  if (nilai < 0) return;
  counter = min(nilai, KAPASITAS_MAX);
  updateLampu();
  Serial.printf("[Firebase] kendaraan = %d\n", counter);
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("[Firebase] Keep-Alive Ping OK.");
}

// Sinkronisasi waktu via NTP — wajib sebelum Firebase agar sertifikat TLS valid
void syncNTP() {
  Serial.print("[NTP] Sinkronisasi waktu...");
  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com"); // WIB
  struct tm timeinfo;
  Serial.println(getLocalTime(&timeinfo, 8000) ? " OK" : " TIMEOUT");
}

void connectWiFi() {
  Serial.print("[WiFi] Menghubungkan");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true); // reconnect otomatis kalau sinyal drop
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    Serial.print(".");
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Gagal. Sistem jalan offline tanpa Firebase.");
  }
}

void initFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  config.timeout.socketConnection    = 15000;
  config.timeout.serverResponse      = 30000;
  config.timeout.rtdbKeepAlive       = 45000;
  config.timeout.rtdbStreamReconnect =  1000;
  config.timeout.rtdbStreamError     =  3000;

  // Login anonymous pakai API key (Authentication > Anonymous harus aktif)
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("[Firebase] SignUp anonymous OK");
  } else {
    Serial.printf("[Firebase] SignUp gagal: %s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  stream.setBSSLBufferSize(4096, 1024);
  fbdo.setBSSLBufferSize(4096, 1024);
  stream.keepAlive(5, 5, 1);

  unsigned long t = millis();
  while (!Firebase.ready() && millis() - t < 5000) delay(100);

  // beginStream DULU, setStreamCallback SETELAH (urutan terbalik bikin SSL bentrok)
  if (Firebase.RTDB.beginStream(&stream, "/")) {
    Serial.println("[Firebase] Stream aktif");
  } else {
    Serial.printf("[Firebase] Stream gagal: %s\n", stream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);
  firebaseAktif = true;
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);
  pinMode(LAMPU_MERAH, OUTPUT);
  pinMode(LAMPU_KUNING, OUTPUT);
  pinMode(LAMPU_HIJAU, OUTPUT);

  // Jatah timer LEDC dikelola ESP32Servo, servo (50Hz) & buzzer (2kHz) tidak berbagi timer
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  servo1.setPeriodHertz(50);
  servo2.setPeriodHertz(50);
  servo1.attach(SERVO1_PIN, 500, 2400);
  servo2.attach(SERVO2_PIN, 500, 2400);
  servo1.write(SUDUT_TUTUP);
  servo2.write(SUDUT_TUTUP);

  buzzer.attachPin(BUZZER_PIN, NADA_BUZZER, 10);
  buzzer.write(0); // diam

  updateLampu();

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    syncNTP();
    initFirebase(); // snapshot awal stream mengisi counter dari /kendaraan
  }

  Serial.println("Sistem siap. Kapasitas maks: 10 kendaraan");
}

void loop() {
  // Sensor dianggap hadir selama jarak < JARAK_DETEKSI, dan baru dianggap
  // pergi setelah bebas deteksi terus-menerus WAKTU_LEPAS.
  // Tujuannya: pulseIn yang sesekali timeout (jarak 999) tidak dihitung
  // sebagai "kendaraan sudah lewat", sehingga counter tidak turun berulang.
  float jarak1 = bacaJarak(TRIG1, ECHO1);
  bool muncul1 = (jarak1 < JARAK_DETEKSI);
  if (muncul1) waktuTerlihat1 = millis();
  bool sebelumnya1 = terdeteksi1;
  terdeteksi1 = muncul1 || (sebelumnya1 && millis() - waktuTerlihat1 < WAKTU_LEPAS);

  delay(60); // jeda antar sensor supaya echo sensor 1 tidak tertangkap sensor 2

  float jarak2 = bacaJarak(TRIG2, ECHO2);
  bool muncul2 = (jarak2 < JARAK_DETEKSI);
  if (muncul2) waktuTerlihat2 = millis();
  bool sebelumnya2 = terdeteksi2;
  terdeteksi2 = muncul2 || (sebelumnya2 && millis() - waktuTerlihat2 < WAKTU_LEPAS);

  // ===== PINTU MASUK =====
  if (terdeteksi1 && !sebelumnya1) {
    if (counter < KAPASITAS_MAX) {
      counter++;
      perluKirim = true;
      servo1.write(SUDUT_BUKA);
      servo1Buka = true;
      waktuBuka1 = millis();
      buzzer.writeTone(NADA_BUZZER);
      buzzerBunyi = true;
      waktuBunyi = millis();
      Serial.print("MASUK  | Gerbang masuk BUKA | Counter: ");
      Serial.println(counter);
      updateLampu();
    } else {
      Serial.println("PENUH  | Gerbang masuk TIDAK dibuka | Counter: 10");
    }
  }

  // ===== PINTU KELUAR =====
  if (terdeteksi2 && !sebelumnya2) {
    if (counter > 0) {
      counter--;
      perluKirim = true;
      servo2.write(SUDUT_BUKA);
      servo2Buka = true;
      waktuBuka2 = millis();
      Serial.print("KELUAR | Gerbang keluar BUKA | Counter: ");
      Serial.println(counter);
      updateLampu();
    } else {
      Serial.println("KOSONG | Tidak ada kendaraan di dalam");
    }
  }

  // ===== TUTUP SERVO OTOMATIS =====
  if (servo1Buka && millis() - waktuBuka1 >= WAKTU_BUKA && !terdeteksi1) {
    servo1.write(SUDUT_TUTUP);
    servo1Buka = false;
    Serial.println("Gerbang masuk TUTUP");
  }
  if (servo2Buka && millis() - waktuBuka2 >= WAKTU_BUKA && !terdeteksi2) {
    servo2.write(SUDUT_TUTUP);
    servo2Buka = false;
    Serial.println("Gerbang keluar TUTUP");
  }

  // ===== MATIKAN BUZZER =====
  if (buzzerBunyi && millis() - waktuBunyi >= LAMA_BUNYI) {
    buzzer.write(0);
    buzzerBunyi = false;
  }

  // ===== KIRIM KE FIREBASE =====
  // Dikirim setelah buzzer mati: setInt blocking ~ratusan ms, jangan tahan bunyi.
  // Gagal kirim -> perluKirim tetap true -> dicoba lagi loop berikutnya.
  if (firebaseAktif && perluKirim && !buzzerBunyi && Firebase.ready()) {
    if (Firebase.RTDB.setInt(&fbdo, "/kendaraan", counter)) {
      perluKirim = false;
      Serial.printf("[Firebase] Kirim kendaraan = %d OK\n", counter);
    } else {
      Serial.printf("[Firebase] Kirim gagal: %s\n", fbdo.errorReason().c_str());
    }
  }

  delay(50);
}
