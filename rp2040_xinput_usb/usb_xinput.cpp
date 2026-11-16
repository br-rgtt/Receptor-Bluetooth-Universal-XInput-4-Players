#include "usb_xinput.h"
#include "device/usbd_pvt.h"
#include <string.h>

#define XINPUT_W_DATA_DESC_LEN   43  // 9 + 20 + 7 + 7
#define XINPUT_W_AUDIO_DESC_LEN  35  // 9 + 12 + 7 + 7

static uint8_t s_rhport = 0;
static uint8_t s_epIn[GP_MAX_CONTROLLERS]  = {0};
static uint8_t s_epOut[GP_MAX_CONTROLLERS] = {0};

static volatile bool s_slotPresentState[GP_MAX_CONTROLLERS] = {false};
static volatile bool s_pendingPresence[GP_MAX_CONTROLLERS]  = {false};
static volatile bool s_pendingAnnounce[GP_MAX_CONTROLLERS]  = {false};
static volatile bool s_pendingStatus[GP_MAX_CONTROLLERS]    = {false};

CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_cfgBuf[512];
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_inBuf[GP_MAX_CONTROLLERS][32];
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_outBuf[GP_MAX_CONTROLLERS][32];

static XInputFeedbackCallback s_feedbackCb = nullptr;

USBXInputManager XInputUSB;

// ----------------------------------------------------------------------------
// Requisicoes de Controle Vendor (Capabilities / Status do Receptor 045E:0719)
// ----------------------------------------------------------------------------
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (request->bRequest == 0x01 && request->wValue == 0x0100) {
            static const uint8_t capabilities[20] = {
                0x00, 0x14, 0xFF, 0xF7, 0xFF, 0xFF, 0xC0, 0xFF,
                0xC0, 0xFF, 0xC0, 0xFF, 0xC0, 0xFF, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00
            };
            return tud_control_xfer(rhport, request, (void*)capabilities, sizeof(capabilities));
        }
    }

    return tud_control_status(rhport, request);
}

// ----------------------------------------------------------------------------
// Callbacks do Driver Customizado TinyUSB (usbd_class_driver_t)
// ----------------------------------------------------------------------------
static void xinput_driver_init(void) {
    memset(s_epIn,  0, sizeof(s_epIn));
    memset(s_epOut, 0, sizeof(s_epOut));
}

static bool xinput_driver_deinit(void) {
    memset(s_epIn,  0, sizeof(s_epIn));
    memset(s_epOut, 0, sizeof(s_epOut));
    return true;
}

static void xinput_driver_reset(uint8_t rhport) {
    (void)rhport;
    memset(s_epIn,  0, sizeof(s_epIn));
    memset(s_epOut, 0, sizeof(s_epOut));
    for (uint8_t i = 0; i < GP_MAX_CONTROLLERS; i++) {
        if (s_slotPresentState[i]) {
            s_pendingPresence[i] = true;
            s_pendingAnnounce[i] = true;
            s_pendingStatus[i]   = true;
        }
    }
}

static uint16_t xinput_driver_open(uint8_t rhport, tusb_desc_interface_t const* desc_intf, uint16_t max_len) {
    if (desc_intf->bInterfaceClass != 0xFF ||
        desc_intf->bInterfaceSubClass != 0x5D ||
        (desc_intf->bInterfaceProtocol != 0x81 && desc_intf->bInterfaceProtocol != 0x82)) {
        return 0;
    }

    bool isData = (desc_intf->bInterfaceProtocol == 0x81);
    uint16_t expectedLen = isData ? XINPUT_W_DATA_DESC_LEN : XINPUT_W_AUDIO_DESC_LEN;
    if (max_len < expectedLen) {
        return 0;
    }

    s_rhport = rhport;
    uint8_t slot = desc_intf->bInterfaceNumber / 2;
    if (slot >= GP_MAX_CONTROLLERS) {
        return 0;
    }

    uint16_t drv_len = sizeof(tusb_desc_interface_t);
    uint8_t const* p_desc = tu_desc_next(desc_intf);

    uint8_t found_endpoints = 0;
    while (found_endpoints < desc_intf->bNumEndpoints && drv_len < max_len) {
        if (tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT) {
            tusb_desc_endpoint_t const* desc_ep = (tusb_desc_endpoint_t const*)p_desc;
            if (!usbd_edpt_open(rhport, desc_ep)) {
                return 0;
            }
            if (isData) {
                if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
                    s_epIn[slot] = desc_ep->bEndpointAddress;
                } else {
                    s_epOut[slot] = desc_ep->bEndpointAddress;
                }
            }
            found_endpoints++;
        }
        drv_len += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    }

    if (isData && s_epOut[slot] != 0) {
        usbd_edpt_xfer(rhport, s_epOut[slot], s_outBuf[slot], sizeof(s_outBuf[slot]), false);
    }

    return drv_len;
}

