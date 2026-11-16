@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

echo ============================================================================
echo  MONITOR SERIAL DO ESP32-WROOM (Diagnostico Bluetooth Bluepad32 - 115200)
echo ============================================================================

set "ROOT_DIR=%~dp0"
set "TOOLS_DIR=%ROOT_DIR%tools"
set "ARDUINO_CLI=%TOOLS_DIR%\bin\arduino-cli.exe"
set "ARDUINO_CONFIG=%TOOLS_DIR%\arduino-cli.yaml"

set "ARDUINO_DIRECTORIES_DATA=%TOOLS_DIR%\data"
set "ARDUINO_DIRECTORIES_DOWNLOADS=%TOOLS_DIR%\staging"
set "ARDUINO_DIRECTORIES_USER=%TOOLS_DIR%\sketchbook"

echo.
echo [INFO] Portas seriais detectadas:
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

echo.
echo Abrindo Monitor Serial em %COM_PORT% (115200 bps)... Pressione Ctrl+C para sair.
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" monitor -p %COM_PORT% -c baudrate=115200
pause
