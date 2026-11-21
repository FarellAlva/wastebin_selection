/**
 * ============================================================================
 * Smart Trash Sorting System - Main Application Logic
 * Integrates: Google Teachable Machine, Web Serial API, and ESP32 Servo
 * ============================================================================
 */

// Global Configuration
const CONFIG = {
  DEFAULT_MODEL_URL: "https://teachablemachine.withgoogle.com/models/YVOo01Icl/",
  LOCAL_MODEL_URL: "./model/",
  BAUD_RATE: 115200,
  SERVO_PIN: 18,
  DEFAULT_THRESHOLD: 0.85,     // Minimal 85% confidence
  STABILITY_FRAMES_REQ: 6,     // Jumlah frame stabil sebelum trigger perintah
  COOLDOWN_MS: 4000,           // Cooldown pencegah pemicuan ganda (4 detik)
  ANGLES: {
    DRY: 30,                   // 30° untuk sampah kering (Kiri)
    WET: 150,                  // 150° untuk sampah basah (Kanan)
    RESET: 90                  // 90° untuk posisi netral
  }
};

// ============================================================================
// State Management
// ============================================================================
const AppState = {
  // Model state
  model: null,
  maxPredictions: 0,
  isModelLoading: false,
  isModelReady: false,
  labels: [],

  // Camera state
  webcam: null,
  isCameraRunning: false,
  animationFrameId: null,

  // Serial state
  serialPort: null,
  serialReader: null,
  serialWriter: null,
  isSerialConnected: false,
  keepReadingSerial: false,

  // Detection logic state
  currentThreshold: CONFIG.DEFAULT_THRESHOLD,
  candidateCategory: null,
  consecutiveFrames: 0,
  isCooldown: false,
  cooldownTimer: null,
  hasTriggeredForCurrentObject: false,
  lastTriggeredTime: 0,

  // Hardware tracking
  currentServoAngle: CONFIG.ANGLES.RESET,

  // History & Statistics
  history: [],
  countDry: 0,
  countWet: 0,
  countTotal: 0
};

// ============================================================================
// DOM Elements Cache
// ============================================================================
const DOM = {
  // Status Pills
  pillSerial: document.getElementById('pill-serial'),
  pillCamera: document.getElementById('pill-camera'),
  pillModel: document.getElementById('pill-model'),

  // Counters
  countDry: document.getElementById('count-dry'),
  countWet: document.getElementById('count-wet'),
  countTotal: document.getElementById('count-total'),
  statLastTime: document.getElementById('stat-last-time'),

  // Camera Elements
  webcamContainer: document.getElementById('webcam-container'),
  cameraPlaceholder: document.getElementById('camera-placeholder'),
  btnStartCamera: document.getElementById('btn-start-camera'),
  btnStopCamera: document.getElementById('btn-stop-camera'),
  targetBox: document.querySelector('.target-box'),
  detectionToast: document.getElementById('detection-toast'),
  toastIcon: document.getElementById('toast-icon'),
  toastText: document.getElementById('toast-text'),

  // Stability
  stabilityCount: document.getElementById('stability-count'),
  stabilityBar: document.getElementById('stability-bar'),
  cooldownIndicator: document.getElementById('cooldown-indicator'),
  cooldownText: document.getElementById('cooldown-text'),

  // AI Result Elements
  primaryResultBox: document.getElementById('primary-result-box'),
  primaryCategoryName: document.getElementById('primary-category-name'),
  primaryCategorySub: document.getElementById('primary-category-sub'),
  primaryConfidencePct: document.getElementById('primary-confidence-pct'),
  thresholdValText: document.getElementById('threshold-val-text'),
  pctDry: document.getElementById('pct-dry'),
  barDry: document.getElementById('bar-dry'),
  pctWet: document.getElementById('pct-wet'),
  barWet: document.getElementById('bar-wet'),

  // Model Config
  btnToggleConfig: document.getElementById('btn-toggle-config'),
  configBody: document.getElementById('config-body'),
  inputModelUrl: document.getElementById('input-model-url'),
  btnReloadModel: document.getElementById('btn-reload-model'),
  btnSrcOnline: document.getElementById('btn-src-online'),
  btnSrcLocal: document.getElementById('btn-src-local'),
  rangeThreshold: document.getElementById('range-threshold'),
  rangeThresholdLabel: document.getElementById('range-threshold-label'),

  // Serial Hardware
  btnConnectEsp32: document.getElementById('btn-connect-esp32'),
  btnDisconnectEsp32: document.getElementById('btn-disconnect-esp32'),
  badgeConnectionState: document.getElementById('badge-connection-state'),
  connStateLabel: document.getElementById('conn-state-label'),
  specPortName: document.getElementById('spec-port-name'),

  // Servo Control
  servoAngleText: document.getElementById('servo-angle-text'),
  servoNeedle: document.getElementById('servo-needle'),
  servoDirectionText: document.getElementById('servo-direction-text'),
  btnManualDry: document.getElementById('btn-manual-dry'),
  btnManualReset: document.getElementById('btn-manual-reset'),
  btnManualWet: document.getElementById('btn-manual-wet'),

  // Terminal & History
  serialTerminal: document.getElementById('serial-terminal'),
  btnClearLog: document.getElementById('btn-clear-log'),
  historyTableBody: document.getElementById('history-table-body'),
  historyEmptyRow: document.getElementById('history-empty-row'),
  btnExportCsv: document.getElementById('btn-export-csv'),
  btnClearHistory: document.getElementById('btn-clear-history')
};

