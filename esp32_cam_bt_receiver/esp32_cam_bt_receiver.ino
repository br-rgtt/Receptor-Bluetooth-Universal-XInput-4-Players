// ============================================================================
// Firmware 1: Receptor Bluetooth Universal para ate 4 Controles (ESP32-CAM)
// Framework: Bluepad32 (Suporta BT Classico BR/EDR + BLE simultaneamente)
// Comunicacao: UART2 (GPIO15 TX -> GP1 RX / GPIO14 RX -> GP0 TX) a 921600 bps
// ============================================================================

#include <Arduino.h>
#include <Bluepad32.h>

extern "C" {
#include <btstack.h>
#include <uni.h>
}

#include "GamepadProtocol.h"

// ----------------------------------------------------------------------------
// Mapeamento de Pinos para ESP32-CAM (AI-Thinker)
// Nota: Os pinos GPIO 16 e 17 sao usados internamente pela PSRAM na ESP32-CAM
// e nao podem ser utilizados para UART. Usamos GPIO 14 (RX) e GPIO 15 (TX).
// ----------------------------------------------------------------------------
#define UART_RP2040_RX_PIN  14  // Conectar ao GP0 (TX) do RP2040-Zero
#define UART_RP2040_TX_PIN  15  // Conectar ao GP1 (RX) do RP2040-Zero

// Botao de Pareamento / Reset de Chaves
// No ESP32-CAM conecte um push-button entre o GPIO 13 e GND (INPUT_PULLUP).
// Se estiver usando o shield USB (ESP32-CAM-MB), tambem pode alterar para GPIO 0.
#define PIN_BOOT_BUTTON     13

// LED de Status integrado na placa ESP32-CAM (LED vermelho traseiro no GPIO 33)
// ATENCAO: O LED no GPIO 33 possui logica invertida (LOW = Aceso, HIGH = Apagado).
#define PIN_STATUS_LED      33
#define LED_ON              LOW
#define LED_OFF             HIGH

// Zona morta (deadzone) nos analogicos (escala Bluepad32: -511 a +512)
#define STICK_DEADZONE      12

// Intervalo de envio de pacotes para o RP2040 (2 ms = 500 Hz por ciclo)
#define STATE_SEND_INTERVAL_US 2000

ControllerPtr myControllers[GP_MAX_CONTROLLERS];

bool pairingEnabled = false;
uint32_t keysClearedUntilMs = 0;
uint32_t lastSendUs = 0;
uint32_t lastScanKickMs = 0;

// Estado do botao BOOT / Pareamento
bool bootBtnPressed = false;
bool bootLongPressHandled = false;
uint32_t bootBtnDownMs = 0;

// Buffer de recepcao de Feedback (Rumble / Player LED) vindo do RP2040
uint8_t fbRxBuf[sizeof(GamepadFeedbackPacket)];
size_t fbRxIndex = 0;

// Cache de Rumble individual para cada um dos 4 controles
uint8_t  lastLeftMotor[GP_MAX_CONTROLLERS]    = {0};
uint8_t  lastRightMotor[GP_MAX_CONTROLLERS]   = {0};
uint32_t lastRumbleSentMs[GP_MAX_CONTROLLERS] = {0};

// Registro de callback para executar comandos de scan na thread do BTstack
static btstack_context_callback_registration_t s_scanCallbackReg;
static btstack_context_callback_registration_t s_stopScanCallbackReg;

// Cores de identificacao para controles com barra de luz RGB (PS4 / PS5)
struct RgbColor { uint8_t r, g, b; };
static const RgbColor kPlayerColors[GP_MAX_CONTROLLERS] = {
    {  0, 120, 255}, // Player 1: Azul
    {255,  30,   0}, // Player 2: Vermelho
    {  0, 220,  40}, // Player 3: Verde
    {200,   0, 255}  // Player 4: Roxo
};

static inline void setStatusLed(bool on) {
    digitalWrite(PIN_STATUS_LED, on ? LED_ON : LED_OFF);
}

