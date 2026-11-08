# Backup program.ino

Versi offline (tanpa Firebase). Logika sensor/servo/buzzer/lampu berjalan lokal penuh.

```cpp
// Sistem Kapasitas Kendaraan ESP32
// Sensor 1 = pintu masuk  (counter +1, servo 1 buka, buzzer bunyi, ditolak jika penuh)
// Sensor 2 = pintu keluar (counter -1, servo 2 buka)
// Lampu: hijau (<5), kuning (5-9), merah (10 = penuh)

#include <ESP32Servo.h>

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

Servo servo1;
Servo servo2;
ESP32PWM buzzer; // timer LEDC terpisah dari servo
bool buzzerBunyi = false;
unsigned long waktuBunyi = 0;

int counter = 0;
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

  delay(50);
}
```
