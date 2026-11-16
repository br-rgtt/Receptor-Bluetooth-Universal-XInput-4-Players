@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

echo ============================================================================
echo  GRAVACAO DO FIRMWARE NO RP2040-ZERO (Emulador USB Xbox 360 / XInput)
echo ============================================================================

set "ROOT_DIR=%~dp0"
set "TOOLS_DIR=%ROOT_DIR%tools"
set "ARDUINO_CLI=%TOOLS_DIR%\bin\arduino-cli.exe"
set "ARDUINO_CONFIG=%TOOLS_DIR%\arduino-cli.yaml"
set "BUILD_DIR=%ROOT_DIR%build\rp2040"
set "UF2_FILE=%BUILD_DIR%\rp2040_xinput_usb.ino.uf2"

:: Forca modo 100%% portatil
set "ARDUINO_DIRECTORIES_DATA=%TOOLS_DIR%\data"
set "ARDUINO_DIRECTORIES_DOWNLOADS=%TOOLS_DIR%\staging"
set "ARDUINO_DIRECTORIES_USER=%TOOLS_DIR%\sketchbook"

if not exist "%ARDUINO_CLI%" (
    echo [AVISO] Toolkit portatil nao encontrado. Executando setup_tools.bat...
    call "%ROOT_DIR%setup_tools.bat" nopause
    if errorlevel 1 exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo.
echo [1/2] Compilando rp2040_xinput_usb (TinyUSB XInput)...
"%ARDUINO_CLI%" --config-file "%ARDUINO_CONFIG%" compile ^
    --fqbn rp2040:rp2040:waveshare_rp2040_zero:usbstack=tinyusb ^
    --output-dir "%BUILD_DIR%" ^
    "%ROOT_DIR%rp2040_xinput_usb"

if errorlevel 1 (
    echo [ERRO] Falha na compilacao do RP2040-Zero!
    pause
    exit /b 1
)

echo.
echo [2/2] Procurando o RP2040-Zero em modo BOOTSEL (Unidade USB RPI-RP2)...
echo       Como colocar o RP2040-Zero em modo de gravacao (BOOTSEL):
echo         1. Segure o botao [BOOT] no RP2040-Zero
echo         2. Com o [BOOT] segurado, conecte na USB (ou de um clique no botao [RESET])
echo         3. Solte o botao [BOOT]. O Windows abrira a unidade "RPI-RP2".
echo.
echo       Aguardando unidade RPI-RP2 aparecer... (Pressione Ctrl+C para cancelar)

:wait_uf2
set "RP2040_DRIVE="
for %%D in (D E F G H I J K L M N O P Q R S T U V W X Y Z) do (
    if exist "%%D:\INFO_UF2.TXT" (
        set "RP2040_DRIVE=%%D:"
    )
)

if "%RP2040_DRIVE%"=="" (
    timeout /t 1 /nobreak >nul
    goto wait_uf2
)

echo.
echo [INFO] RP2040-Zero detectado na unidade %RP2040_DRIVE%\ !
echo [INFO] Gravando "%UF2_FILE%" em %RP2040_DRIVE%\ ...
copy /Y "%UF2_FILE%" "%RP2040_DRIVE%\" >nul
if errorlevel 1 (
    echo [ERRO] Falha ao copiar o arquivo .uf2 para %RP2040_DRIVE%\
    pause
    exit /b 1
)

echo.
echo ============================================================================
echo  SUCESSO! O RP2040-Zero foi gravado e ja reiniciou como Controle Xbox 360!
echo ============================================================================
pause
exit /b 0
