#ifndef USB_XINPUT_H
#define USB_XINPUT_H

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "GamepadProtocol.h"

// Relatorio oficial de entrada USB de 20 bytes do Controle Microsoft Xbox 360
struct __attribute__((packed)) XInputReport20 {
    uint8_t  reportId;      // Sempre 0x00
    uint8_t  reportSize;    // Sempre 0x14 (20 bytes)
    uint16_t buttons;       // Mascara XINPUT_BTN_*
    uint8_t  leftTrigger;   // 0..255
    uint8_t  rightTrigger;  // 0..255
    int16_t  thumbLX;       // -32768..32767
    int16_t  thumbLY;       // -32768..32767
    int16_t  thumbRX;       // -32768..32767
    int16_t  thumbRY;       // -32768..32767
    uint8_t  reserved[6];   // Sempre zeros
};

typedef void (*XInputFeedbackCallback)(uint8_t playerIdx, uint8_t flags, uint8_t leftMotor, uint8_t rightMotor, uint8_t ledPattern);

// Representa uma interface individual de Controle Xbox 360 (Slot 0..3)
class USBXInputInterface : public Adafruit_USBD_Interface {
public:
    USBXInputInterface();
    virtual uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t* buf, uint16_t bufsize) override;
};

// Gerenciador Dinamico de 1 a 4 Controles Xbox 360 na USB
class USBXInputManager {
public:
    USBXInputManager();

    // Inicializa com 1 controle visivel no PC (0 ou 1 controles BT conectados)
    bool begin(uint8_t initialControllerCount = 1);

    // Altera dinamicamente quantos controles aparecem no PC (1 a 4)
    // Regra: 0 ou 1 controles BT -> 1 controle USB; 2..4 controles BT -> 2..4 controles USB
    bool setControllerCount(uint8_t count);

    // Retorna quantos controles USB estao expostos no momento (1 a 4)
    uint8_t getControllerCount() const { return _activeCount; }

    // Verifica se o endpoint IN de um jogador especifico (0..3) esta pronto para envio
    bool ready(uint8_t playerIdx);

    // Envia relatorio de 20 bytes para o jogador (0..3)
    bool sendReport(uint8_t playerIdx, const XInputReport20* report);

    // Envia pacote recebido do ESP32 para o jogador correspondente
    bool sendFromGamepadPacket(uint8_t playerIdx, const GamepadStatePacket* pkt);

    // Envia estado neutro para o jogador (0..3)
    bool sendNeutral(uint8_t playerIdx);

    // Define callback para receber Rumble e LED de qualquer um dos 4 controles
    void setFeedbackCallback(XInputFeedbackCallback cb);

private:
    uint8_t _activeCount;
    USBXInputInterface _interfaces[GP_MAX_CONTROLLERS];
};

extern USBXInputManager XInputUSB;

#endif // USB_XINPUT_H
