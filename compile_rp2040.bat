@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

echo ============================================================================
echo  COMPILACAO INDIVIDUAL - RP2040-Zero (Emulador USB Xbox 360 Wireless)
echo ============================================================================

set "ROOT_DIR=%~dp0"
set "TOOLS_DIR=%ROOT_DIR%tools"
set "ARDUINO_CLI=%TOOLS_DIR%\bin\arduino-cli.exe"
set "ARDUINO_CONFIG=%TOOLS_DIR%\arduino-cli.yaml"
set "BUILD_DIR=%ROOT_DIR%build\rp2040"

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
echo Compilando Firmware RP2040-Zero Emulador USB Xbox 360 (TinyUSB XInput)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" compile ^
    --fqbn rp2040:rp2040:waveshare_rp2040_zero:usbstack=tinyusb ^
    --output-dir "%BUILD_DIR%" ^
    "%ROOT_DIR%rp2040_xinput_usb"

if errorlevel 1 (
    echo.
    echo [ERRO] Falha ao compilar o firmware do RP2040-Zero!
    if "%1"=="" pause
    exit /b 1
)

echo.
echo ============================================================================
echo  SUCESSO! Firmware RP2040-Zero compilado:
echo    - Binario gerado: build\rp2040\rp2040_xinput_usb.ino.uf2
echo ============================================================================
if "%1"=="" pause
exit /b 0
