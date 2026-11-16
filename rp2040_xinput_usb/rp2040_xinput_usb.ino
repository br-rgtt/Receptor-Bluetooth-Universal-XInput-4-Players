// ============================================================================
// Firmware 2: Interface USB Xbox 360 Dinamica (1 a 4 Controles) - RP2040-Zero
// Stack USB: Adafruit TinyUSB + Driver XInput + MS OS 2.0 (XUSB10)
// ============================================================================
//
// COMPORTAMENTO DINAMICO NA USB:
//   - 0 ou 1 controle conectado no Bluetooth -> Aparece 1 Controle Xbox 360 no PC
//   - 2 controles conectados no Bluetooth    -> Aparecem 2 Controles Xbox 360 no PC
//   - 3 controles conectados no Bluetooth    -> Aparecem 3 Controles Xbox 360 no PC
//   - 4 controles conectados no Bluetooth    -> Aparecem 4 Controles Xbox 360 no PC
//
// LED RGB INTEGRADO DO RP2040-ZERO (GP16):
//   - Amarelo piscando : Sem sinal UART do ESP32 (verifique os fios TX/RX)
//   - Azul piscando    : Modo de Pareamento ABERTO (mesmo com controles ja conectados!)
//   - Verde fixo       : 1 Controle conectado
//   - Ciano fixo       : 2 Controles conectados
//   - Azul/Roxo fixo   : 3 Controles conectados
//   - Branco/Ouro fixo : 4 Controles conectados
//   - Roxo piscando    : Memoria de pareamento Bluetooth limpa (Reset BT)
// ============================================================================

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include "GamepadProtocol.h"
#include "usb_xinput.h"

// Pinos UART0 do RP2040-Zero conectados ao ESP32-WROOM
#define RP2040_UART_TX_PIN  0   // GP0 -> Conectar ao GPIO 16 (RX2) do ESP32
#define RP2040_UART_RX_PIN  1   // GP1 -> Conectar ao GPIO 17 (TX2) do ESP32

// Pino do LED RGB WS2812 integrado no Waveshare RP2040-Zero
#define RP2040_NEOPIXEL_PIN 16

// Timeout de seguranca: se ficar 200ms sem pacote do ESP32, zera os controles no PC
#define UART_WATCHDOG_TIMEOUT_MS 200

// Tempo de estabilizacao antes de alterar a quantidade de dispositivos USB (300 ms)
#define USB_COUNT_DEBOUNCE_MS    300