static bool xinput_driver_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    return tud_vendor_control_xfer_cb(rhport, stage, request);
}

static bool xinput_driver_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)result;

    for (uint8_t slot = 0; slot < GP_MAX_CONTROLLERS; slot++) {
        if (s_epOut[slot] != 0 && ep_addr == s_epOut[slot]) {
            const uint8_t* buf = s_outBuf[slot];

            if (xferred_bytes >= 4) {
                // 1. Consulta de presenca/info enviada pelo xusb22.sys: [0x08, 0x00, 0x0F, 0xC0]
                if (buf[0] == 0x08 && buf[1] == 0x00 && buf[2] == 0x0F) {
                    s_pendingPresence[slot] = true;
                    if (s_slotPresentState[slot]) {
                        s_pendingAnnounce[slot] = true;
                        s_pendingStatus[slot]   = true;
                    }
                }
                // 2. Consulta de status/bateria enviada pelo xusb22.sys: [0x00, 0x00, 0x00, 0x40]
                else if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x40) {
                    if (s_slotPresentState[slot]) {
                        s_pendingStatus[slot] = true;
                    }
                }
                // 3. Pacote de Rumble Wireless (045E:0719): [0x00, 0x01, 0x0F, 0xC0, 0x00, left, right, ...]
                else if (buf[0] == 0x00 && buf[1] == 0x01 && xferred_bytes >= 7 && s_feedbackCb != nullptr) {
                    s_feedbackCb(slot, GP_FB_FLAG_RUMBLE, buf[5], buf[6], 0);
                }
                // 4. Pacote de LED Wireless (045E:0719): [0x00, 0x00, 0x08, 0x40+pattern, ...]
                else if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x08 && buf[3] >= 0x40 && s_feedbackCb != nullptr) {
                    s_feedbackCb(slot, GP_FB_FLAG_PLAYER_LED, 0, 0, buf[3] - 0x40);
                }
                // 5. Fallback formato Wired
                else if (buf[0] == 0x00 && buf[1] == 0x08 && xferred_bytes >= 5 && s_feedbackCb != nullptr) {
                    s_feedbackCb(slot, GP_FB_FLAG_RUMBLE, buf[3], buf[4], 0);
                }
                else if (buf[0] == 0x01 && buf[1] == 0x03 && s_feedbackCb != nullptr) {
                    s_feedbackCb(slot, GP_FB_FLAG_PLAYER_LED, 0, 0, buf[2]);
                }
            }

            usbd_edpt_xfer(rhport, s_epOut[slot], s_outBuf[slot], sizeof(s_outBuf[slot]), false);
            break;
        }
    }

    return true;
}

static const usbd_class_driver_t s_xinputClassDriver = {
    .name            = "XINPUT_W",
    .init            = xinput_driver_init,
    .deinit          = xinput_driver_deinit,
    .reset           = xinput_driver_reset,
    .open            = xinput_driver_open,
    .control_xfer_cb = xinput_driver_control_xfer_cb,
    .xfer_cb         = xinput_driver_xfer_cb,
    .xfer_isr        = nullptr,
    .sof             = nullptr
};

