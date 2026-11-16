#ifndef GAMEPAD_PROTOCOL_H
#define GAMEPAD_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

// Velocidade da comunicacao UART entre ESP32 (GPIO17/16) e RP2040-Zero (GP1/GP0)
#define GAMEPAD_UART_BAUD 921600

// Numero maximo de controles simultaneos suportados
#define GP_MAX_CONTROLLERS 4

// Bytes de sincronizacao (Magic Header)
#define GP_STATE_MAGIC1    0xAA
#define GP_STATE_MAGIC2    0x55

#define GP_FEEDBACK_MAGIC1 0x55
#define GP_FEEDBACK_MAGIC2 0xAA

// Codificacao do byte 'flags' no GamepadStatePacket (ESP32 -> RP2040):
//   Bits 0..1 : Indice do controle (playerIndex: 0 a 3)
//   Bits 2..4 : Quantidade total de controles conectados no Bluetooth (0 a 4)
//   Bit  5    : GP_FLAG_CONNECTED (1 = este slot esta conectado)
//   Bit  6    : GP_FLAG_PAIRING_OPEN (1 = modo de pareamento aberto no ESP32)
//   Bit  7    : GP_FLAG_KEYS_CLEARED (1 = chaves Bluetooth recem-apagadas)
#define GP_FLAG_PLAYER_MASK      0x03
#define GP_FLAG_COUNT_SHIFT      2
#define GP_FLAG_COUNT_MASK       (0x07 << GP_FLAG_COUNT_SHIFT)
#define GP_FLAG_CONNECTED        (1 << 5)
#define GP_FLAG_PAIRING_OPEN     (1 << 6)
#define GP_FLAG_KEYS_CLEARED     (1 << 7)

static inline uint8_t gp_make_state_flags(uint8_t playerIdx, uint8_t connectedCount, bool slotConnected, bool pairingOpen, bool keysCleared) {
    uint8_t f = (playerIdx & 0x03) | ((connectedCount & 0x07) << GP_FLAG_COUNT_SHIFT);
    if (slotConnected) f |= GP_FLAG_CONNECTED;
    if (pairingOpen)   f |= GP_FLAG_PAIRING_OPEN;
    if (keysCleared)   f |= GP_FLAG_KEYS_CLEARED;
    return f;
}

static inline uint8_t gp_get_player_index(uint8_t flags) {
    return flags & GP_FLAG_PLAYER_MASK;
}

static inline uint8_t gp_get_connected_count(uint8_t flags) {
    return (flags >> GP_FLAG_COUNT_SHIFT) & 0x07;
}

// Codificacao do byte 'flags' no GamepadFeedbackPacket (RP2040 -> ESP32):
//   Bits 0..1 : Indice do controle (playerIndex: 0 a 3)
//   Bit  2    : GP_FB_FLAG_RUMBLE
//   Bit  3    : GP_FB_FLAG_PLAYER_LED
#define GP_FB_PLAYER_MASK        0x03
#define GP_FB_FLAG_RUMBLE        (1 << 2)
#define GP_FB_FLAG_PLAYER_LED    (1 << 3)

// Mascara de botoes nativa do protocolo Microsoft Xbox 360 (XInput)
#define XINPUT_BTN_DPAD_UP     0x0001
#define XINPUT_BTN_DPAD_DOWN   0x0002
#define XINPUT_BTN_DPAD_LEFT   0x0004
#define XINPUT_BTN_DPAD_RIGHT  0x0008
#define XINPUT_BTN_START       0x0010
#define XINPUT_BTN_BACK        0x0020
#define XINPUT_BTN_L3          0x0040
#define XINPUT_BTN_R3          0x0080
#define XINPUT_BTN_LB          0x0100
#define XINPUT_BTN_RB          0x0200
#define XINPUT_BTN_GUIDE       0x0400
#define XINPUT_BTN_A           0x1000
#define XINPUT_BTN_B           0x2000
#define XINPUT_BTN_X           0x4000
#define XINPUT_BTN_Y           0x8000

// Pacote de 16 bytes enviado do ESP32-WROOM para o RP2040-Zero
struct __attribute__((packed)) GamepadStatePacket {
    uint8_t  magic1;   // GP_STATE_MAGIC1 (0xAA)
    uint8_t  magic2;   // GP_STATE_MAGIC2 (0x55)
    uint8_t  flags;    // Codificado via gp_make_state_flags()
    uint16_t buttons;  // XINPUT_BTN_* bitmask
    uint8_t  lt;       // Gatilho esquerdo (0..255)
    uint8_t  rt;       // Gatilho direito (0..255)
    int16_t  lx;       // Analogico esquerdo X (-32768..32767)
    int16_t  ly;       // Analogico esquerdo Y (-32768..32767, + para cima)
    int16_t  rx;       // Analogico direito X (-32768..32767)
    int16_t  ry;       // Analogico direito Y (-32768..32767, + para cima)
    uint8_t  crc8;     // CRC-8 dos 15 bytes anteriores
};

// Pacote de 7 bytes enviado do RP2040-Zero para o ESP32-WROOM (Rumble / LED)
struct __attribute__((packed)) GamepadFeedbackPacket {
    uint8_t magic1;      // GP_FEEDBACK_MAGIC1 (0x55)
    uint8_t magic2;      // GP_FEEDBACK_MAGIC2 (0xAA)
    uint8_t flags;       // (playerIdx & 0x03) | GP_FB_FLAG_*
    uint8_t leftMotor;   // Motor de vibracao esquerdo/forte (0..255)
    uint8_t rightMotor;  // Motor de vibracao direito/fraco (0..255)
    uint8_t ledPattern;  // Padrao de LED XInput (ex: 0x06 = Player 1)
    uint8_t crc8;        // CRC-8 dos 6 bytes anteriores
};

// Calculo rapido de CRC-8 (Polinomio 0x07)
static inline uint8_t gamepad_calc_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static inline void gamepad_init_neutral_state(GamepadStatePacket* pkt, uint8_t flags) {
    pkt->magic1  = GP_STATE_MAGIC1;
    pkt->magic2  = GP_STATE_MAGIC2;
    pkt->flags   = flags;
    pkt->buttons = 0;
    pkt->lt      = 0;
    pkt->rt      = 0;
    pkt->lx      = 0;
    pkt->ly      = 0;
    pkt->rx      = 0;
    pkt->ry      = 0;
    pkt->crc8    = gamepad_calc_crc8((const uint8_t*)pkt, sizeof(GamepadStatePacket) - 1);
}

#endif // GAMEPAD_PROTOCOL_H
