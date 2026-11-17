// ============================================================================
// Firmware 2: Emulador Receptor Sem Fio Xbox 360 (045E:0719) - RP2040-Zero
// Stack USB: Adafruit TinyUSB + Protocolo Xbox 360 Wireless Receiver
// ============================================================================
//
// FUNCIONAMENTO NO PC (WINDOWS / LINUX):
//   - Ao plugar na USB sem controles Bluetooth: Nenhum controle aparece no joy.cpl.
//   - Ao conectar o 1º, 2º, 3º ou 4º controle Bluetooth no ESP32:
//     O RP2040 envia o pacote de presenca [0x08, 0x80] + Announce no respectivo
//     canal sem fio. O Windows adiciona o novo controle na hora SEM DESCONECTAR
//     ou interromper os controles que ja estavam jogando!
//   - Ao desligar qualquer controle: O RP2040 envia [0x08, 0x00] e o Windows
//     remove apenas aquele controle do joy.cpl.
//
// LED RGB INTEGRADO DO RP2040-ZERO (GP16):
//   - Amarelo piscando : Sem comunicacao UART com o ESP32 (verifique GP0/GP1)
//   - Roxo piscando    : Memoria de pareamento Bluetooth resetada
//   - Azul piscando    : Modo de pareamento ABERTO para novos controles
//   - Verde fixo       : 1 Controle Bluetooth conectado
//   - Ciano fixo       : 2 Controles Bluetooth conectados
//   - Azul/Roxo fixo   : 3 Controles Bluetooth conectados
//   - Dourado/Amarelo  : 4 Controles Bluetooth conectados
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

Adafruit_NeoPixel rgbLed(1, RP2040_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

uint8_t rxBuf[sizeof(GamepadStatePacket)];
size_t  rxIndex = 0;

GamepadStatePacket latestState[GP_MAX_CONTROLLERS];
uint32_t lastUsbSendUs[GP_MAX_CONTROLLERS] = {0};

uint8_t  connectedBtCount     = 0;
bool     pairingOpen          = true;
bool     keysCleared          = false;
bool     hasValidPacket       = false;
bool     neutralForced        = true;
uint32_t lastValidPacketMs    = 0;
uint32_t lastLedUpdateMs      = 0;

// ----------------------------------------------------------------------------
// Callback acionada quando o Windows/Linux envia Rumble ou Player LED
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
// Atualiza a presenca dos 4 slots no Receptor Sem Fio Xbox 360 (sem reset USB)
// Regra:
//   - Slots 0..3: visiveis no Windows SOMENTE quando o respectivo controle BT
//     esta conectado no ESP32 (0 controles BT -> 0 controles no joy.cpl)
// ----------------------------------------------------------------------------
void syncWirelessPresence() {
    if (!XInputUSB.mounted()) {
        return;
    }

    // Todos os slots (0..3) acompanham a quantidade real de controles conectados no ESP32
    for (uint8_t i = 0; i < GP_MAX_CONTROLLERS; i++) {
        bool shouldBePresent = hasValidPacket && (i < connectedBtCount);
        if (XInputUSB.isSlotPresent(i) != shouldBePresent) {
            if (!shouldBePresent) {
                gamepad_init_neutral_state(&latestState[i], 0);
            }
            XInputUSB.setSlotPresent(i, shouldBePresent);
        }
    }
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

                    if (connectedBtCount == 0) {
                        gamepad_init_neutral_state(&latestState[0], 0);
                    }

                    // Garante que o slot ja esteja anunciado no Receptor Wireless antes de enviar
                    syncWirelessPresence();

                    if (playerIdx < GP_MAX_CONTROLLERS) {
                        memcpy(&latestState[playerIdx], pkt, sizeof(GamepadStatePacket));

                        if (!XInputUSB.processControlPackets(playerIdx) &&
                            XInputUSB.isSlotPresent(playerIdx) &&
                            XInputUSB.ready(playerIdx)) {
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
// Atualiza o LED RGB WS2812 do RP2040-Zero (GP16)
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
        // Cor fixa indicando quantos controles estao conectados (1=Verde, 2=Ciano, 3=Roxo, 4=Dourado)
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
        // 0 controles conectados e pareamento fechado
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

    // Inicializa a USB como Xbox 360 Wireless Receiver (045E:0719)
    XInputUSB.setFeedbackCallback(onPcFeedback);
    XInputUSB.begin();
}

void loop() {
    // 1. Le pacotes de todos os controles ativos vindos do ESP32
    pollEsp32Serial();

    // 2. Sincroniza presenca dos controles na USB sem resetar o barramento
    syncWirelessPresence();

    // 2.1 Processa pacotes pendentes de presenca / anuncio (0x08 / 0x0F / 0x13) para todos os slots
    bool controlSent[GP_MAX_CONTROLLERS] = {false};
    for (uint8_t i = 0; i < GP_MAX_CONTROLLERS; i++) {
        controlSent[i] = XInputUSB.processControlPackets(i);
    }

    uint32_t nowMs = millis();
    uint32_t nowUs = micros();

    // 3. Watchdog de seguranca: se ficar sem UART por 200ms, zera o controle ativo
    if (hasValidPacket && (nowMs - lastValidPacketMs > UART_WATCHDOG_TIMEOUT_MS)) {
        if (!neutralForced) {
            bool allSent = true;
            for (uint8_t i = 0; i < GP_MAX_CONTROLLERS; i++) {
                gamepad_init_neutral_state(&latestState[i], 0);
                if (XInputUSB.isSlotPresent(i) && !controlSent[i]) {
                    if (XInputUSB.ready(i)) {
                        XInputUSB.sendNeutral(i);
                    } else {
                        allSent = false;
                    }
                }
            }
            if (allSent) {
                neutralForced = true;
            }
        }
    } else if (hasValidPacket) {
        // Envia periodicamente a 1000 Hz apenas para os slots atualmente presentes
        for (uint8_t i = 0; i < GP_MAX_CONTROLLERS; i++) {
            if (XInputUSB.isSlotPresent(i) && !controlSent[i] && XInputUSB.ready(i) && (uint32_t)(nowUs - lastUsbSendUs[i]) >= 1000) {
                XInputUSB.sendFromGamepadPacket(i, &latestState[i]);
                lastUsbSendUs[i] = nowUs;
            }
        }
    }

    // 4. Atualiza LED RGB de status
    updateStatusRgbLed();
}
