@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

echo ============================================================================
echo  COMPILACAO COMPLETA - ESP32-WROOM (Bluepad32) + RP2040-Zero (USB XInput)
echo ============================================================================

set "ROOT_DIR=%~dp0"
set "TOOLS_DIR=%ROOT_DIR%tools"
set "ARDUINO_CLI=%TOOLS_DIR%\bin\arduino-cli.exe"
set "ARDUINO_CONFIG=%TOOLS_DIR%\arduino-cli.yaml"
set "BUILD_DIR=%ROOT_DIR%build"

:: Forca modo 100%% portatil
set "ARDUINO_DIRECTORIES_DATA=%TOOLS_DIR%\data"
set "ARDUINO_DIRECTORIES_DOWNLOADS=%TOOLS_DIR%\staging"
set "ARDUINO_DIRECTORIES_USER=%TOOLS_DIR%\sketchbook"

if not exist "%ARDUINO_CLI%" (
    echo [AVISO] Toolkit portatil ainda nao configurado. Executando setup_tools.bat...
    call "%ROOT_DIR%setup_tools.bat" nopause
    if errorlevel 1 exit /b 1
)

if not exist "%BUILD_DIR%\esp32" mkdir "%BUILD_DIR%\esp32"
if not exist "%BUILD_DIR%\rp2040" mkdir "%BUILD_DIR%\rp2040"

echo.
echo [1/2] Compilando Firmware 1: ESP32-WROOM Receptor Bluetooth (Bluepad32)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" compile ^
    --fqbn esp32-bluepad32:esp32:esp32 ^
    --output-dir "%BUILD_DIR%\esp32" ^
    "%ROOT_DIR%esp32_bt_receiver"

if errorlevel 1 (
    echo.
    echo [ERRO] Falha ao compilar o firmware do ESP32-WROOM!
    if "%1"=="" pause
    exit /b 1
)

echo.
echo [2/2] Compilando Firmware 2: RP2040-Zero Emulador USB Xbox 360 (TinyUSB XInput)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" compile ^
    --fqbn rp2040:rp2040:waveshare_rp2040_zero:usbstack=tinyusb ^
    --output-dir "%BUILD_DIR%\rp2040" ^
    "%ROOT_DIR%rp2040_xinput_usb"

if errorlevel 1 (
    echo.
    echo [ERRO] Falha ao compilar o firmware do RP2040-Zero!
    if "%1"=="" pause
    exit /b 1
)

echo.
echo ============================================================================
echo  SUCESSO! Ambos os firmwares foram compilados com sucesso:
echo    - ESP32-WROOM : build\esp32\esp32_bt_receiver.ino.bin
echo    - RP2040-Zero : build\rp2040\rp2040_xinput_usb.ino.uf2
echo ============================================================================
if "%1"=="" pause
exit /b 0
