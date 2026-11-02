# Dokumentasi Pin Sensor & Penjelasan Dashboard (`index.html`)

Dokumen ini melengkapi `program.ino` dan `index.html` untuk proyek sistem monitoring kapasitas parkir berbasis ESP32 (Arduino core) dan Firebase Realtime Database.

---

## 1. Pin ESP32 / Arduino

Modul yang digunakan adalah **ESP32 Dev Module** (30/38 pin).

### Tabel Pin Lengkap

| Komponen | Pin Fisik Modul | GPIO ESP32 | Sifat GPIO | Mode Code | Fungsi dalam Sistem |
|---|---|---|---|---|---|
| **Ultrasonik 1 (Masuk)** | TRIG | **GPIO 25** | Input/Output | `OUTPUT` | Mengirim pulsa trigger 10 µs |
| **Ultrasonik 1 (Masuk)** | ECHO | **GPIO 34** | **Input Only** (tanpa pull-up) | `INPUT` | Menerima pantulan pulsa via `pulseIn()` |
| **Ultrasonik 2 (Keluar)** | TRIG | **GPIO 26** | Input/Output | `OUTPUT` | Mengirim pulsa trigger 10 µs |
| **Ultrasonik 2 (Keluar)** | ECHO | **GPIO 35** | **Input Only** (tanpa pull-up) | `INPUT` | Menerima pantulan pulsa via `pulseIn()` |
| **Servo 1 (Palang Masuk)** | Signal (Kuning/Oranye) | **GPIO 18** | Output (LEDC) | PWM 50 Hz | Buka 90° / Tutup 0° gerbang masuk |
| **Servo 2 (Palang Keluar)** | Signal (Kuning/Oranye) | **GPIO 19** | Output (LEDC) | PWM 50 Hz | Buka 90° / Tutup 0° gerbang keluar |
| **Buzzer Pasif** | Positif (+) | **GPIO 27** | Output (LEDC) | PWM 2 kHz | Bunyi 300 ms nada 2000 Hz saat mobil masuk |
| **LED Merah** | Anoda (+) | **GPIO 16** | Output | `OUTPUT` | Menyala saat kapasitas **penuh** (10 kendaraan) |
| **LED Kuning** | Anoda (+) | **GPIO 17** | Output | `OUTPUT` | Menyala saat **hampir penuh** (5–9 kendaraan) |
| **LED Hijau** | Anoda (+) | **GPIO 4** | Output | `OUTPUT` | Menyala saat **slot tersedia** (< 5 kendaraan) |

### Power & Ground

- **VCC Ultrasonik (HC-SR04):** pin `VIN` / 5V ESP32 (HC-SR04 standar butuh 5V; HC-SR04P/RCWL-0516 bisa 3.3V).
- **VCC Servo (SG90 / MG90S):** gunakan adaptor/catu 5V eksternal. Jangan ambil dari pin 3V3 atau VIN ESP32 karena lonjakan arus servo (~500 mA–1 A per servo) memicu brownout/restart ESP32.
- **GND:** semua ground (ESP32, catu eksternal servo, buzzer, LED, kedua ultrasonik) wajib terhubung ke satu jalur bersama (**common ground**).

### Peringatan Hardware Khusus ESP32

1. **Tegangan ECHO HC-SR04 = 5V, toleransi GPIO ESP32 = 3.3V.**
   Pin GPIO 34 dan 35 bukan 5V-tolerant. Pasang rangkaian pembagi tegangan (voltage divider) di jalur ECHO sebelum masuk ke pin ESP32:
   ```
   ECHO HC-SR04 (5V) ───[ 1 kΩ ]───┬─── GPIO 34 / 35 (ESP32 ~3.3V)
                                   │
                                [ 2 kΩ ]
                                   │
                                  GND
   ```
2. **GPIO 34 & 35 adalah GPI (Input Only).**
   Pin ini tidak memiliki sirkuit output dan tidak memiliki resistor pull-up/pull-down internal. Sangat tepat untuk membaca ECHO ultrasonik yang aktif push-pull.
3. **Timer LEDC Buzzer vs Servo:**
   Di `program.ino`, timer LEDC dialokasikan lewat library `ESP32Servo` (`ESP32PWM::allocateTimer(0..3)`). Servo berjalan di frekuensi 50 Hz, sedangkan buzzer pasif memakai objek `ESP32PWM` pada pin 27 dengan nada 2000 Hz agar tidak berebut timer hardware dengan servo.

---

## 2. Penjelasan Dashboard (`index.html`)

`index.html` adalah web dashboard single-page yang memantau slot parkir secara realtime tanpa reload halaman. File ini langsung berkomunikasi dengan Firebase Realtime Database.

### Arsitektur Aliran Data

```
[ Mobil Masuk/Keluar ]
         │
         ▼
[ Sensor HC-SR04 ] ──> [ ESP32 program.ino ]
                             │
                      WiFi / HTTPS REST
                             ▼
                [ Firebase Realtime Database ]
                   path: /kendaraan (integer)
                             │
                      Websocket Stream
                             ▼
                    [ index.html (Web) ]
                   onValue() listener aktif
```

Dashboard juga mendukung dua arah secara logis: jika nilai `/kendaraan` diubah langsung di Firebase Console atau via REST API, ESP32 menerima nilainya lewat `FirebaseStream` dan langsung memperbarui posisi lampu LED fisiknya.

---

### Bagian-Bagian Kode `index.html`