Adafruit_NeoPixel rgbLed(1, RP2040_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

uint8_t rxBuf[sizeof(GamepadStatePacket)];
size_t  rxIndex = 0;

GamepadStatePacket latestState[GP_MAX_CONTROLLERS];
uint32_t lastUsbSendUs[GP_MAX_CONTROLLERS] = {0};

uint8_t  connectedBtCount   = 0;
bool     pairingOpen        = true;
bool     keysCleared        = false;
bool     hasValidPacket     = false;
bool     neutralForced      = true;
uint32_t lastValidPacketMs  = 0;
uint32_t lastLedUpdateMs    = 0;

uint8_t  pendingUsbCount    = 1;
uint32_t pendingCountSinceMs = 0;

// ----------------------------------------------------------------------------
// Callback acionada quando o PC (Windows/Linux) envia Rumble ou Player LED
// para qualquer um dos 4 controles (playerIdx: 0..3)
// ----------------------------------------------------------------------------
void onPcFeedback(uint8_t playerIdx, uint8_t flags, uint8_t leftMotor, uint8_t rightMotor, uint8_t ledPattern) {
    GamepadFeedbackPacket fb;
    fb.magic1     = GP_FEEDBACK_MAGIC1;
    fb.magic2     = GP_FEEDBACK_MAGIC2;
    fb.flags      = (playerIdx & GP_FB_PLAYER_MASK) | flags;
    fb.leftMotor  = leftMotor;
    fb.rightMotor = rightMotor;
    fb.ledPattern = ledPattern;
    fb.crc8       = gamepad_calc_crc8((const uint8_t*)&fb, sizeof(GamepadFeedbackPacket) - 1);

    Serial1.write((const uint8_t*)&fb, sizeof(GamepadFeedbackPacket));
}

// ----------------------------------------------------------------------------
// Le pacotes binarios de 16 bytes vindos do ESP32 pela UART0 (Serial1)
// ----------------------------------------------------------------------------
void pollEsp32Serial() {
    while (Serial1.available() > 0) {
        uint8_t b = (uint8_t)Serial1.read();

        if (rxIndex == 0) {
            if (b == GP_STATE_MAGIC1) {
                rxBuf[rxIndex++] = b;
            }
        } else if (rxIndex == 1) {
            if (b == GP_STATE_MAGIC2) {
                rxBuf[rxIndex++] = b;
            } else if (b == GP_STATE_MAGIC1) {
                rxBuf[0] = b;
                rxIndex = 1;
            } else {
                rxIndex = 0;
            }
        } else {
            rxBuf[rxIndex++] = b;
            if (rxIndex == sizeof(GamepadStatePacket)) {
                const GamepadStatePacket* pkt = (const GamepadStatePacket*)rxBuf;
                uint8_t expectedCrc = gamepad_calc_crc8(rxBuf, sizeof(GamepadStatePacket) - 1);

                if (pkt->crc8 == expectedCrc) {
                    uint8_t playerIdx = gp_get_player_index(pkt->flags);
                    uint8_t btCount   = gp_get_connected_count(pkt->flags);
                    if (btCount > GP_MAX_CONTROLLERS) btCount = GP_MAX_CONTROLLERS;

                    connectedBtCount  = btCount;
                    pairingOpen       = (pkt->flags & GP_FLAG_PAIRING_OPEN) != 0;
                    keysCleared       = (pkt->flags & GP_FLAG_KEYS_CLEARED) != 0;
                    hasValidPacket    = true;
                    neutralForced     = false;
                    lastValidPacketMs = millis();

                    if (playerIdx < GP_MAX_CONTROLLERS) {
                        memcpy(&latestState[playerIdx], pkt, sizeof(GamepadStatePacket));

                        // Limpa slots acima de connectedBtCount caso algum controle tenha desconectado
                        for (uint8_t i = (connectedBtCount == 0 ? 1 : connectedBtCount); i < GP_MAX_CONTROLLERS; i++) {
                            gamepad_init_neutral_state(&latestState[i], 0);
                        }

                        // Envia imediatamente para o PC se o endpoint deste jogador estiver livre
                        if (XInputUSB.ready(playerIdx)) {
                            XInputUSB.sendFromGamepadPacket(playerIdx, &latestState[playerIdx]);
                            lastUsbSendUs[playerIdx] = micros();
                        }
                    }
                }
                rxIndex = 0;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Gerencia a adicao/remocao dinamica de controles na USB (1 a 4 dispositivos)
// Regra: 0 ou 1 controle BT -> 1 dispositivo USB; 2..4 controles BT -> 2..4 USB
// ----------------------------------------------------------------------------
void manageDynamicUsbControllers() {
    if (!hasValidPacket) return;

    uint8_t desiredUsbCount = (connectedBtCount <= 1) ? 1 : connectedBtCount;
    if (desiredUsbCount != pendingUsbCount) {
        pendingUsbCount     = desiredUsbCount;
        pendingCountSinceMs = millis();
    }

    if (pendingUsbCount != XInputUSB.getControllerCount()) {
        if (millis() - pendingCountSinceMs >= USB_COUNT_DEBOUNCE_MS) {
            XInputUSB.setControllerCount(pendingUsbCount);
        }
    }
}

// ----------------------------------------------------------------------------
// Atualiza o LED RGB WS2812 do RP2040-Zero (GP16)
// Prioridade:
//   1. Sem UART -> Amarelo piscando
//   2. Reset BT -> Roxo piscando rapido
//   3. Modo Pareamento ABERTO -> Azul piscando (mesmo com controles conectados!)
//   4. Controles Conectados (1..4) -> Cor fixa por quantidade de controles
// ----------------------------------------------------------------------------
void updateStatusRgbLed() {
    uint32_t now = millis();
    if (now - lastLedUpdateMs < 25) {
        return;
    }
    lastLedUpdateMs = now;

    bool uartAlive = hasValidPacket && ((now - lastValidPacketMs) <= UART_WATCHDOG_TIMEOUT_MS);

    if (!uartAlive) {
        // Amarelo piscando: Aguardando comunicacao UART com o ESP32
        bool on = ((now / 350) % 2) == 0;
        rgbLed.setPixelColor(0, on ? rgbLed.Color(35, 20, 0) : 0);
    } else if (keysCleared) {
        // Roxo piscando rapido: Chaves Bluetooth resetadas
        bool on = ((now / 80) % 2) == 0;
        rgbLed.setPixelColor(0, on ? rgbLed.Color(45, 0, 45) : 0);
    } else if (pairingOpen) {
        // Azul piscando: Modo de pareamento ABERTO procurando novo controle!
        bool on = ((now / 350) % 2) == 0;
        rgbLed.setPixelColor(0, on ? rgbLed.Color(0, 15, 50) : 0);
    } else if (connectedBtCount >= 1) {
        // Cor fixa indicando quantos controles estao conectados (1=Verde, 2=Ciano, 3=Roxo, 4=Ouro)
        if (connectedBtCount == 1) {
            rgbLed.setPixelColor(0, rgbLed.Color(0, 35, 0));
        } else if (connectedBtCount == 2) {
            rgbLed.setPixelColor(0, rgbLed.Color(0, 30, 35));
        } else if (connectedBtCount == 3) {
            rgbLed.setPixelColor(0, rgbLed.Color(25, 0, 40));
        } else {
            rgbLed.setPixelColor(0, rgbLed.Color(35, 25, 0));
        }
    } else {
        // 0 controles conectados e modo de pareamento fechado (clique no BOOT para abrir)
        rgbLed.setPixelColor(0, rgbLed.Color(15, 0, 0));
    }

    rgbLed.show();
}

// ----------------------------------------------------------------------------
// Setup & Loop Principal
// ----------------------------------------------------------------------------
void setup() {
    for (uint8_t i = 0; i < GP_MAX_CONTROLLERS; i++) {
        gamepad_init_neutral_state(&latestState[i], 0);
    }

    rgbLed.begin();
    rgbLed.setBrightness(80);
    rgbLed.setPixelColor(0, rgbLed.Color(0, 0, 30));
    rgbLed.show();

    // Configura UART0 em hardware (GP0 = TX, GP1 = RX) a 921600 bps
    Serial1.setTX(RP2040_UART_TX_PIN);
    Serial1.setRX(RP2040_UART_RX_PIN);
    Serial1.setFIFOSize(256);
    Serial1.begin(GAMEPAD_UART_BAUD);

    // Inicia com 1 controle Xbox 360 visivel na USB (mesmo com 0 controles conectados)
    XInputUSB.setFeedbackCallback(onPcFeedback);
    XInputUSB.begin(1);
}

void loop() {
    // 1. Le pacotes de todos os controles ativos vindos do ESP32
    pollEsp32Serial();

    // 2. Verifica se precisa adicionar/remover controles na USB (1 a 4)
    manageDynamicUsbControllers();

    uint32_t nowMs = millis();
    uint32_t nowUs = micros();
    uint8_t usbCount = XInputUSB.getControllerCount();

    // 3. Watchdog de seguranca ou polling USB a 1000 Hz para cada controle ativo
    if (hasValidPacket && (nowMs - lastValidPacketMs > UART_WATCHDOG_TIMEOUT_MS)) {
        if (!neutralForced) {
            bool allSent = true;
            for (uint8_t i = 0; i < usbCount; i++) {
                gamepad_init_neutral_state(&latestState[i], 0);
                if (XInputUSB.ready(i)) {
                    XInputUSB.sendNeutral(i);
                } else {
                    allSent = false;
                }
            }
            if (allSent) {
                neutralForced = true;
            }
        }
    } else if (hasValidPacket) {
        for (uint8_t i = 0; i < usbCount; i++) {
            if (XInputUSB.ready(i) && (uint32_t)(nowUs - lastUsbSendUs[i]) >= 1000) {
                XInputUSB.sendFromGamepadPacket(i, &latestState[i]);
                lastUsbSendUs[i] = nowUs;
            }
        }
    }

    // 4. Atualiza LED RGB de status
    updateStatusRgbLed();
}
