# Smart Trash Sorting System
### Integrasi Computer Vision (Teachable Machine) + JavaScript + Web Serial API + ESP32 & Servo Motor

Sistem pemilahan sampah otomatis berbasis kecerdasan buatan (Computer Vision) yang berjalan sepenuhnya di sisi peramban (client-side browser). Sistem mengklasifikasikan sampah menjadi dua kategori utama:
- **DRY (Sampah Kering)**: Kertas, kardus, botol plastik, daun kering.
- **WET (Sampah Basah)**: Sisa makanan, kulit buah, sayuran basah.

Setelah sampah terdeteksi secara stabil dengan tingkat keyakinan (confidence) $\ge 85\%$, sistem secara otomatis mengirimkan sinyal perintah melalui kabel USB via **Web Serial API** ke mikrokontroler **ESP32** untuk menggerakkan servo motor ke arah wadah yang tepat.

---

## 1. Arsitektur & Alur Kerja Sistem

```
+--------------------------------------------------------------------------+
|                            LAPTOP BROWSER                                |
|  [Webcam Laptop]                                                         |
|         │                                                                |
|         ▼                                                                |
|  [MediaDevices API / HTML5 Video Stream]                                 |
|         │                                                                |
|         ▼                                                                |
|  [Teachable Machine Image Model (TensorFlow.js)]                         |
|         │                                                                |
|         ▼                                                                |
|  [AI Logic Engine]                                                       |
|   ├─ Confidence >= 85%                                                   |
|   ├─ Frame Stability (6 frames berturut-turut)                           |
|   ├─ Cooldown Guard (4000ms - satu trigger per objek)                    |
|   └─ Dynamic Mapping: "sampah kering" -> DRY | "sampah basah" -> WET     |
|         │                                                                |
|         ▼                                                                |
|  [Web Serial API (115200 bps)]                                           |
+---------┼----------------------------------------------------------------+
          │ (Kabel Data USB - Perintah ASCII: DRY\n, WET\n, RESET\n)
          ▼
+--------------------------------------------------------------------------+
|                              HARDWARE                                    |
|  [ESP32 Dev Module]                                                      |
|   ├─ GPIO 18 (PWM Signal via ESP32Servo)                                 |
|   └─ Common Ground (GND)                                                 |
|         │                                                                |
|         ▼                                                                |
|  [Servo SG90 / MG90S] ◄── [Power Supply Eksternal 5V / 2A]               |
|   ├─ 30°  : Kiri (Wadah Sampah Kering / DRY)                             |
|   ├─ 90°  : Netral (Standby / RESET)                                     |
|   └─ 150° : Kanan (Wadah Sampah Basah / WET)                             |
+--------------------------------------------------------------------------+
```

---

## 2. Struktur Proyek

```
smart-trash-sorting/
├── index.html          # Antarmuka dashboard modern, HUD kamera & kontrol
├── style.css           # Desain visual responsif, dark futuristic glassmorphism
├── app.js              # Logika AI, Web Serial API, manajemen kamera & state
├── esp32/
│   └── esp32_servo.ino # Firmware Arduino C++ dengan library ESP32Servo
├── model/              # File model Teachable Machine lokal
│   ├── model.json
│   ├── metadata.json
│   └── weights.bin
├── libs/               # Dependensi TensorFlow.js & TM Image lokal (fallback offline)
│   ├── tf.min.js
│   └── teachablemachine-image.min.js
└── README.md           # Panduan instalasi, konfigurasi, wiring, dan troubleshooting
```

---

## 3. Konfigurasi Hardware & Skema Wiring

### 3.1 Daftar Komponen
1. **Mikrokontroler**: ESP32 DevKit V1 (30 atau 38 pin).
2. **Aktuator**: Motor Servo SG90 atau MG90S (Metal Gear).
3. **Power Supply**: Adaptor 5V / 2A atau Modul Step-Down / Baterai untuk suplai servo eksternal.
4. **Kabel**: Kabel USB data (menghubungkan ESP32 ke port USB laptop) & kabel jumper.

### 3.2 Tabel Koneksi Pin

| Komponen | Pin Servo | Pin ESP32 / Sumber Daya | Keterangan |
|---|---|---|---|
| **Signal Servo** | Kabel Oranye / Kuning | **GPIO 18** | Output PWM kontrol sudut |
| **VCC Servo** | Kabel Merah | **VCC External 5V (+)** | **WAJIB** daya eksternal (jangan dari pin 3.3V ESP32) |
| **GND Servo** | Kabel Cokelat / Hitam | **GND External (-)** | Kutub negatif catu daya |
| **Common GND** | — | **GND ESP32 ke GND External** | **WAJIB** disatukan agar level sinyal seimbang |
| **ESP32 Power**| Port Micro USB | **Port USB Laptop** | Daya ESP32 & komunikasi Serial |

