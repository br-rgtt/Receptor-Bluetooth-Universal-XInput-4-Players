#ifndef USB_XINPUT_H
#define USB_XINPUT_H

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "GamepadProtocol.h"

// Relatorio de entrada sem fio (29 bytes) do Receptor Xbox 360 Wireless (045E:0719)
// Bytes 0..3: Cabecalho Wireless [0x00, 0x01, 0x00, 0xF0]
// Bytes 4..23: Payload XInput padrao de 20 bytes
// Bytes 24..28: Reservado (zeros)
struct __attribute__((packed)) XInputWirelessReport29 {
    uint8_t  header[4];     // [0x00, 0x01, 0x00, 0xF0]
    uint8_t  reportId;      // Sempre 0x00
    uint8_t  reportSize;    // Sempre 0x14 (20 bytes)
    uint16_t buttons;       // Mascara XINPUT_BTN_*
    uint8_t  leftTrigger;   // 0..255
    uint8_t  rightTrigger;  // 0..255
    int16_t  thumbLX;       // -32768..32767
    int16_t  thumbLY;       // -32768..32767
    int16_t  thumbRX;       // -32768..32767
    int16_t  thumbRY;       // -32768..32767
    uint8_t  reserved[5];   // Sempre zeros
};

typedef void (*XInputFeedbackCallback)(uint8_t playerIdx, uint8_t flags, uint8_t leftMotor, uint8_t rightMotor, uint8_t ledPattern);

// Interface de Dados de Controle Sem Fio (bInterfaceProtocol = 0x81)
class USBXInputWirelessDataInterface : public Adafruit_USBD_Interface {
public:
    USBXInputWirelessDataInterface();
    virtual uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t* buf, uint16_t bufsize) override;
};

// Interface de Audio/Headset de Controle Sem Fio (bInterfaceProtocol = 0x82)
class USBXInputWirelessAudioInterface : public Adafruit_USBD_Interface {
public:
    USBXInputWirelessAudioInterface();
    virtual uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t* buf, uint16_t bufsize) override;
};

// Emulador do Receptor Sem Fio Xbox 360 para Windows/Linux (VID 0x045E / PID 0x0719)
// Permite conectar e desconectar controles 2, 3 e 4 dinamicamente no Windows
// via pacotes de presenca [0x08, 0x80] / [0x08, 0x00], SEM resetar a USB!
class USBXInputManager {
public:
    USBXInputManager();

    bool begin();

    // Verifica se a USB ja foi montada pelo PC
    bool mounted() const { return tud_mounted(); }

    // Define se o slot (0..3) esta presente/visivel no Windows/Linux
    // Agenda envio de [0x08, 0x80] + pacote 0x0F Announce (ou [0x08, 0x00] para desconectar)
    bool setSlotPresent(uint8_t playerIdx, bool present);
    bool isSlotPresent(uint8_t playerIdx) const;

    // Processa pacotes pendentes de presenca/status/anuncio para o slot
    // Retorna true se enviou um pacote de controle (nao enviar input no mesmo ciclo)
    bool processControlPackets(uint8_t playerIdx);

    // Verifica se o endpoint IN de um jogador especifico (0..3) esta pronto para envio
    bool ready(uint8_t playerIdx);

    // Envia pacote de estado recebido do ESP32 para o jogador correspondente
    bool sendFromGamepadPacket(uint8_t playerIdx, const GamepadStatePacket* pkt);

    // Envia estado neutro para o jogador (0..3)
    bool sendNeutral(uint8_t playerIdx);

    // Define callback para receber Rumble e LED de qualquer um dos 4 controles
    void setFeedbackCallback(XInputFeedbackCallback cb);

private:
    bool sendReport29(uint8_t playerIdx, const XInputWirelessReport29* report);

    USBXInputWirelessDataInterface  _dataInterfaces[GP_MAX_CONTROLLERS];
    USBXInputWirelessAudioInterface _audioInterfaces[GP_MAX_CONTROLLERS];
    bool _slotPresent[GP_MAX_CONTROLLERS];
};

extern USBXInputManager XInputUSB;

#endif // USB_XINPUT_H