// ============================================================================
// Sound Feedback (Web Audio API Synthesizer)
// ============================================================================
class AudioChime {
  static playTone(freq, type, duration) {
    try {
      const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = type;
      osc.frequency.setValueAtTime(freq, audioCtx.currentTime);
      gain.gain.setValueAtTime(0.08, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.0001, audioCtx.currentTime + duration);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start();
      osc.stop(audioCtx.currentTime + duration);
    } catch (e) {
      // Audio context might be restricted before interaction
    }
  }

  static success() {
    AudioChime.playTone(587.33, 'sine', 0.15); // D5
    setTimeout(() => AudioChime.playTone(880.00, 'sine', 0.25), 120); // A5
  }

  static neutral() {
    AudioChime.playTone(440.00, 'triangle', 0.12);
  }
}

// ============================================================================
// Terminal & Log Utility
// ============================================================================
function logTerminal(message, type = 'info') {
  const line = document.createElement('div');
  line.className = `term-line ${type}`;

  const now = new Date();
  const timeStr = now.toTimeString().split(' ')[0] + '.' + String(now.getMilliseconds()).padStart(3, '0');
  line.textContent = `[${timeStr}] ${message}`;

  DOM.serialTerminal.appendChild(line);
  DOM.serialTerminal.scrollTop = DOM.serialTerminal.scrollHeight;
}

// ============================================================================
// Model & Classification Engine
// ============================================================================
class ModelManager {
  /**
   * Load model from specified URL or local path
   */
  static async load(baseUrl) {
    if (AppState.isModelLoading) return;
    AppState.isModelLoading = true;
    AppState.isModelReady = false;

    // Update Status UI
    DOM.pillModel.innerHTML = `
      <span class="status-dot loading"></span>
      <span class="status-text">Memuat Model...</span>
    `;
    logTerminal(`Memulai pengunduhan model dari: ${baseUrl}`, 'info');

    try {
      // Pastikan trailing slash
      if (!baseUrl.endsWith('/')) baseUrl += '/';
      const modelURL = baseUrl + "model.json";
      const metadataURL = baseUrl + "metadata.json";

      // Load model via Teachable Machine Image library
      AppState.model = await tmImage.load(modelURL, metadataURL);
      AppState.maxPredictions = AppState.model.getTotalClasses();
      AppState.labels = AppState.model.getClassLabels();

      AppState.isModelReady = true;
      AppState.isModelLoading = false;

      // Update UI
      DOM.pillModel.innerHTML = `
        <span class="status-dot connected"></span>
        <span class="status-text">Model: Siap (${AppState.maxPredictions} Kelas)</span>
      `;
      logTerminal(`Model berhasil dimuat! Kelas terdeteksi: [${AppState.labels.join(', ')}]`, 'recv');

      // Tampilkan toast
      showToast(`Model Siap: ${AppState.labels.join(', ')}`, '✅');
    } catch (err) {
      AppState.isModelLoading = false;
      AppState.isModelReady = false;
      DOM.pillModel.innerHTML = `
        <span class="status-dot disconnected"></span>
        <span class="status-text">Model Gagal Dimuat</span>
      `;
      logTerminal(`Gagal memuat model: ${err.message}`, 'err');
      showToast('Gagal memuat model. Periksa koneksi internet / path lokal.', '❌');
    }
  }

