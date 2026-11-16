@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

echo ============================================================================
echo  SETUP PORTATIL - Receptor Bluetooth Universal (ESP32-WROOM + RP2040-Zero)
echo ============================================================================

set "ROOT_DIR=%~dp0"
set "TOOLS_DIR=%ROOT_DIR%tools"
set "BIN_DIR=%TOOLS_DIR%\bin"
set "ARDUINO_CLI=%BIN_DIR%\arduino-cli.exe"
set "ARDUINO_CONFIG=%TOOLS_DIR%\arduino-cli.yaml"

:: Forca modo 100%% portatil dentro da pasta tools do projeto
set "ARDUINO_DIRECTORIES_DATA=%TOOLS_DIR%\data"
set "ARDUINO_DIRECTORIES_DOWNLOADS=%TOOLS_DIR%\staging"
set "ARDUINO_DIRECTORIES_USER=%TOOLS_DIR%\sketchbook"

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
if not exist "%ARDUINO_DIRECTORIES_DATA%" mkdir "%ARDUINO_DIRECTORIES_DATA%"
if not exist "%ARDUINO_DIRECTORIES_DOWNLOADS%" mkdir "%ARDUINO_DIRECTORIES_DOWNLOADS%"
if not exist "%ARDUINO_DIRECTORIES_USER%" mkdir "%ARDUINO_DIRECTORIES_USER%"

:: Verifica se o arduino-cli ja existe na pasta portatil tools\bin
if not exist "%ARDUINO_CLI%" (
    if exist "%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" (
        echo [INFO] Copiando arduino-cli da instalacao local para a pasta portatil tools\bin...
        copy /Y "%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" "%ARDUINO_CLI%" >nul
    ) else (
        echo [INFO] Baixando arduino-cli oficial para Windows x64 em tools\bin...
        powershell -NoProfile -ExecutionPolicy Bypass -Command ^
            "$url = 'https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip'; " ^
            "$zip = '%TOOLS_DIR%\staging\arduino-cli.zip'; " ^
            "Invoke-WebRequest -Uri $url -OutFile $zip; " ^
            "Expand-Archive -Path $zip -DestinationPath '%BIN_DIR%' -Force"
    )
)

if not exist "%ARDUINO_CLI%" (
    echo [ERRO] Nao foi possivel localizar ou baixar o arduino-cli.exe!
    pause
    exit /b 1
)

echo [1/5] Atualizando indices de placas (ESP32 Bluepad32 e RP2040)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" core update-index
if errorlevel 1 (
    echo [ERRO] Falha ao atualizar indices de placas.
    pause
    exit /b 1
)

echo [2/5] Instalando toolkit portatil do ESP32 + Bluepad32 (esp32-bluepad32:esp32)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" core install esp32-bluepad32:esp32
if errorlevel 1 (
    echo [ERRO] Falha ao instalar o core esp32-bluepad32:esp32.
    pause
    exit /b 1
)

echo [3/5] Desbloqueando limite de conexoes Bluetooth Classico (bt_max_acl_conn: 2 -> 7) em libbtstack.a...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ar = Get-ChildItem '%TOOLS_DIR%\data\packages' -Recurse -Filter 'xtensa-esp32-elf-ar.exe' | Select-Object -First 1 -ExpandProperty FullName; " ^
    "$lib = Get-ChildItem '%TOOLS_DIR%\data\packages\esp32-bluepad32' -Recurse -Filter 'libbtstack.a' | Where-Object { $_.FullName -match '\\esp32\\lib\\' } | Select-Object -First 1 -ExpandProperty FullName; " ^
    "if ($ar -and $lib) { " ^
    "  Push-Location '%TOOLS_DIR%\staging'; " ^
    "  & $ar x $lib btstack_port_esp32.c.obj; " ^
    "  $b = [System.IO.File]::ReadAllBytes('btstack_port_esp32.c.obj'); " ^
    "  $b[0x314+21] = 5; $b[0x314+22] = 7; " ^
    "  [System.IO.File]::WriteAllBytes('btstack_port_esp32.c.obj', $b); " ^
    "  & $ar rcs $lib btstack_port_esp32.c.obj; " ^
    "  Pop-Location; " ^
    "}"

echo [4/5] Instalando toolkit portatil do RP2040 (rp2040:rp2040)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" core install rp2040:rp2040
if errorlevel 1 (
    echo [ERRO] Falha ao instalar o core rp2040:rp2040.
    pause
    exit /b 1
)

echo [5/5] Instalando biblioteca Adafruit NeoPixel (LED RGB do RP2040-Zero)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" lib install "Adafruit NeoPixel"

echo.
echo ============================================================================
echo  CONCLUIDO! Todos os toolkits estao instalados de forma portatil em .\tools
echo ============================================================================
if "%1"=="" pause
exit /b 0