> [!CAUTION]
> **PENTING TENTANG DAYA DAN PIN ESP32:**
> 1. **Dilarang memakai GPIO 34**: Pada ESP32, pin GPIO 34, 35, 36 (VP), dan 39 (VN) adalah pin *input-only* (tanpa sirkuit output PWM). Gunakan selalu **GPIO 18** seperti yang telah dikonfigurasi.
> 2. **Dilarang menyalakan Servo langsung dari pin 3.3V ESP32**: Servo SG90 saat bergerak dapat menarik arus sesaat hingga 500mA - 800mA. Mengambil daya dari pin 3.3V internal ESP32 akan memicu *brownout detector* yang menyebabkan ESP32 sering restart berulang kali. Gunakan suplai eksternal 5V dengan **GND bersama (Common GND)**.

### 3.3 Diagram Blok Pengkabelan
```
   [External 5V DC PSU]
       (+) ───► Kabel Merah Servo (VCC)
       (-) ───┬───► Kabel Cokelat Servo (GND)
              │
              └───► Pin GND ESP32 (Common Ground)

   [ESP32 Board]
       GPIO 18 ───► Kabel Oranye Servo (PWM Signal)
       Micro USB ──► Port USB Laptop (Web Serial API)
```

---

## 4. Logika Servo & Protokol Serial

ESP32 berkomunikasi dua arah melalui port USB UART pada baud rate **115200 bps**.

### Tabel Logika Sudut Servo:
| Perintah Serial | Sudut Servo | Arah Fisik | Fungsi | Durasi Tahan |
|---|---:|---|---|---|
| `DRY\n` | **30°** | Kiri | Mengarahkan sampah kering | Tahan 2.5 detik lalu kembali ke 90° |
| `WET\n` | **150°** | Kanan | Mengarahkan sampah basah | Tahan 2.5 detik lalu kembali ke 90° |
| `RESET\n`| **90°** | Tengah | Posisi netral / standby | Langsung ke 90° |

---

## 5. Panduan Instalasi & Menjalankan Firmware ESP32

1. Buka aplikasi **Arduino IDE** (versi 1.8.x atau 2.x).
2. Pasang board ESP32 di Arduino IDE:
   - Buka `File` > `Preferences`.
   - Masukkan URL package board di *Additional Boards Manager URLs*:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Buka `Tools` > `Board` > `Boards Manager`, cari **esp32** oleh Espressif Systems, lalu klik **Install**.
3. Pasang library **ESP32Servo**:
   - Buka `Sketch` > `Include Library` > `Manage Libraries...`.
   - Di kotak pencarian, ketik: `ESP32Servo`.
   - Pilih library **ESP32Servo by Kevin Harrington** dan klik **Install**.
4. Buka file firmware:
   - Buka file `esp32/esp32_servo.ino` (atau `sketch_nov21a/sketch_nov21a.ino`).
5. Pilih Board & Port:
   - `Tools` > `Board` > `ESP32 Arduino` > `ESP32 Dev Module`.
   - `Tools` > `Port` > Pilih port COM ESP32 Anda (misal: COM3, COM4, dsb).
   - `Tools` > `Upload Speed` > `115200` atau `921600`.
6. Klik tombol **Upload** (panah kanan).
7. Jika Arduino IDE menampilkan teks *Connecting........*, tekan dan tahan tombol **BOOT** pada board ESP32 selama 2 detik hingga proses flashing mulai berjalan.

---

## 6. Panduan Menjalankan Web Dashboard

Web Serial API dan MediaDevices (Kamera) merupakan fitur keamanan browser tingkat lanjut yang membutuhkan protokol yang aman (**`localhost`** atau **`https://`**). Jangan membuka file langsung dengan double click `file:///...`.

### Opsi A: Menggunakan VS Code Live Server (Direkomendasikan)
1. Buka folder proyek ini di **Visual Studio Code**.
2. Pasang ekstensi **Live Server** (oleh Ritwick Dey).
3. Klik kanan pada file `index.html`, lalu pilih **"Open with Live Server"**.
4. Browser Google Chrome akan terbuka secara otomatis di `http://127.0.0.1:5500`.

### Opsi B: Menggunakan Node.js `npx serve` atau `http-server`
Buka terminal / PowerShell di direktori proyek:
```bash
npx serve .
# atau
npx http-server -p 8080
```
Buka browser Google Chrome dan akses alamat `http://localhost:3000` atau `http://localhost:8080`.

### Opsi C: Menggunakan Python (Built-in)
Jika Anda memiliki Python:
```bash
python -m http.server 8000
```
Buka browser Google Chrome ke `http://localhost:8000`.

---

## 7. Langkah Pengujian Langkah Demi Langkah

1. **Hubungkan ESP32 ke Laptop**:
   - Tancapkan kabel USB ESP32 ke port laptop.
   - Pastikan power supply 5V untuk servo sudah menyala.
2. **Buka Web Dashboard di Google Chrome**:
   - Status indikator awal akan menunjukkan *ESP32: Terputus* dan *Model: Memuat...*.
   - Tunggu 1–2 detik sampai indikator model berubah hijau: *Model: Siap (2 Kelas)*.