  /**
   * Dynamic Label Mapping to Standard Categories (DRY / WET)
   * Mengikuti ketentuan: mapping berdasarkan metadata & nama kelas,
   * bukan asumsi nomor urut.
   */
  static mapCategory(rawLabel) {
    const clean = String(rawLabel).toLowerCase().trim();

    // Deteksi Sampah Kering (DRY)
    if (clean.includes("kering") || clean.includes("dry") || clean.includes("anorganik") || clean.includes("kardus") || clean.includes("plastik")) {
      return {
        type: "DRY",
        displayName: "Sampah Kering",
        subDesc: "Kertas, Kardus, Plastik, Daun Kering",
        angle: CONFIG.ANGLES.DRY,
        command: "DRY\n",
        direction: "Kiri (30°)",
        themeClass: "detected-dry",
        barClass: "bar-dry"
      };
    }

    // Deteksi Sampah Basah (WET)
    if (clean.includes("basah") || clean.includes("wet") || clean.includes("organik") || clean.includes("makanan") || clean.includes("buah")) {
      return {
        type: "WET",
        displayName: "Sampah Basah",
        subDesc: "Sisa Makanan, Kulit Buah, Sayuran Basah",
        angle: CONFIG.ANGLES.WET,
        command: "WET\n",
        direction: "Kanan (150°)",
        themeClass: "detected-wet",
        barClass: "bar-wet"
      };
    }

    // Unrecognized or Neutral background
    return {
      type: "UNKNOWN",
      displayName: rawLabel || "Tidak Dikenal",
      subDesc: "Kategori tidak sesuai kriteria pemilahan",
      angle: CONFIG.ANGLES.RESET,
      command: null,
      direction: "Netral (90°)",
      themeClass: "",
      barClass: ""
    };
  }
}

// ============================================================================
// Camera & Vision Loop
// ============================================================================
class CameraManager {
  static async start() {
    if (!AppState.isModelReady) {
      alert("Harap tunggu hingga model Teachable Machine selesai dimuat!");
      return;
    }

    if (AppState.isCameraRunning) return;

    try {
      logTerminal("Mengakses webcam laptop via MediaDevices...", 'info');

      const width = 440;
      const height = 330;
      const flip = true; // mirror mode untuk kenyamanan pengguna
      AppState.webcam = new tmImage.Webcam(width, height, flip);

      await AppState.webcam.setup(); // Meminta izin kamera
      await AppState.webcam.play();

      // Hapus placeholder dan masukkan elemen canvas kamera
      DOM.cameraPlaceholder.style.display = "none";
      DOM.webcamContainer.querySelectorAll('canvas').forEach(c => c.remove());
      DOM.webcamContainer.appendChild(AppState.webcam.canvas);

      AppState.isCameraRunning = true;
      DOM.btnStartCamera.disabled = true;
      DOM.btnStopCamera.disabled = false;

      // Update UI Status Pill
      DOM.pillCamera.innerHTML = `
        <span class="status-dot active"></span>
        <span class="status-text">Kamera: Aktif</span>
      `;
      logTerminal("Webcam aktif. Menjalankan pipeline klasifikasi real-time.", 'recv');
      showToast("Kamera aktif. Dekatkan sampah ke kotak panduan.", '📷');

      // Mulai classification loop
      CameraManager.loop();
    } catch (err) {
      logTerminal(`Akses webcam ditolak atau error: ${err.message}`, 'err');
      alert(`Gagal mengakses webcam: ${err.message}\nPastikan browser memiliki izin webcam dan tidak sedang digunakan aplikasi lain.`);
      DOM.pillCamera.innerHTML = `
        <span class="status-dot disconnected"></span>
        <span class="status-text">Kamera: Error</span>
      `;
    }
  }

  static stop() {
    if (!AppState.isCameraRunning) return;

    if (AppState.webcam) {
      AppState.webcam.stop();
    }
    if (AppState.animationFrameId) {
      window.cancelAnimationFrame(AppState.animationFrameId);
      AppState.animationFrameId = null;
    }

    AppState.isCameraRunning = false;
    DOM.btnStartCamera.disabled = false;
    DOM.btnStopCamera.disabled = true;
    DOM.cameraPlaceholder.style.display = "flex";
    DOM.webcamContainer.querySelectorAll('canvas').forEach(c => c.remove());

    DOM.pillCamera.innerHTML = `
      <span class="status-dot inactive"></span>
      <span class="status-text">Kamera: Nonaktif</span>
    `;

    // Reset visual state
    resetClassificationDisplay();
    logTerminal("Webcam dimatikan.", 'warn');
    showToast("Deteksi dihentikan.", '⏹️');
  }

  static async loop() {
    if (!AppState.isCameraRunning) return;

    AppState.webcam.update();
    await CameraManager.predict();

    AppState.animationFrameId = window.requestAnimationFrame(CameraManager.loop);
  }