#### A. Head & Library
- **Tailwind CSS Play CDN** (`<script src="https://cdn.tailwindcss.com"></script>`): menyediakan utility styling responsive tanpa langkah kompilasi.
- **Firebase JavaScript SDK v13.1.0 (ES Module CDN):**
  - `firebase-app.js` (`initializeApp`): menginisialisasi koneksi dengan project credentials.
  - `firebase-database.js` (`getDatabase`, `ref`, `onValue`): membuka koneksi socket realtime ke database path.

#### B. Komponen Antarmuka (UI)

1. **Header Card:**
   - Judul: "Parkir Kendaraan".
   - Badge Koneksi (`#koneksi`): memantau path internal Firebase `/.info/connected`. Menampilkan:
     - Hijau: `Online` (terhubung ke server Firebase).
     - Abu-abu: `Offline` (koneksi putus / sedang menyambung).
     - Merah: `Error` (kredensial / kuota ditolak).

2. **Panel Status Berwarna (`#panel`):**
   Panel ini mengadopsi prinsip *single focal region* — seluruh area berubah warna sesuai status parkir, dengan kontras teks yang lolos uji WCAG AA (rasio kontras ≥ 4.5:1):
   - **Hijau (`data-state="hijau"`):** latar hijau tua (`#15803d`), teks putih. Muncul jika kendaraan `< 5`.
   - **Kuning (`data-state="kuning"`):** latar kuning amber (`#facc15`), teks hitam gelap (`#0f172a`). Muncul jika kendaraan `5 – 9`.
   - **Merah (`data-state="merah"`):** latar merah tegas (`#b91c1c`), teks putih. Muncul jika kendaraan `≥ 10` (**PARKIR PENUH**).

3. **Indikator Angka & Progress Bar:**
   - `#counter`: angka besar (font 7xl) menampilkan jumlah kendaraan saat ini.
   - Angka statis `/10`: kapasitas maksimal parkir.
   - `#sisa`: slot kosong (`10 - counter`).
   - `#bar`: bar progress visual yang lebarnya dihitung dari persentase `(counter / 10) * 100%`. Memiliki atribut ARIA `role="progressbar"` untuk aksesibilitas pembaca layar.

4. **Legenda 3 Lampu Fisik:**
   Merefleksikan langsung kondisi 3 LED di board ESP32:
   - Tiga lingkaran: **Tersedia (Hijau)**, **Hampir penuh (Kuning)**, **Penuh (Merah)**.
   - **State aktif (`data-aktif="true"`):** lingkaran membesar (`transform: scale(1.14)`), warna cerah, label tebal hitam.
   - **State non-aktif (`data-aktif="false"`):** lingkaran mengecil (`transform: scale(0.88)`), desaturasi grayscale penuh, teks pudar abu-abu. Cues ganda ini memastikan pengguna yang buta warna tetap dapat membaca status dari bentuk dan ketebalan label, bukan warna semata.

---

### Logika JavaScript di `index.html`

```javascript
// Ambang batas disamakan persis dengan firmware ESP32
const KAPASITAS_MAX = 10;
const BATAS_HIJAU = 5;

// Listener realtime ke path /kendaraan
onValue(ref(db, "kendaraan"), snap => {
  // Clamping 0..10 untuk mencegah data invalid dari database
  const n = Math.min(Math.max(Number(snap.val()) || 0, 0), KAPASITAS_MAX);
  const penuh = n >= KAPASITAS_MAX;
  const hijau = n < BATAS_HIJAU;

  // 1. Update angka counter & sisa slot
  $("counter").textContent = n;
  $("sisa").textContent = KAPASITAS_MAX - n;

  // 2. Update panjang progress bar
  $("bar").style.width = `${(n / KAPASITAS_MAX) * 100}%`;
  $("bar").parentElement.setAttribute("aria-valuenow", n);

  // 3. Switch tema warna panel utama
  $("panel").dataset.state = penuh ? "merah" : hijau ? "hijau" : "kuning";
  $("status").textContent = penuh ? "PARKIR PENUH" : hijau ? "Slot tersedia" : "Hampir penuh";
  // 4. Update status aktif 3 lampu legenda
  $("lampu-hijau").parentElement.dataset.aktif = hijau;
  $("lampu-kuning").parentElement.dataset.aktif = !penuh && !hijau;
  $("lampu-merah").parentElement.dataset.aktif = penuh;
});
```

---

## 3. Matriks Status Sistem (Firmware & Web)

| Jumlah Mobil (`counter`) | Status Teks | Warna Panel Web | LED Hijau (GPIO 4) | LED Kuning (GPIO 17) | LED Merah (GPIO 16) | Respon Pintu Masuk |
|---|---|---|---|---|---|---|
| **0 – 4** | Slot tersedia | Hijau | **MENYALA** | MATI | MATI | Buka normal + Buzzer |
| **5 – 9** | Hampir penuh | Kuning | MATI | **MENYALA** | MATI | Buka normal + Buzzer |
| **10** | PARKIR PENUH | Merah | MATI | MATI | **MENYALA** | **Ditolak** (servo tetap tutup, buzzer diam) |

---

## 4. Cara Menjalankan Dashboard

Karena `index.html` menggunakan JavaScript ES Module (`import ... from "https://..."`), browser memblokir akses jika file dibuka lewat protokol `file:///`. Halaman wajib disajikan lewat server HTTP:

### Cara 1: Menggunakan Python (paling praktis)
Jalankan di folder proyek ini:
```bash
python -m http.server 8000
```
Buka peramban: `http://localhost:8000/index.html`

### Cara 2: Live Server (VS Code Extension)
Klik kanan `index.html` → **Open with Live Server**.

### Cara 3: Node.js `serve` / `http-server`
```bash
npx serve .
```