3. **Hubungkan Web Serial**:
   - Klik tombol **"Hubungkan ESP32"**.
   - Jendela pop-up Chrome akan muncul meminta Anda memilih perangkat USB Serial.
   - Pilih perangkat (misal: *CP2102 USB to UART Bridge Controller* atau *USB-SERIAL CH340*) lalu klik **Connect**.
   - Servo akan bergerak ke posisi 90° (Netral) dan di Log Komunikasi muncul pesan konfirmasi dari ESP32.
4. **Uji Pergerakan Manual**:
   - Klik tombol **"Kiri (30° DRY)"** -> Servo mengarah ke 30° dan kembali otomatis ke 90°.
   - Klik tombol **"Kanan (150° WET)"** -> Servo mengarah ke 150° dan kembali otomatis ke 90°.
   - Klik tombol **"Netral (90°)"** -> Memastikan servo berada tepat di posisi tengah.
5. **Mulai Deteksi Otomatis**:
   - Klik tombol **"Mulai Deteksi"**.
   - Setujui dialog izin browser (*Allow Camera*).
   - Video webcam akan tampil di layar dengan kotak panduan (*Target Reticle*).
6. **Uji Pemilahan Sampah**:
   - Arahkan kertas/plastik (Sampah Kering) ke depan kamera.
   - Baris kestabilan akan mengisi frame berturut-turut ($1/6 \rightarrow 6/6$).
   - Begitu stabil di atas 85%, sistem membunyikan nada lonceng (*chime*), mengirimkan `DRY\n` ke ESP32, dan servo bergerak ke 30°.
   - Cooldown aktif selama 4 detik mengunci deteksi agar satu benda tidak memicu servo berkali-kali.
   - Setelah selesai, servo kembali ke 90° siap mendeteksi sampah berikutnya.

---

## 8. Panduan Troubleshooting Lengkap

### Masalah 1: Web Serial API Tidak Muncul atau Port Tidak Terdeteksi
- **Penyebab**: Menggunakan peramban yang tidak mendukung atau driver USB-to-UART belum terpasang di Windows.
- **Solusi**:
  1. Pastikan menggunakan **Google Chrome** atau **Microsoft Edge** versi desktop. Mozilla Firefox dan Safari tidak mendukung Web Serial API.
  2. Pastikan web dibuka melalui `http://localhost:...` atau `http://127.0.0.1:...`, bukan dibuka langsung melalui protocol `file:///`.
  3. Cek apakah driver USB Serial chip ESP32 Anda sudah terpasang di Windows Device Manager:
     - Jika board memakai chip Silicon Labs: Unduh dan pasang **CP210x Driver**.
     - Jika board memakai chip WCH: Unduh dan pasang **CH340/CH341 Driver**.
  4. Tutup **Serial Monitor** pada aplikasi Arduino IDE jika sedang terbuka, karena satu port COM serial tidak dapat diakses oleh dua aplikasi sekaligus.

### Masalah 2: Webcam Tidak Tampil / Layar Hitam
- **Penyebab**: Izin kamera diblokir oleh peramban atau webcam sedang digunakan oleh aplikasi lain (seperti Zoom, Teams, Camera app).
- **Solusi**:
  1. Klik ikon gembok / perizinan di sebelah kiri kolom URL Chrome, pastikan **Camera** diset ke **Allow**.
  2. Tutup seluruh aplikasi lain di laptop yang sedang mengakses webcam.
  3. Muat ulang halaman web (`Ctrl + F5`).

### Masalah 3: ESP32 Tiba-tiba Restart saat Servo Bergerak (Brownout Detector)
- **Penyebab**: Tegangan anjlok (*voltage drop*) akibat motor servo mengambil arus besar langsung dari pin suplai ESP32.
- **Solusi**:
  1. Jangan hubungkan kabel VCC merah servo ke pin 3V3 atau pin VIN ESP32!
  2. Gunakan adaptor atau baterai eksternal 5V dengan arus minimal 1.5A–2A khusus untuk motor servo.
  3. Pastikan kabel GND adaptor eksternal tetap terhubung ke pin GND ESP32 (*Common Ground*).
  4. Pasang kapasitor elektrolit $100\mu F$ - $470\mu F$ paralel pada jalur power VCC dan GND servo untuk meredam lonjakan arus sesaat.

### Masalah 4: Servo Bergetar (Jitter) atau Bergerak Tidak Sesuai Derajat
- **Penyebab**: Kebisingan sinyal PWM atau kabel jumper kendur.
- **Solusi**:
  1. Pastikan kabel oranye/kuning servo terpasang kokoh ke **GPIO 18**.
  2. Periksa kembali lebar pulsa PWM di firmware. Servo SG90 standar merespons pulsa antara $500\mu s$ (0°) sampai $2400\mu s$ (180°), yang sudah dikonfigurasi di `trashServo.attach(SERVO_PIN, 500, 2400);`.