extern "C" usbd_class_driver_t const* usbd_app_driver_get_cb(uint8_t* driver_count) {
    *driver_count = 1;
    return &s_xinputClassDriver;
}

// ----------------------------------------------------------------------------
// Descritores das Interfaces Wireless (Data 0x81 e Audio 0x82)
// ----------------------------------------------------------------------------
USBXInputWirelessDataInterface::USBXInputWirelessDataInterface() {}

uint16_t USBXInputWirelessDataInterface::getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t* buf, uint16_t bufsize) {
    (void)itfnum_deprecated;
    if (buf == nullptr) return XINPUT_W_DATA_DESC_LEN;
    if (bufsize < XINPUT_W_DATA_DESC_LEN) return 0;

    uint8_t itfnum = TinyUSBDevice.allocInterface(1);
    uint8_t epIn   = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
    uint8_t epOut  = TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT);

    const uint8_t desc[XINPUT_W_DATA_DESC_LEN] = {
        // Interface Descriptor (9 bytes)
        0x09, 0x04, itfnum, 0x00, 0x02, 0xFF, 0x5D, 0x81, 0x00,

        // XUSB Wireless Adapter Data Descriptor (20 bytes - Tipo 0x22, capturado do 045E:0719 original)
        0x14, 0x22, 0x00, 0x01, 0x13,
        epIn,
        0x1D, 0x00, 0x17, 0x01, 0x02, 0x08, 0x13,
        epOut,
        0x0C, 0x00, 0x0C, 0x01, 0x02, 0x08,

        // Endpoint IN (7 bytes - 1ms / 1000Hz)
        0x07, 0x05, epIn,  0x03, 0x20, 0x00, 0x01,
        // Endpoint OUT (7 bytes - Rumble & LEDs)
        0x07, 0x05, epOut, 0x03, 0x20, 0x00, 0x08
    };

    memcpy(buf, desc, XINPUT_W_DATA_DESC_LEN);
    return XINPUT_W_DATA_DESC_LEN;
}

USBXInputWirelessAudioInterface::USBXInputWirelessAudioInterface() {}

uint16_t USBXInputWirelessAudioInterface::getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t* buf, uint16_t bufsize) {
    (void)itfnum_deprecated;
    if (buf == nullptr) return XINPUT_W_AUDIO_DESC_LEN;
    if (bufsize < XINPUT_W_AUDIO_DESC_LEN) return 0;

    uint8_t itfnum = TinyUSBDevice.allocInterface(1);
    uint8_t epIn   = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
    uint8_t epOut  = TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT);

    const uint8_t desc[XINPUT_W_AUDIO_DESC_LEN] = {
        // Interface Descriptor (9 bytes)
        0x09, 0x04, itfnum, 0x00, 0x02, 0xFF, 0x5D, 0x82, 0x00,

        // XUSB Wireless Adapter Audio Descriptor (12 bytes - Tipo 0x22, capturado do 045E:0719 original)
        0x0C, 0x22, 0x00, 0x01, 0x01,
        epIn,
        0x00, 0x40, 0x01,
        epOut,
        0x20, 0x00,

        // Endpoint IN (7 bytes)
        0x07, 0x05, epIn,  0x03, 0x20, 0x00, 0x02,
        // Endpoint OUT (7 bytes)
        0x07, 0x05, epOut, 0x03, 0x20, 0x00, 0x04
    };

    memcpy(buf, desc, XINPUT_W_AUDIO_DESC_LEN);
    return XINPUT_W_AUDIO_DESC_LEN;
}

// ----------------------------------------------------------------------------
// Implementacao de USBXInputManager (Receptor Sem Fio Xbox 360 - 045E:0719)
// ----------------------------------------------------------------------------
USBXInputManager::USBXInputManager() {
    memset(_slotPresent, 0, sizeof(_slotPresent));
}

