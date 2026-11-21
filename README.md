# 🗑️ Panduan Menjalankan Proyek: Smart Trash Sorting System
> **Sistem Pemilahan Sampah Otomatis Berbasis Web AI (Teachable Machine) + ESP32 & Servo Motor**

Dokumen ini berisi panduan langkah-demi-langkah yang lengkap dan praktis untuk merakit perangkat keras, mengunggah kode ke ESP32, serta menjalankan dashboard web di laptop Anda.

---

## 📋 Daftar Isi
1. [Prasyarat Sistem](#1-prasyarat-sistem)
2. [Skema Wiring Hardware](#2-skema-wiring-hardware)
3. [Langkah 1: Upload Firmware ke ESP32](#langkah-1-upload-firmware-ke-esp32)
4. [Langkah 2: Menjalankan Dashboard Web](#langkah-2-menjalankan-dashboard-web)
5. [Langkah 3: Pengujian & Pemilahan Sampah](#langkah-3-pengujian--pemilahan-sampah)
6. [Struktur File Proyek](#struktur-file-proyek)
7. [Panduan Troubleshooting](#panduan-troubleshooting)

---

## 1. Prasyarat Sistem

### Perangkat Keras:
- **Board ESP32** (ESP32 DevKit V1 / NodeMCU-32S).
- **Servo Motor SG90** (atau MG90S).
- **Power Supply Eksternal 5V** (Adaptor DC 5V / 2A atau modul baterai) untuk menyuplai servo.
- **Kabel USB Data** (untuk menghubungkan ESP32 ke port USB laptop).
- **Kabel Jumper** (Male-to-Female / Male-to-Male).
- **Webcam Laptop** yang berfungsi baik.

### Perangkat Lunak:
- **Google Chrome** atau **Microsoft Edge** versi desktop terbaru (Web Serial API & MediaDevices hanya didukung penuh pada browser berbasis Chromium).
- **Arduino IDE** (versi 1.8.x atau 2.x).
- **Driver USB-to-UART ESP32** (CP210x atau CH340) terpasang di Windows.

---

## 2. Skema Wiring Hardware

Hubungkan komponen sesuai tabel dan diagram berikut:

| Pin Komponen | Warna Kabel | Terhubung Ke | Catatan Penting |
|---|---|---|---|
| **Servo Signal (PWM)** | Oranye / Kuning | **GPIO 18** (ESP32) | Pin kontrol sudut servo |
| **Servo VCC (+)** | Merah | **5V Power Supply Eksternal** | ⚠️ **Dilarang** mengambil dari pin 3.3V ESP32 |
| **Servo GND (-)** | Cokelat / Hitam | **GND Power Supply Eksternal** | Kutub negatif catu daya |
| **Common Ground** | Kabel Jumper | **GND ESP32 $\longleftrightarrow$ GND Eksternal** | ⚠️ **WAJIB DISATUKAN** untuk referensi ground |
| **ESP32 USB** | Kabel USB Data | **Port USB Laptop** | Daya ESP32 & jalur Web Serial API |

### 📌 Diagram Blok Sirkuit:
```
   [Power Supply Eksternal 5V]
       (+) 5V  ────────────────────► Kabel MERAH Servo (VCC)
       (-) GND ──────────┬─────────► Kabel COKELAT Servo (GND)
                         │
                         └─────────► Pin GND ESP32 (Common Ground)

   [ESP32 Dev Board]
       Pin GPIO 18 ────────────────► Kabel ORANYE Servo (PWM Signal)
       Port USB    ────────────────► Port USB Laptop (Web Serial API)
```

> [!CAUTION]
> - **Jangan gunakan GPIO 34**: Pin GPIO 34, 35, 36, dan 39 pada ESP32 adalah *input-only* dan tidak dapat mengeluarkan sinyal PWM ke servo.
> - **Selalu satukan GND**: Tanpa sambungan *Common Ground*, servo akan bergetar tak terkendali (*jitter*) karena hilangnya referensi sinyal logika.

---

## Langkah 1: Upload Firmware ke ESP32

1. **Buka Arduino IDE** di laptop Anda.
2. **Pasang Library `ESP32Servo`**:
   - Di Arduino IDE, buka menu: **Sketch** $\rightarrow$ **Include Library** $\rightarrow$ **Manage Libraries...**
   - Di kolom pencarian, ketik: `ESP32Servo`
   - Cari library **ESP32Servo by Kevin Harrington**, lalu klik **Install**.
3. **Buka File Firmware**:
   - Buka file [esp32/esp32_servo.ino](file:///d:/project-internship/wastebin_selection/esp32/esp32_servo.ino) (atau file [sketch_nov21a/sketch_nov21a.ino](file:///d:/project-internship/wastebin_selection/sketch_nov21a/sketch_nov21a.ino)).
4. **Pilih Board & Port**:
   - Menu **Tools** $\rightarrow$ **Board** $\rightarrow$ **ESP32 Arduino** $\rightarrow$ pilih **ESP32 Dev Module**.
   - Menu **Tools** $\rightarrow$ **Port** $\rightarrow$ pilih port COM ESP32 Anda (contoh: `COM3`, `COM4`, dsb).
   - Menu **Tools** $\rightarrow$ **Upload Speed** $\rightarrow$ pilih `115200` atau `921600`.
5. **Upload Program**:
   - Klik tombol **Upload** (ikon panah ke kanan `➔`).
   - *Tips*: Jika muncul teks `Connecting........_____.....` di konsol Arduino IDE, tekan dan tahan tombol **BOOT** pada board ESP32 selama 2 detik sampai proses flashing dimulai.
6. **Tutup Serial Monitor Arduino IDE**:
   - ⚠️ **PENTING**: Pastikan jendela *Serial Monitor* di Arduino IDE ditutup sebelum membuka dashboard web, agar port COM tidak terkunci.

---

## Langkah 2: Menjalankan Dashboard Web

> [!IMPORTANT]
> Karena peramban Google Chrome menerapkan standar keamanan tinggi untuk fitur **Web Serial API** dan **Webcam**, dashboard **harus dibuka melalui server lokal (`http://localhost` / `http://127.0.0.1`)**, bukan dengan klik ganda berkas `file:///`.

Pilih salah satu cara tercepat berikut:

### 🌟 Cara A: 1-Klik Launcher (Paling Praktis)
Cukup klik ganda (double-click) file:
👉 **[start_dashboard.bat](file:///d:/project-internship/wastebin_selection/start_dashboard.bat)**

Script ini otomatis mendeteksi server lokal di komputer Anda dan langsung membuka dashboard di browser default.

---

### 🌐 Cara B: Menggunakan VS Code Live Server
1. Buka folder proyek ini di **Visual Studio Code**.
2. Pasang ekstensi **Live Server** (oleh Ritwick Dey) jika belum terpasang.
3. Klik kanan pada file [index.html](file:///d:/project-internship/wastebin_selection/index.html), lalu pilih **"Open with Live Server"**.
4. Browser akan otomatis terbuka di `http://127.0.0.1:5500/`.

---

### 🐍 Cara C: Menggunakan Python (Command Prompt / PowerShell)
Buka terminal di folder proyek ini:
```powershell
python -m http.server 8000
```
Buka Google Chrome dan ketik alamat:
`http://localhost:8000`

---

### 📦 Cara D: Menggunakan Node.js / NPX
Buka terminal di folder proyek ini:
```powershell
npx serve -l 3000 .
```
Buka Google Chrome dan ketik alamat:
`http://localhost:3000`

---

## Langkah 3: Pengujian & Pemilahan Sampah

Setelah antarmuka terbuka di Google Chrome:

1. **Pastikan Model AI Siap**:
   - Periksa indikator status di bilah atas: badge harus berwarna hijau bertuliskan **`Model: Siap (2 Kelas)`**.
   - Model yang digunakan adalah model Teachable Machine Anda: `https://teachablemachine.withgoogle.com/models/YVOo01Icl/` dengan label:
     - `sampah kering` $\rightarrow$ Sampah Kering (DRY)
     - `sampah basah` $\rightarrow$ Sampah Basah (WET)

2. **Hubungkan ESP32**:
   - Pastikan power supply 5V eksternal servo telah menyala.
   - Klik tombol hijau **"Hubungkan ESP32"**.
   - Sebuah jendela pop-up Chrome akan muncul menampilkan daftar port USB. Pilih board ESP32 Anda, lalu klik **Connect**.
   - Servo akan otomatis bergerak ke posisi netral **90°**, dan terminal log di layar web akan menampilkan respon konfirmasi dari ESP32.

3. **Uji Kontrol Manual**:
   - Klik tombol **"Kiri (30° DRY)"** $\rightarrow$ Servo bergerak ke 30° lalu kembali ke 90°.
   - Klik tombol **"Kanan (150° WET)"** $\rightarrow$ Servo bergerak ke 150° lalu kembali ke 90°.
   - Klik tombol **"Netral (90°)"** $\rightarrow$ Servo kembali ke posisi awal 90°.

4. **Aktifkan Kamera & Deteksi Otomatis**:
   - Klik tombol biru **"Mulai Deteksi"**.
   - Klik **Allow** pada dialog izin akses webcam.
   - Posisikan sampel sampah (contoh: kertas/plastik atau sisa makanan/buah) di dalam kotak pemandu (*Target Reticle*).

5. **Mekanisme Otomatis**:
   - Sistem akan mengevaluasi tingkat keyakinan (confidence).
   - Indikator kestabilan frame akan bertambah ($1/6 \rightarrow 6/6$).
   - Begitu confidence mencapai minimal **85%** dan stabil selama 6 frame:
     1. Suara lonceng (*chime*) berbunyi.
     2. Perintah serial dikirimkan (`DRY\n` atau `WET\n`).
     3. Servo berputar ke sudut yang sesuai (**30° Kiri** untuk kering, **150° Kanan** untuk basah).
     4. Servo menahan sampah selama 2.5 detik lalu kembali ke posisi netral 90°.
     5. Cooldown aktif selama 4 detik mengunci deteksi berulang untuk objek yang sama.
     6. Data pemilahan tercatat rapi di tabel riwayat dan statistik bertambah.

---

## Struktur File Proyek

```
wastebin_selection/
├── start_dashboard.bat          # Script 1-klik untuk menjalankan server & browser
├── index.html                   # Halaman dashboard utama
├── style.css                    # Tampilan modern dark futuristic glassmorphism
├── app.js                       # Logika AI, Web Serial API, dan kamera
├── README.md                    # Dokumentasi lengkap proyek
│
├── esp32/
│   └── esp32_servo.ino          # Firmware Arduino C++ untuk ESP32
├── sketch_nov21a/
│   └── sketch_nov21a.ino        # Salinan firmware siap flash di Arduino IDE
│
├── model/                       # File model Teachable Machine offline/lokal
│   ├── model.json
│   ├── metadata.json
│   └── weights.bin
│
├── libs/                        # Library TensorFlow.js & TM Image lokal (fallback offline)
│   ├── tf.min.js
│   └── teachablemachine-image.min.js
│
└── smart-trash-sorting/         # Folder bundle terisolasi siap distribusi
```

---

## Panduan Troubleshooting

### ❓ Web Serial API: Tombol "Hubungkan ESP32" Tidak Merespon atau Port Tidak Muncul
- **Pemeriksaan 1**: Pastikan Anda menggunakan **Google Chrome** atau **Microsoft Edge**. Mozilla Firefox dan Safari tidak mendukung Web Serial API.
- **Pemeriksaan 2**: Pastikan dashboard dibuka melalui alamat `http://localhost:...` atau `http://127.0.0.1:...`. Web Serial diblokir oleh browser pada protokol `file:///`.
- **Pemeriksaan 3**: Pastikan **Serial Monitor Arduino IDE sudah DITUTUP**. Satu port COM hanya dapat diakses oleh satu aplikasi pada satu waktu.
- **Pemeriksaan 4**: Buka *Device Manager* di Windows. Jika port COM memiliki tanda seru kuning, pasang driver USB serial chip Anda (driver **CH340** atau **CP210x**).

---

### ❓ Servo: ESP32 Tiba-Tiba Restart saat Servo Mulai Bergerak
- **Penyebab**: *Brownout detector* ESP32 aktif karena tegangan drop drastis akibat motor servo menarik arus besar dari ESP32.
- **Solusi**:
  1. Jangan hubungkan kabel VCC merah servo ke pin 3.3V atau VIN ESP32!
  2. Gunakan adaptor eksternal 5V dengan arus minimal 1.5A–2A.
  3. Pastikan kabel GND adaptor eksternal disambungkan ke pin GND ESP32 (**Common Ground**).
  4. Pasang kapasitor elektrolit $100\mu\text{F} - 470\mu\text{F}$ secara paralel pada kutub (+) dan (-) power supply servo untuk menyerap hentakan daya.

---

### ❓ Kamera: Layar Webcam Hitam atau Izin Akses Ditolak
- **Solusi**:
  1. Klik ikon perizinan di samping kiri kolom URL browser Chrome, lalu ubah status **Camera** menjadi **Allow**.
  2. Pastikan tidak ada aplikasi lain (seperti Zoom, Google Meet, Skype, atau aplikasi Kamera bawaan Windows) yang sedang memakai webcam.
  3. Muat ulang halaman dengan menekan tombol `Ctrl + F5`.

---

### ❓ Model AI: Muncul Peringatan "Model Gagal Dimuat"
- **Solusi**:
  1. Klik tombol **"Pengaturan Model & Sensitivitas"** di bawah kartu klasifikasi.
  2. Klik tombol chip **"Model Lokal (./model/)"** untuk memuat file model dari folder lokal di laptop Anda tanpa membutuhkan koneksi internet.
  3. Klik tombol **"Muat Ulang"**.