  static async predict() {
    if (!AppState.model || !AppState.webcam.canvas) return;

    // Lakukan inferensi model
    const predictions = await AppState.model.predict(AppState.webcam.canvas);

    let topPrediction = null;
    let highestProb = -1;

    // Update bar probabilitas tiap kelas
    for (let i = 0; i < predictions.length; i++) {
      const pred = predictions[i];
      const probability = pred.probability;
      const percentStr = (probability * 100).toFixed(1) + "%";
      const catInfo = ModelManager.mapCategory(pred.className);

      if (catInfo.type === "DRY") {
        DOM.pctDry.textContent = percentStr;
        DOM.barDry.style.width = percentStr;
      } else if (catInfo.type === "WET") {
        DOM.pctWet.textContent = percentStr;
        DOM.barWet.style.width = percentStr;
      }

      if (probability > highestProb) {
        highestProb = probability;
        topPrediction = {
          rawClassName: pred.className,
          probability: probability,
          catInfo: catInfo
        };
      }
    }

    // Evaluasi aturan keputusan AI
    evaluateClassification(topPrediction);
  }
}

// ============================================================================
// Decision & Stability Logic
// ============================================================================
/**
 * Evaluasi hasil prediksi dengan:
 * 1. Confidence threshold >= 85%
 * 2. Kestabilan beberapa frame berturut-turut (STABILITY_FRAMES_REQ)
 * 3. Mekanisme Cooldown & Single-shot per objek
 */
function evaluateClassification(topPrediction) {
  if (!topPrediction) return;

  const confidence = topPrediction.probability;
  const cat = topPrediction.catInfo;
  const isAboveThreshold = confidence >= AppState.currentThreshold;

  // Jika sedang cooldown, kunci evaluasi servo tapi tetap update preview
  if (AppState.isCooldown) {
    updatePrimaryDisplay(cat, confidence, `Objek terproses. Menunggu cooldown...`);
    return;
  }

  // Objek valid yang dapat memicu servo (DRY atau WET)
  const isValidSortable = (cat.type === "DRY" || cat.type === "WET");

  if (isAboveThreshold && isValidSortable) {
    // Cek apakah kandidat sama dengan frame sebelumnya
    if (AppState.candidateCategory === cat.type) {
      AppState.consecutiveFrames++;
    } else {
      AppState.candidateCategory = cat.type;
      AppState.consecutiveFrames = 1;
    }

    // Update meter kestabilan
    const progressPct = Math.min(100, Math.round((AppState.consecutiveFrames / CONFIG.STABILITY_FRAMES_REQ) * 100));
    DOM.stabilityCount.textContent = `${AppState.consecutiveFrames} / ${CONFIG.STABILITY_FRAMES_REQ} Frame`;
    DOM.stabilityBar.style.width = `${progressPct}%`;

    updatePrimaryDisplay(cat, confidence, `Menstabilkan deteksi (${AppState.consecutiveFrames}/${CONFIG.STABILITY_FRAMES_REQ})...`);

    // Target visual box highlight
    if (cat.type === "DRY") {
      DOM.targetBox.className = "target-box active-dry";
    } else if (cat.type === "WET") {
      DOM.targetBox.className = "target-box active-wet";
    }

    // Jika sudah stabil dan belum pernah trigger untuk objek saat ini
    if (AppState.consecutiveFrames >= CONFIG.STABILITY_FRAMES_REQ && !AppState.hasTriggeredForCurrentObject) {
      triggerSortingAction(cat, confidence);
    }
  } else {
    // Confidence drop atau objek bergeser/hilang
    if (AppState.consecutiveFrames > 0) {
      AppState.consecutiveFrames = Math.max(0, AppState.consecutiveFrames - 1);
      const progressPct = Math.round((AppState.consecutiveFrames / CONFIG.STABILITY_FRAMES_REQ) * 100);
      DOM.stabilityCount.textContent = `${AppState.consecutiveFrames} / ${CONFIG.STABILITY_FRAMES_REQ} Frame`;
      DOM.stabilityBar.style.width = `${progressPct}%`;
    }

    if (AppState.consecutiveFrames === 0) {
      AppState.candidateCategory = null;
      DOM.targetBox.className = "target-box";
      updatePrimaryDisplay(null, confidence, "Posisikan sampah di depan kamera");
    }
  }
}

/**
 * Eksekusi pengiriman perintah ke ESP32 dan pencatatan riwayat
 */