// ----------------------------------------------------------------------------
// Executa na thread principal do BTstack para iniciar/renovar o Inquiry Scan
// ----------------------------------------------------------------------------
static void forceBtScanOnMainThread(void* context) {
    (void)context;
    uni_bt_enable_new_connections_unsafe(true);
    gap_connectable_control(1);
    gap_discoverable_control(1);

    uint8_t inqLen = uni_bt_get_gap_inquiry_length();
    uint8_t maxPer = uni_bt_get_gap_max_periodic_length();
    uint8_t minPer = uni_bt_get_gap_min_periodic_length();
    gap_inquiry_periodic_start(inqLen, maxPer, minPer);
    uni_bt_le_scan_start();
}

static void stopBtScanOnMainThread(void* context) {
    (void)context;
    gap_inquiry_stop();
    uni_bt_le_scan_stop();
    gap_discoverable_control(0);
    gap_connectable_control(1); // Mantem reconexao automatica de controles ja pareados
    uni_bt_enable_new_connections_unsafe(false);
}

void kickBluetoothScan() {
    s_scanCallbackReg.callback = &forceBtScanOnMainThread;
    s_scanCallbackReg.context  = nullptr;
    btstack_run_loop_execute_on_main_thread(&s_scanCallbackReg);
    lastScanKickMs = millis();
}

void stopBluetoothScan() {
    s_stopScanCallbackReg.callback = &stopBtScanOnMainThread;
    s_stopScanCallbackReg.context  = nullptr;
    btstack_run_loop_execute_on_main_thread(&s_stopScanCallbackReg);
}

void setPairingMode(bool enable) {
    pairingEnabled = enable;
    if (enable) {
        kickBluetoothScan();
    } else {
        stopBluetoothScan();
    }
}

uint8_t getActiveControllers(ControllerPtr outList[GP_MAX_CONTROLLERS]) {
    uint8_t count = 0;
    for (int i = 0; i < GP_MAX_CONTROLLERS; i++) {
        ControllerPtr ctl = myControllers[i];
        if (ctl && ctl->isConnected() && ctl->isGamepad()) {
            if (outList != nullptr) {
                outList[count] = ctl;
            }
            count++;
        }
    }
    return count;
}

void refreshPlayerLeds() {
    ControllerPtr activeList[GP_MAX_CONTROLLERS] = {nullptr};
    uint8_t count = getActiveControllers(activeList);
    for (uint8_t idx = 0; idx < count; idx++) {
        ControllerPtr ctl = activeList[idx];
        ctl->setPlayerLEDs(1 << idx);
        ctl->setColorLED(kPlayerColors[idx].r, kPlayerColors[idx].g, kPlayerColors[idx].b);
    }
}

void onConnectedController(ControllerPtr ctl) {
    for (int i = 0; i < GP_MAX_CONTROLLERS; i++) {
        if (myControllers[i] == nullptr) {
            myControllers[i] = ctl;

            refreshPlayerLeds();
            ctl->playDualRumble(0, 150, 0x80, 0x40);

            setPairingMode(false);
            return;
        }
    }
}

void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < GP_MAX_CONTROLLERS; i++) {
        if (myControllers[i] == ctl) {
            myControllers[i] = ctl = nullptr;
            lastLeftMotor[i] = 0;
            lastRightMotor[i] = 0;
            refreshPlayerLeds();
            break;
        }
    }
}

static inline int16_t scaleStickAxis(int32_t rawVal, bool invertAxis) {
    if (invertAxis) {
        rawVal = -rawVal;
    }

    if (rawVal > -STICK_DEADZONE && rawVal < STICK_DEADZONE) {
        return 0;
    }

    int32_t scaled = rawVal * 64;
    if (scaled > 32767)  scaled = 32767;
    if (scaled < -32768) scaled = -32768;
    return (int16_t)scaled;
}

static inline uint8_t scaleTrigger(int32_t analogBrakeOrThrottle, bool digitalBtnPressed) {
    if (analogBrakeOrThrottle > 0) {
        int32_t v = analogBrakeOrThrottle >> 2;
        if (v > 255) v = 255;
        return (uint8_t)v;
    }
    return digitalBtnPressed ? 255 : 0;
}

