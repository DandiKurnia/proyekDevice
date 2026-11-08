# Sistem Kapasitas Parkir ESP32 + Firebase

Penghitung kendaraan parkir (kapasitas 10). Sensor ultrasonik di pintu masuk dan keluar menambah/mengurangi counter, membuka gerbang servo, menyalakan lampu status, dan menyinkronkan jumlah kendaraan ke Firebase Realtime Database. Dashboard web (`index.html`) menampilkan data secara realtime.

## Daftar Pin

| Komponen | Pin komponen | GPIO ESP32 | Mode | Fungsi |
|---|---|---|---|---|
| Ultrasonik 1 (masuk) | TRIG | 25 | OUTPUT | Pemicu pulsa |
| Ultrasonik 1 (masuk) | ECHO | 34 | INPUT | Pantulan (input-only pin) |
| Ultrasonik 2 (keluar) | TRIG | 26 | OUTPUT | Pemicu pulsa |
| Ultrasonik 2 (keluar) | ECHO | 35 | INPUT | Pantulan (input-only pin) |
| Servo 1 (gerbang masuk) | Signal | 18 | PWM 50 Hz | Buka 90° / tutup 0° |
| Servo 2 (gerbang keluar) | Signal | 19 | PWM 50 Hz | Buka 180° / tutup 90° |
| Buzzer pasif | + | 27 | PWM 2 kHz | Bunyi 300 ms saat mobil masuk |
| LED merah | Anoda | 16 | OUTPUT | Parkir penuh (10) |
| LED kuning | Anoda | 17 | OUTPUT | Hampir penuh (5–9) |
| LED hijau | Anoda | 4 | OUTPUT | Tersedia (< 5) |

Semua GND komponen disatukan dengan GND ESP32.

## Catatan Rangkaian

- **ECHO HC-SR04 keluar 5V**, sedangkan GPIO ESP32 maksimal 3.3V. Pasang pembagi tegangan di ECHO (mis. 1 kΩ seri + 2 kΩ ke GND) sebelum masuk GPIO 34/35.
- **Servo pakai catu 5V terpisah** (bukan pin 5V/3V3 ESP32). GND catu servo wajib disambung ke GND ESP32. Lepas daya servo saat upload kalau upload gagal (`No serial data received`).
- **LED** masing-masing pakai resistor seri 220–330 Ω ke GND.
- **Buzzer pasif** butuh sinyal PWM; buzzer aktif tidak akan menghasilkan nada yang sama.
- GPIO 34/35 hanya input dan tidak punya pull-up internal — cukup untuk ECHO.

## Cara Kerja

| Kejadian | Aksi |
|---|---|
| Sensor 1 mendeteksi objek < 3 cm, counter < 10 | Counter +1, servo 1 buka, buzzer bunyi |
| Sensor 1 mendeteksi, counter = 10 | Gerbang masuk tetap tertutup |
| Sensor 2 mendeteksi objek < 3 cm, counter > 0 | Counter −1, servo 2 buka |
| Gerbang terbuka ≥ 2 detik dan sensor sudah bebas | Servo kembali menutup |
| Counter berubah | Lampu diperbarui, nilai dikirim async ke `/kendaraan` di Firebase |
| ESP32 boot | Membaca nilai terakhir `/kendaraan` agar counter sinkron setelah restart |

Objek dianggap sudah lewat setelah sensor bebas deteksi 1 detik, supaya pembacaan yang sesekali gagal tidak menghitung ganda.

## Library Arduino

- `ESP32Servo` (Kevin Harrington / madhephaestus) — servo dan PWM buzzer
- `Firebase Arduino Client Library for ESP8266 and ESP32` (Mobizt) — `Firebase_ESP_Client.h`
- Board: **ESP32 Dev Module** (Arduino-ESP32 core)

## Konfigurasi

Ubah konstanta di awal `program.ino`:

| Konstanta | Isi |
|---|---|
| `WIFI_SSID`, `WIFI_PASSWORD` | Hotspot/WiFi yang dipakai |
| `DATABASE_URL`, `API_KEY` | Dari Firebase Console → Project settings |
| `KAPASITAS_MAX` | Kapasitas parkir (default 10) |
| `JARAK_DETEKSI` | Jarak deteksi sensor dalam cm (default 3) |
| `WAKTU_BUKA` | Lama gerbang terbuka minimal, ms (default 2000) |

## Dashboard Web

`index.html` membaca `/kendaraan` secara realtime. Buka lewat server lokal (bukan double-click file):

```
python -m http.server 8000
```

lalu akses `http://localhost:8000/index.html`.