async function triggerSortingAction(categoryInfo, confidence) {
  AppState.hasTriggeredForCurrentObject = true;
  AppState.isCooldown = true;
  AppState.lastTriggeredTime = Date.now();

  const command = categoryInfo.command; // "DRY\n" atau "WET\n"
  const angle = categoryInfo.angle;
  const confPercent = (confidence * 100).toFixed(1) + "%";

  logTerminal(`[AI TRIGGER] ${categoryInfo.displayName} terkonfirmasi stabil! (${confPercent}). Mengirim: ${command.trim()}`, 'sent');
  AudioChime.success();

  // Kirim perintah ke ESP32 melalui Web Serial
  let sendStatus = "Belum Terhubung ESP32";
  if (AppState.isSerialConnected) {
    const success = await SerialManager.write(command);
    sendStatus = success ? "Terkirim ke ESP32" : "Gagal Kirim";
  } else {
    logTerminal(`ESP32 belum terhubung USB. Simulasi pergerakan servo pada antarmuka.`, 'warn');
    sendStatus = "Simulasi UI (Offline ESP32)";
  }

  // Update Visual Servo Gauge di UI
  ServoController.updateVisualAngle(angle, `Memilah: ${categoryInfo.direction}`);

  // Update Statistik
  if (categoryInfo.type === "DRY") {
    AppState.countDry++;
    DOM.countDry.textContent = AppState.countDry;
  } else if (categoryInfo.type === "WET") {
    AppState.countWet++;
    DOM.countWet.textContent = AppState.countWet;
  }
  AppState.countTotal++;
  DOM.countTotal.textContent = AppState.countTotal;
  DOM.statLastTime.textContent = `Terakhir: ${new Date().toLocaleTimeString()}`;

  // Catat ke Tabel Riwayat
  HistoryManager.addEntry({
    time: new Date().toLocaleTimeString(),
    category: categoryInfo.displayName,
    type: categoryInfo.type,
    confidence: confPercent,
    command: command.trim(),
    angle: `${angle}°`,
    status: sendStatus
  });

  showToast(`Berhasil memilah: ${categoryInfo.displayName} (${categoryInfo.direction})`, '🎉');

  // Mulai hitung mundur cooldown
  startCooldown(CONFIG.COOLDOWN_MS);
}

/**
 * Cooldown timer untuk menghindari trigger berulang
 */
function startCooldown(durationMs) {
  DOM.cooldownIndicator.classList.add('active');
  let remainingMs = durationMs;

  clearInterval(AppState.cooldownTimer);
  AppState.cooldownTimer = setInterval(() => {
    remainingMs -= 500;
    const secLeft = (remainingMs / 1000).toFixed(1);
    DOM.cooldownText.textContent = `Cooldown aktif: ${secLeft}s (mengunci deteksi ganda)...`;

    if (remainingMs <= 0) {
      clearInterval(AppState.cooldownTimer);
      AppState.isCooldown = false;
      AppState.hasTriggeredForCurrentObject = false;
      AppState.consecutiveFrames = 0;
      AppState.candidateCategory = null;

      DOM.cooldownIndicator.classList.remove('active');
      DOM.stabilityCount.textContent = `0 / ${CONFIG.STABILITY_FRAMES_REQ} Frame`;
      DOM.stabilityBar.style.width = `0%`;
      DOM.targetBox.className = "target-box";

      // Kembalikan visual servo ke netral di antarmuka
      ServoController.updateVisualAngle(CONFIG.ANGLES.RESET, "Standby Netral (90°)");

      logTerminal("Cooldown selesai. Sistem siap mendeteksi objek sampah berikutnya.", 'info');
      showToast("Sistem siap mendeteksi sampah berikutnya.", '✨');
    }
  }, 500);
}

function updatePrimaryDisplay(cat, confidence, subText) {
  const pctStr = (confidence * 100).toFixed(1) + "%";
  DOM.primaryConfidencePct.textContent = pctStr;

  if (cat && (cat.type === "DRY" || cat.type === "WET")) {
    DOM.primaryCategoryName.textContent = cat.displayName;
    DOM.primaryCategorySub.textContent = subText || cat.subDesc;
    DOM.primaryResultBox.className = `primary-result-box ${cat.themeClass}`;
  } else {
    DOM.primaryCategoryName.textContent = "Menunggu Objek...";
    DOM.primaryCategorySub.textContent = subText || "Posisikan sampah di depan kamera";
    DOM.primaryResultBox.className = "primary-result-box";
  }
}