void buildControllerPacket(GamepadStatePacket* pkt, ControllerPtr ctl, uint8_t playerIdx, uint8_t connectedCount) {
    bool keysCleared = (millis() < keysClearedUntilMs);
    bool slotConnected = (ctl != nullptr);
    uint8_t flags = gp_make_state_flags(playerIdx, connectedCount, slotConnected, pairingEnabled, keysCleared);

    if (!ctl) {
        gamepad_init_neutral_state(pkt, flags);
        return;
    }

    uint16_t btns = 0;
    uint8_t dpad = ctl->dpad();
    if (dpad & DPAD_UP)    btns |= XINPUT_BTN_DPAD_UP;
    if (dpad & DPAD_DOWN)  btns |= XINPUT_BTN_DPAD_DOWN;
    if (dpad & DPAD_LEFT)  btns |= XINPUT_BTN_DPAD_LEFT;
    if (dpad & DPAD_RIGHT) btns |= XINPUT_BTN_DPAD_RIGHT;

    if (ctl->a())      btns |= XINPUT_BTN_A;
    if (ctl->b())      btns |= XINPUT_BTN_B;
    if (ctl->x())      btns |= XINPUT_BTN_X;
    if (ctl->y())      btns |= XINPUT_BTN_Y;

    if (ctl->l1())     btns |= XINPUT_BTN_LB;
    if (ctl->r1())     btns |= XINPUT_BTN_RB;
    if (ctl->thumbL()) btns |= XINPUT_BTN_L3;
    if (ctl->thumbR()) btns |= XINPUT_BTN_R3;

    if (ctl->miscStart())                        btns |= XINPUT_BTN_START;
    if (ctl->miscSelect() || ctl->miscCapture()) btns |= XINPUT_BTN_BACK;
    if (ctl->miscSystem())                       btns |= XINPUT_BTN_GUIDE;

    pkt->magic1  = GP_STATE_MAGIC1;
    pkt->magic2  = GP_STATE_MAGIC2;
    pkt->flags   = flags;
    pkt->buttons = btns;
    pkt->lt      = scaleTrigger(ctl->brake(), ctl->l2());
    pkt->rt      = scaleTrigger(ctl->throttle(), ctl->r2());
    pkt->lx      = scaleStickAxis(ctl->axisX(),  false);
    pkt->ly      = scaleStickAxis(ctl->axisY(),  true);
    pkt->rx      = scaleStickAxis(ctl->axisRX(), false);
    pkt->ry      = scaleStickAxis(ctl->axisRY(), true);
    pkt->crc8    = gamepad_calc_crc8((const uint8_t*)pkt, sizeof(GamepadStatePacket) - 1);
}

void applyFeedbackFromPC(const GamepadFeedbackPacket* fb) {
    ControllerPtr activeList[GP_MAX_CONTROLLERS] = {nullptr};
    uint8_t count = getActiveControllers(activeList);

    uint8_t playerIdx = fb->flags & GP_FB_PLAYER_MASK;
    if (playerIdx >= count || activeList[playerIdx] == nullptr) {
        return;
    }

    ControllerPtr ctl = activeList[playerIdx];

    if (fb->flags & GP_FB_FLAG_RUMBLE) {
        uint32_t now = millis();
        bool changed = (fb->leftMotor != lastLeftMotor[playerIdx]) ||
                       (fb->rightMotor != lastRightMotor[playerIdx]);
        if (changed || ((fb->leftMotor > 0 || fb->rightMotor > 0) && (now - lastRumbleSentMs[playerIdx] > 150))) {
            lastLeftMotor[playerIdx]    = fb->leftMotor;
            lastRightMotor[playerIdx]   = fb->rightMotor;
            lastRumbleSentMs[playerIdx] = now;

            if (fb->leftMotor == 0 && fb->rightMotor == 0) {
                ctl->playDualRumble(0, 0, 0, 0);
            } else {
                ctl->playDualRumble(0, 250, fb->rightMotor, fb->leftMotor);
            }
        }
    }

    if (fb->flags & GP_FB_FLAG_PLAYER_LED) {
        ctl->setPlayerLEDs(1 << playerIdx);
    }
}