bool USBXInputManager::begin() {
    if (TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.detach();
        delay(150);
    }

    memset(s_epIn,  0, sizeof(s_epIn));
    memset(s_epOut, 0, sizeof(s_epOut));
    memset(_slotPresent, 0, sizeof(_slotPresent));
    for (uint8_t i = 0; i < GP_MAX_CONTROLLERS; i++) {
        s_slotPresentState[i] = false;
        s_pendingPresence[i]  = false;
        s_pendingAnnounce[i]  = false;
        s_pendingStatus[i]    = false;
    }

    TinyUSBDevice.clearConfiguration();
    TinyUSBDevice.setConfigurationBuffer(s_cfgBuf, sizeof(s_cfgBuf));

    // Identificacao do Receptor Sem Fio Oficial Xbox 360 para Windows (045E:0719)
    TinyUSBDevice.setID(0x045E, 0x0719);
    TinyUSBDevice.setVersion(0x0200);
    TinyUSBDevice.setDeviceVersion(0x0100);
    TinyUSBDevice.setConfigurationMaxPower(500);
    TinyUSBDevice.setManufacturerDescriptor("Microsoft");
    TinyUSBDevice.setProductDescriptor("Xbox 360 Wireless Receiver for Windows");
    TinyUSBDevice.setSerialDescriptor("1000001");

    tusb_desc_device_t* devDesc = (tusb_desc_device_t*)tud_descriptor_device_cb();
    if (devDesc != nullptr) {
        devDesc->bcdUSB          = 0x0200;
        devDesc->bDeviceClass    = 0xFF;
        devDesc->bDeviceSubClass = 0xFF;
        devDesc->bDeviceProtocol = 0xFF;
        devDesc->idVendor        = 0x045E;
        devDesc->idProduct       = 0x0719;
        devDesc->bcdDevice       = 0x0100;
    }

    // Adiciona os 4 pares de interfaces (Data + Audio) = 8 interfaces
    for (uint8_t i = 0; i < GP_MAX_CONTROLLERS; i++) {
        TinyUSBDevice.addInterface(_dataInterfaces[i]);
        TinyUSBDevice.addInterface(_audioInterfaces[i]);
    }

    if (TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.attach();
    }

    return true;
}

bool USBXInputManager::isSlotPresent(uint8_t playerIdx) const {
    if (playerIdx >= GP_MAX_CONTROLLERS) return false;
    return _slotPresent[playerIdx];
}

bool USBXInputManager::setSlotPresent(uint8_t playerIdx, bool present) {
    if (playerIdx >= GP_MAX_CONTROLLERS) {
        return false;
    }
    if (_slotPresent[playerIdx] == present) {
        return true;
    }

    _slotPresent[playerIdx]       = present;
    s_slotPresentState[playerIdx] = present;
    s_pendingPresence[playerIdx]  = true;

    if (present) {
        s_pendingAnnounce[playerIdx] = true;
        s_pendingStatus[playerIdx]   = true;
    } else {
        s_pendingAnnounce[playerIdx] = false;
        s_pendingStatus[playerIdx]   = false;
    }

    return true;
}