function resetClassificationDisplay() {
  DOM.primaryCategoryName.textContent = "Kamera Nonaktif";
  DOM.primaryCategorySub.textContent = "Tekan Mulai Deteksi untuk mengaktifkan";
  DOM.primaryConfidencePct.textContent = "0%";
  DOM.primaryResultBox.className = "primary-result-box";
  DOM.pctDry.textContent = "0.0%";
  DOM.barDry.style.width = "0%";
  DOM.pctWet.textContent = "0.0%";
  DOM.barWet.style.width = "0%";
  DOM.stabilityCount.textContent = "0 / 6 Frame";
  DOM.stabilityBar.style.width = "0%";
  DOM.targetBox.className = "target-box";
}

function showToast(text, icon = 'ℹ️') {
  DOM.toastIcon.textContent = icon;
  DOM.toastText.textContent = text;
}

// ============================================================================
// Web Serial API Manager (ESP32 USB Communication)
// ============================================================================
class SerialManager {
  /**
   * Request user permission and connect to ESP32
   */
  static async connect() {
    if (!('serial' in navigator)) {
      alert("Browser Anda belum mendukung Web Serial API.\nGunakan Google Chrome Desktop atau Microsoft Edge versi terbaru!");
      return;
    }

    try {
      logTerminal("Membuka prompt pemilih port USB...", 'info');
      // Meminta user memilih port serial ESP32
      AppState.serialPort = await navigator.serial.requestPort();

      // Buka port dengan baud rate 115200
      await AppState.serialPort.open({ baudRate: CONFIG.BAUD_RATE });

      AppState.isSerialConnected = true;
      AppState.keepReadingSerial = true;

      // Update UI Status
      DOM.pillSerial.innerHTML = `
        <span class="status-dot connected"></span>
        <span class="status-text">ESP32: Terhubung (115200)</span>
      `;
      DOM.badgeConnectionState.innerHTML = `
        <span class="dot-indicator green"></span>
        <span>Terhubung ke USB Serial</span>
      `;
      DOM.specPortName.textContent = "ESP32 (USB-UART Port)";
      DOM.btnConnectEsp32.disabled = true;
      DOM.btnDisconnectEsp32.disabled = false;

      logTerminal("ESP32 Berhasil Terhubung pada baud rate 115200!", 'recv');
      AudioChime.neutral();

      // Kirim perintah RESET awal untuk memastikan posisi 90°
      await SerialManager.write("RESET\n");

      // Jalankan worker pembaca feedback Serial dari ESP32
      SerialManager.readLoop();
    } catch (err) {
      AppState.isSerialConnected = false;
      logTerminal(`Koneksi serial dibatalkan atau gagal: ${err.message}`, 'err');
    }
  }

  /**
   * Disconnect serial port cleanly
   */
  static async disconnect() {
    AppState.keepReadingSerial = false;

    if (AppState.serialReader) {
      try {
        await AppState.serialReader.cancel();
      } catch (e) {}
    }

    if (AppState.serialPort) {
      try {
        await AppState.serialPort.close();
      } catch (e) {}
      AppState.serialPort = null;
    }

    AppState.isSerialConnected = false;

    // Update UI Status
    DOM.pillSerial.innerHTML = `
      <span class="status-dot disconnected"></span>
      <span class="status-text">ESP32: Terputus</span>
    `;
    DOM.badgeConnectionState.innerHTML = `
      <span class="dot-indicator red"></span>
      <span>Belum Terhubung</span>
    `;
    DOM.specPortName.textContent = "Tidak aktif";
    DOM.btnConnectEsp32.disabled = false;
    DOM.btnDisconnectEsp32.disabled = true;

    logTerminal("Koneksi ESP32 diputus.", 'warn');
  }

  /**
   * Send ASCII command string to ESP32
   */
  static async write(commandString) {
    if (!AppState.isSerialConnected || !AppState.serialPort || !AppState.serialPort.writable) {
      return false;
    }

    try {
      const textEncoder = new TextEncoderStream();
      const writableStreamClosed = textEncoder.readable.pipeTo(AppState.serialPort.writable);
      const writer = textEncoder.writable.getWriter();

      await writer.write(commandString);
      writer.releaseLock();
      await writableStreamClosed.catch(() => {}); // catch close

      logTerminal(`[TX -> ESP32]: ${commandString.trim()}`, 'sent');
      return true;
    } catch (err) {
      logTerminal(`Gagal mengirim data serial: ${err.message}`, 'err');
      return false;
    }
  }

