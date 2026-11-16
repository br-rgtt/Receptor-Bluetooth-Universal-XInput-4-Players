@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

echo ============================================================================
echo  GRAVACAO DO FIRMWARE NO ESP32-WROOM (Receptor Bluetooth Bluepad32)
echo ============================================================================

set "ROOT_DIR=%~dp0"
set "TOOLS_DIR=%ROOT_DIR%tools"
set "ARDUINO_CLI=%TOOLS_DIR%\bin\arduino-cli.exe"
set "ARDUINO_CONFIG=%TOOLS_DIR%\arduino-cli.yaml"
set "BUILD_DIR=%ROOT_DIR%build\esp32"

:: Forca modo 100%% portatil
set "ARDUINO_DIRECTORIES_DATA=%TOOLS_DIR%\data"
set "ARDUINO_DIRECTORIES_DOWNLOADS=%TOOLS_DIR%\staging"
set "ARDUINO_DIRECTORIES_USER=%TOOLS_DIR%\sketchbook"

if not exist "%ARDUINO_CLI%" (
    echo [AVISO] Toolkit portatil nao encontrado. Executando setup_tools.bat...
    call "%ROOT_DIR%setup_tools.bat" nopause
    if errorlevel 1 exit /b 1
)

echo.
echo [INFO] Portas seriais detectadas no computador:
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" board list
echo.

set "COM_PORT=%~1"
if "%COM_PORT%"=="" (
    set /p "COM_PORT=Digite a porta COM do ESP32-WROOM (ex: COM3): "
)

if "%COM_PORT%"=="" (
    echo [ERRO] Nenhuma porta COM informada!
    pause
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo.
echo [1/2] Compilando esp32_bt_receiver...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" compile ^
    --fqbn esp32-bluepad32:esp32:esp32 ^
    --output-dir "%BUILD_DIR%" ^
    "%ROOT_DIR%esp32_bt_receiver"

if errorlevel 1 (
    echo [ERRO] Falha na compilacao do ESP32-WROOM!
    pause
    exit /b 1
)

echo.
echo [2/2] Gravando no ESP32-WROOM na porta %COM_PORT%...
echo       (Dica: Se ficar parado em "Connecting...", segure o botao BOOT do ESP32 por 2 segundos)
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" upload ^
    --fqbn esp32-bluepad32:esp32:esp32 ^
    --port %COM_PORT% ^
    --input-dir "%BUILD_DIR%" ^
    "%ROOT_DIR%esp32_bt_receiver"

if errorlevel 1 (
    echo.
    echo [ERRO] Falha ao gravar no ESP32-WROOM na porta %COM_PORT%.
    echo Verifique se o cabo USB suporta dados e se a porta COM esta correta.
    pause
    exit /b 1
)

echo.
echo ============================================================================
echo  SUCESSO! O ESP32-WROOM foi gravado com o receptor Bluepad32!
echo ============================================================================
pause
exit /b 0