void pollFeedbackSerial() {
    while (Serial2.available() > 0) {
        uint8_t b = (uint8_t)Serial2.read();

        if (fbRxIndex == 0) {
            if (b == GP_FEEDBACK_MAGIC1) {
                fbRxBuf[fbRxIndex++] = b;
            }
        } else if (fbRxIndex == 1) {
            if (b == GP_FEEDBACK_MAGIC2) {
                fbRxBuf[fbRxIndex++] = b;
            } else if (b == GP_FEEDBACK_MAGIC1) {
                fbRxBuf[0] = b;
                fbRxIndex = 1;
            } else {
                fbRxIndex = 0;
            }
        } else {
            fbRxBuf[fbRxIndex++] = b;
            if (fbRxIndex == sizeof(GamepadFeedbackPacket)) {
                const GamepadFeedbackPacket* fb = (const GamepadFeedbackPacket*)fbRxBuf;
                uint8_t expectedCrc = gamepad_calc_crc8(fbRxBuf, sizeof(GamepadFeedbackPacket) - 1);
                if (fb->crc8 == expectedCrc) {
                    applyFeedbackFromPC(fb);
                }
                fbRxIndex = 0;
            }
        }
    }
}

void handleBootButtonAndLed() {
    uint32_t now = millis();
    bool isDown = (digitalRead(PIN_BOOT_BUTTON) == LOW);

    if (isDown && !bootBtnPressed) {
        bootBtnPressed = true;
        bootLongPressHandled = false;
        bootBtnDownMs = now;
    } else if (isDown && bootBtnPressed) {
        if (!bootLongPressHandled && (now - bootBtnDownMs >= 3000)) {
            bootLongPressHandled = true;
            for (int i = 0; i < GP_MAX_CONTROLLERS; i++) {
                if (myControllers[i] && myControllers[i]->isConnected()) {
                    myControllers[i]->disconnect();
                }
                myControllers[i] = nullptr;
            }
            BP32.forgetBluetoothKeys();
            setPairingMode(true);
            keysClearedUntilMs = now + 2000;
        }
    } else if (!isDown && bootBtnPressed) {
        uint32_t heldMs = now - bootBtnDownMs;
        if (!bootLongPressHandled && (heldMs >= 30)) {
            setPairingMode(!pairingEnabled);
        }
        bootBtnPressed = false;
    }

    if (pairingEnabled && (now - lastScanKickMs >= 4000)) {
        kickBluetoothScan();
    }

    uint8_t connectedCount = getActiveControllers(nullptr);
    if (now < keysClearedUntilMs) {
        // Pisca super rapido para indicar esquecimento de chaves
        setStatusLed(((now / 80) % 2) != 0);
    } else if (pairingEnabled) {
        // Pisca moderado para indicar modo de pareamento aberto
        setStatusLed(((now / 350) % 2) != 0);
    } else if (connectedCount > 0) {
        // Aceso constante quando ha pelo menos 1 controle conectado
        setStatusLed(true);
    } else {
        // Apagado quando em espera / standby
        setStatusLed(false);
    }
}

void setup() {
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
    pinMode(PIN_STATUS_LED, OUTPUT);
    setStatusLed(false);

    Serial2.begin(GAMEPAD_UART_BAUD, SERIAL_8N1, UART_RP2040_RX_PIN, UART_RP2040_TX_PIN);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.enableVirtualDevice(false);

    setPairingMode(false);
}

void loop() {
    bool updated = BP32.update();

    pollFeedbackSerial();
    handleBootButtonAndLed();

    uint32_t nowUs = micros();
    if (updated || (uint32_t)(nowUs - lastSendUs) >= STATE_SEND_INTERVAL_US) {
        lastSendUs = nowUs;

        ControllerPtr activeList[GP_MAX_CONTROLLERS] = {nullptr};
        uint8_t connectedCount = getActiveControllers(activeList);

        if (connectedCount == 0) {
            GamepadStatePacket pkt;
            gamepad_init_neutral_state(&pkt, gp_make_state_flags(0, 0, false, pairingEnabled, (millis() < keysClearedUntilMs)));
            Serial2.write((const uint8_t*)&pkt, sizeof(pkt));
        } else {
            for (uint8_t idx = 0; idx < connectedCount; idx++) {
                GamepadStatePacket pkt;
                buildControllerPacket(&pkt, activeList[idx], idx, connectedCount);
                Serial2.write((const uint8_t*)&pkt, sizeof(pkt));
            }
        }
    }

    delayMicroseconds(200);
}