  /**
   * Continuous background reader for ESP32 serial output
   */
  static async readLoop() {
    let lineBuffer = "";

    while (AppState.serialPort && AppState.serialPort.readable && AppState.keepReadingSerial) {
      try {
        const textDecoder = new TextDecoderStream();
        const readableStreamClosed = AppState.serialPort.readable.pipeTo(textDecoder.writable);
        AppState.serialReader = textDecoder.readable.getReader();

        while (AppState.keepReadingSerial) {
          const { value, done } = await AppState.serialReader.read();
          if (done) break;

          if (value) {
            lineBuffer += value;
            const lines = lineBuffer.split(/\r?\n/);
            // Simpan sisa string yang belum memiliki newline
            lineBuffer = lines.pop();

            for (const line of lines) {
              const trimmed = line.trim();
              if (trimmed.length > 0) {
                logTerminal(`[RX <- ESP32]: ${trimmed}`, 'recv');
              }
            }
          }
        }
      } catch (err) {
        if (AppState.keepReadingSerial) {
          logTerminal(`Error membaca serial: ${err.message}`, 'err');
        }
        break;
      }
    }
  }
}

// ============================================================================
// Servo Controller (Visual & Manual Dispatch)
// ============================================================================
class ServoController {
  /**
   * Update visual gauge needle and labels
   * Needle SVG transform formula: (targetAngle - 90) deg
   */
  static updateVisualAngle(angle, directionDescription) {
    AppState.currentServoAngle = angle;
    DOM.servoAngleText.textContent = `${angle}°`;

    // SVG Needle Rotation: 90 deg = 0deg rotate.
    // 30 deg = -60deg rotate. 150 deg = +60deg rotate.
    const rotationDeg = angle - 90;
    DOM.servoNeedle.setAttribute('transform', `rotate(${rotationDeg}, 150, 150)`);

    if (directionDescription) {
      DOM.servoDirectionText.textContent = `Arah: ${directionDescription}`;
    }
  }

  /**
   * Send Manual Command (DRY, WET, or RESET)
   */
  static async sendManualCommand(type) {
    let command = "";
    let angle = CONFIG.ANGLES.RESET;
    let desc = "";

    if (type === "DRY") {
      command = "DRY\n";
      angle = CONFIG.ANGLES.DRY;
      desc = "Manual Kiri (30° - Sampah Kering)";
    } else if (type === "WET") {
      command = "WET\n";
      angle = CONFIG.ANGLES.WET;
      desc = "Manual Kanan (150° - Sampah Basah)";
    } else {
      command = "RESET\n";
      angle = CONFIG.ANGLES.RESET;
      desc = "Manual Netral (90°)";
    }

    ServoController.updateVisualAngle(angle, desc);
    AudioChime.neutral();

    let sendStatus = "Belum Terhubung ESP32";
    if (AppState.isSerialConnected) {
      const ok = await SerialManager.write(command);
      sendStatus = ok ? "Terkirim (Manual)" : "Gagal Kirim";
    } else {
      logTerminal(`Perintah ${type} dijalankan secara simulasi pada antarmuka.`, 'warn');
      sendStatus = "Simulasi UI (Offline)";
    }

    // Catat aksi manual ke tabel
    HistoryManager.addEntry({
      time: new Date().toLocaleTimeString(),
      category: `Manual ${type}`,
      type: type,
      confidence: "Manual",
      command: command.trim(),
      angle: `${angle}°`,
      status: sendStatus
    });
  }
}

// ============================================================================
// History & Data Export Manager
// ============================================================================
class HistoryManager {
  static addEntry(item) {
    AppState.history.unshift(item); // data terbaru di atas
    if (AppState.history.length > 100) AppState.history.pop(); // batasi 100 riwayat

    HistoryManager.render();
  }

  static render() {
    if (AppState.history.length === 0) {
      DOM.historyTableBody.innerHTML = `
        <tr class="empty-row">
          <td colspan="7">Belum ada data pemilahan. Jalankan kamera untuk memulai klasifikasi otomatis.</td>
        </tr>
      `;
      return;
    }

    DOM.historyTableBody.innerHTML = "";

    AppState.history.forEach((row, index) => {
      const tr = document.createElement('tr');

      let badgeClass = "badge-tag neutral";
      if (row.type === "DRY") badgeClass = "badge-tag dry";
      else if (row.type === "WET") badgeClass = "badge-tag wet";

      tr.innerHTML = `
        <td>${AppState.history.length - index}</td>
        <td>${row.time}</td>
        <td><span class="${badgeClass}">${row.category}</span></td>
        <td><strong>${row.confidence}</strong></td>
        <td><code>${row.command}</code></td>
        <td>${row.angle}</td>
        <td>${row.status}</td>
      `;

      DOM.historyTableBody.appendChild(tr);
    });
  }