bool USBXInputManager::processControlPackets(uint8_t playerIdx) {
    if (playerIdx >= GP_MAX_CONTROLLERS || !tud_mounted()) {
        return false;
    }

    uint8_t ep = s_epIn[playerIdx];
    if (ep == 0 || usbd_edpt_busy(s_rhport, ep)) {
        return false;
    }

    if (s_pendingPresence[playerIdx]) {
        if (tud_suspended()) tud_remote_wakeup();
        if (!usbd_edpt_claim(s_rhport, ep)) return false;

        s_inBuf[playerIdx][0] = 0x08;
        s_inBuf[playerIdx][1] = s_slotPresentState[playerIdx] ? 0x80 : 0x00;
        if (usbd_edpt_xfer(s_rhport, ep, s_inBuf[playerIdx], 2, false)) {
            s_pendingPresence[playerIdx] = false;
        }
        return true;
    }

    if (s_pendingAnnounce[playerIdx]) {
        if (tud_suspended()) tud_remote_wakeup();
        if (!usbd_edpt_claim(s_rhport, ep)) return false;

        // Pacote Announce exato (29 bytes) capturado do controle Xbox 360 Wireless original
        const uint8_t announce[29] = {
            0x00, 0x0F, 0x00, 0xF0, 0x00, 0xCC,
            0xE1, 0xE0, 0x02, (uint8_t)(0x50 + playerIdx),
            0xB8, 0x7C, 0xA6, 0x60, 0x00, 0x20,
            0x13, 0xE3, 0x20, 0x1D, 0x30, 0x03, 0x40, 0x01, 0x50, 0x01,
            0xFF, 0xFF, 0xFF
        };
        memcpy(s_inBuf[playerIdx], announce, 29);
        if (usbd_edpt_xfer(s_rhport, ep, s_inBuf[playerIdx], 29, false)) {
            s_pendingAnnounce[playerIdx] = false;
        }
        return true;
    }

    if (s_pendingStatus[playerIdx]) {
        if (tud_suspended()) tud_remote_wakeup();
        if (!usbd_edpt_claim(s_rhport, ep)) return false;

        // Pacote de Status / Bateria (29 bytes) capturado do controle original
        memset(s_inBuf[playerIdx], 0, 29);
        s_inBuf[playerIdx][0] = 0x00;
        s_inBuf[playerIdx][1] = 0x00;
        s_inBuf[playerIdx][2] = 0x00;
        s_inBuf[playerIdx][3] = 0x13;
        s_inBuf[playerIdx][4] = 0xE2;
        if (usbd_edpt_xfer(s_rhport, ep, s_inBuf[playerIdx], 29, false)) {
            s_pendingStatus[playerIdx] = false;
        }
        return true;
    }

    return false;
}

bool USBXInputManager::ready(uint8_t playerIdx) {
    if (playerIdx >= GP_MAX_CONTROLLERS) return false;
    uint8_t ep = s_epIn[playerIdx];
    return tud_mounted() && (ep != 0) && !usbd_edpt_busy(s_rhport, ep);
}

bool USBXInputManager::sendReport29(uint8_t playerIdx, const XInputWirelessReport29* report) {
    if (playerIdx >= GP_MAX_CONTROLLERS || !tud_mounted()) {
        return false;
    }

    uint8_t ep = s_epIn[playerIdx];
    if (ep == 0) {
        return false;
    }

    if (tud_suspended()) {
        tud_remote_wakeup();
    }

    if (!usbd_edpt_claim(s_rhport, ep)) {
        return false;
    }

    memcpy(s_inBuf[playerIdx], report, sizeof(XInputWirelessReport29));
    return usbd_edpt_xfer(s_rhport, ep, s_inBuf[playerIdx], sizeof(XInputWirelessReport29), false);
}

bool USBXInputManager::sendFromGamepadPacket(uint8_t playerIdx, const GamepadStatePacket* pkt) {
    XInputWirelessReport29 rep;
    memset(&rep, 0, sizeof(rep));
    rep.header[0]    = 0x00;
    rep.header[1]    = 0x01; // Bit 0 = dados validos de controle
    rep.header[2]    = 0x00;
    rep.header[3]    = 0xF0;
    rep.reportId     = 0x00;
    rep.reportSize   = 0x13;
    rep.buttons      = pkt->buttons;
    rep.leftTrigger  = pkt->lt;
    rep.rightTrigger = pkt->rt;
    rep.thumbLX      = pkt->lx;
    rep.thumbLY      = pkt->ly;
    rep.thumbRX      = pkt->rx;
    rep.thumbRY      = pkt->ry;
    return sendReport29(playerIdx, &rep);
}

bool USBXInputManager::sendNeutral(uint8_t playerIdx) {
    XInputWirelessReport29 rep;
    memset(&rep, 0, sizeof(rep));
    rep.header[0]  = 0x00;
    rep.header[1]  = 0x01;
    rep.header[2]  = 0x00;
    rep.header[3]  = 0xF0;
    rep.reportId   = 0x00;
    rep.reportSize = 0x13;
    return sendReport29(playerIdx, &rep);
}

void USBXInputManager::setFeedbackCallback(XInputFeedbackCallback cb) {
    s_feedbackCb = cb;
}
