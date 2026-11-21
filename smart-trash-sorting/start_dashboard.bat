@echo off
title Smart Trash Sorting System - Dashboard Launcher
color 0b

echo ======================================================================
echo           SMART TRASH SORTING SYSTEM - LOCAL LAUNCHER
echo ======================================================================
echo.
echo Memeriksa ketersediaan server lokal di komputer Anda...

:: Cek Python
python --version >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Python terdeteksi. Menjalankan HTTP server pada port 8000...
    start "" http://localhost:8000
    python -m http.server 8000
    goto end
)

:: Cek Node.js / npx
npx --version >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Node.js/npx terdeteksi. Menjalankan serve...
    start "" http://localhost:3000
    npx serve -l 3000 .
    goto end
)

:: Jika tidak ada Python atau Node.js
echo.
echo [PERINGATAN] Python atau Node.js tidak ditemukan di PATH.
echo.
echo Untuk menjalankan Web Serial API dan Kamera secara aman:
echo 1. Anda dapat membuka folder ini di VS Code dan gunakan extension 'Live Server'.
echo 2. Atau install Python (https://www.python.org/) / Node.js (https://nodejs.org/).
echo.
echo Mencoba membuka index.html langsung di browser default...
start "" index.html
echo.
echo Catatan: Web Serial API memerlukan akses via http://localhost agar dapat
echo terhubung ke ESP32 secara penuh sesuai standar keamanan browser.
echo.
pause

:end