  static clear() {
    if (confirm("Apakah Anda yakin ingin menghapus seluruh riwayat pemilahan?")) {
      AppState.history = [];
      AppState.countDry = 0;
      AppState.countWet = 0;
      AppState.countTotal = 0;

      DOM.countDry.textContent = "0";
      DOM.countWet.textContent = "0";
      DOM.countTotal.textContent = "0";
      DOM.statLastTime.textContent = "Belum ada pemilahan";

      HistoryManager.render();
      logTerminal("Riwayat pemilahan berhasil dibersihkan.", 'info');
    }
  }

  static exportCSV() {
    if (AppState.history.length === 0) {
      alert("Belum ada riwayat pemilahan untuk diekspor!");
      return;
    }

    let csvContent = "data:text/csv;charset=utf-8,";
    csvContent += "No,Waktu,Kategori,Confidence,Perintah,Sudut,Status\n";

    AppState.history.forEach((row, idx) => {
      csvContent += `${AppState.history.length - idx},"${row.time}","${row.category}","${row.confidence}","${row.command}","${row.angle}","${row.status}"\n`;
    });

    const encodedUri = encodeURI(csvContent);
    const link = document.createElement("a");
    link.setAttribute("href", encodedUri);
    link.setAttribute("download", `smart_trash_history_${Date.now()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);

    logTerminal("Data riwayat berhasil diekspor ke CSV.", 'recv');
  }
}

// ============================================================================
// Event Listeners & Initialization
// ============================================================================
function initEventListeners() {
  // Camera Buttons
  DOM.btnStartCamera.addEventListener('click', () => CameraManager.start());
  DOM.btnStopCamera.addEventListener('click', () => CameraManager.stop());

  // Serial Connection Buttons
  DOM.btnConnectEsp32.addEventListener('click', () => SerialManager.connect());
  DOM.btnDisconnectEsp32.addEventListener('click', () => SerialManager.disconnect());

  // Manual Servo Buttons
  DOM.btnManualDry.addEventListener('click', () => ServoController.sendManualCommand("DRY"));
  DOM.btnManualReset.addEventListener('click', () => ServoController.sendManualCommand("RESET"));
  DOM.btnManualWet.addEventListener('click', () => ServoController.sendManualCommand("WET"));

  // Terminal & History
  DOM.btnClearLog.addEventListener('click', () => {
    DOM.serialTerminal.innerHTML = '<div class="term-line info">[Sistem] Log dibersihkan.</div>';
  });
  DOM.btnClearHistory.addEventListener('click', () => HistoryManager.clear());
  DOM.btnExportCsv.addEventListener('click', () => HistoryManager.exportCSV());

  // Config Accordion Toggle
  DOM.btnToggleConfig.addEventListener('click', () => {
    const isHidden = DOM.configBody.style.display === "none";
    DOM.configBody.style.display = isHidden ? "flex" : "none";
    DOM.btnToggleConfig.classList.toggle('open', isHidden);
  });

  // Threshold Slider
  DOM.rangeThreshold.addEventListener('input', (e) => {
    const val = parseInt(e.target.value, 10);
    AppState.currentThreshold = val / 100;
    DOM.rangeThresholdLabel.textContent = `${val}%`;
    DOM.thresholdValText.textContent = `${val}%`;
  });

  // Model Source Shortcuts
  DOM.btnSrcOnline.addEventListener('click', () => {
    DOM.inputModelUrl.value = CONFIG.DEFAULT_MODEL_URL;
    ModelManager.load(CONFIG.DEFAULT_MODEL_URL);
  });
  DOM.btnSrcLocal.addEventListener('click', () => {
    DOM.inputModelUrl.value = CONFIG.LOCAL_MODEL_URL;
    ModelManager.load(CONFIG.LOCAL_MODEL_URL);
  });
  DOM.btnReloadModel.addEventListener('click', () => {
    const url = DOM.inputModelUrl.value.trim();
    if (url) ModelManager.load(url);
  });

  // Listen to browser port disconnect events
  if ('serial' in navigator) {
    navigator.serial.addEventListener('disconnect', (event) => {
      logTerminal("Perangkat USB Serial terputus secara fisik!", 'warn');
      SerialManager.disconnect();
    });
  }
}

// Bootstrap Application
window.addEventListener('DOMContentLoaded', async () => {
  initEventListeners();
  ServoController.updateVisualAngle(CONFIG.ANGLES.RESET, "Standby Netral (90°)");

  // Muat model awal dari Teachable Machine Online (dengan fallback otomatis jika ada kendala)
  try {
    await ModelManager.load(CONFIG.DEFAULT_MODEL_URL);
  } catch (e) {
    logTerminal("Mencoba memuat model dari folder lokal ./model/...", 'warn');
    await ModelManager.load(CONFIG.LOCAL_MODEL_URL);
  }
});
