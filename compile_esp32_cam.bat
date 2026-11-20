@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

echo ============================================================================
echo  COMPILACAO INDIVIDUAL - ESP32-CAM (Bluepad32 Bluetooth Receiver)
echo ============================================================================

set "ROOT_DIR=%~dp0"
set "TOOLS_DIR=%ROOT_DIR%tools"
set "ARDUINO_CLI=%TOOLS_DIR%\bin\arduino-cli.exe"
set "ARDUINO_CONFIG=%TOOLS_DIR%\arduino-cli.yaml"
set "BUILD_DIR=%ROOT_DIR%build\esp32_cam"

:: Forca modo 100%% portatil
set "ARDUINO_DIRECTORIES_DATA=%TOOLS_DIR%\data"
set "ARDUINO_DIRECTORIES_DOWNLOADS=%TOOLS_DIR%\staging"
set "ARDUINO_DIRECTORIES_USER=%TOOLS_DIR%\sketchbook"

if not exist "%ARDUINO_CLI%" (
    echo [AVISO] Toolkit portatil ainda nao configurado. Executando setup_tools.bat...
    call "%ROOT_DIR%setup_tools.bat" nopause
    if errorlevel 1 exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo.
echo Compilando Firmware ESP32-CAM Receptor Bluetooth (Bluepad32)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" compile ^
    --fqbn esp32-bluepad32:esp32:esp32cam ^
    --output-dir "%BUILD_DIR%" ^
    "%ROOT_DIR%esp32_cam_bt_receiver"

if errorlevel 1 (
    echo.
    echo [ERRO] Falha ao compilar o firmware do ESP32-CAM!
    if "%1"=="" pause
    exit /b 1
)

echo.
echo ============================================================================
echo  SUCESSO! Firmware ESP32-CAM compilado:
echo    - Binario gerado: build\esp32_cam\esp32_cam_bt_receiver.ino.bin
echo ============================================================================
if "%1"=="" pause
exit /b 0
