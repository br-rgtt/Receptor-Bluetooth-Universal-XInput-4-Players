#include "usb_xinput.h"
#include "device/usbd_pvt.h"
#include <string.h>

// Tamanho do descritor de cada interface Xbox 360 (9 + 16 + 7 + 7 = 39 bytes)
#define XINPUT_DESC_LEN 39

// Codigo de requisicao Vendor para o Microsoft OS 2.0 Descriptor
#define MS_OS_20_VENDOR_CODE 0x20

static uint8_t s_rhport = 0;
static uint8_t s_activePortCount = 1;
static uint8_t s_epIn[GP_MAX_CONTROLLERS]  = {0};
static uint8_t s_epOut[GP_MAX_CONTROLLERS] = {0};

CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_inBuf[GP_MAX_CONTROLLERS][32];
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_outBuf[GP_MAX_CONTROLLERS][32];

// Buffer para o BOS Descriptor (33 bytes) e para o MS OS 2.0 Descriptor Set (ate 122 bytes)
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_bosDescBuf[33];
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_msOs20DescBuf[128];

static XInputFeedbackCallback s_feedbackCb = nullptr;

USBXInputManager XInputUSB;

// ----------------------------------------------------------------------------
// Geracao dinamica do Microsoft OS 2.0 Descriptor Set (CompatibleID = "XUSB10")
// Permite que o Windows (xusb22.inf -> USB\MS_COMP_XUSB10) instale o driver
// nativo "Xbox 360 Controller for Windows" em cada uma das 1..4 interfaces!
// ----------------------------------------------------------------------------
static uint16_t buildMsOs20DescriptorSet(uint8_t portCount, uint8_t* outBuf) {
    if (portCount < 1) portCount = 1;
    if (portCount > GP_MAX_CONTROLLERS) portCount = GP_MAX_CONTROLLERS;

    // Se houver apenas 1 interface: Header (10B) + CompatibleID (20B) = 30 bytes
    if (portCount == 1) {
        const uint16_t totalLen = 30;
        const uint8_t singleDesc[30] = {
            // Set Header (10 bytes)
            0x0A, 0x00,                         // wLength = 10
            0x00, 0x00,                         // MS_OS_20_SET_HEADER_DESCRIPTOR
            0x00, 0x00, 0x03, 0x06,             // dwWindowsVersion = 0x06030000 (Win 8.1+)
            (uint8_t)(totalLen & 0xFF), (uint8_t)(totalLen >> 8),

            // Compatible ID Descriptor (20 bytes)
            0x14, 0x00,                         // wLength = 20
            0x03, 0x00,                         // MS_OS_20_FEATURE_COMPATBLE_ID
            'X', 'U', 'S', 'B', '1', '0', 0x00, 0x00, // CompatibleID = "XUSB10"
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        if (outBuf) {
            memcpy(outBuf, singleDesc, totalLen);
        }
        return totalLen;
    }

    // Dispositivo Composto (2 a 4 controles):
    // Header (10B) + portCount * [Function Subset Header (8B) + CompatibleID (20B)]
    const uint16_t totalLen = (uint16_t)(10 + portCount * 28);
    if (!outBuf) {
        return totalLen;
    }

    uint8_t* p = outBuf;
    // 1. Set Header (10 bytes)
    const uint8_t header[10] = {
        0x0A, 0x00,                         // wLength = 10
        0x00, 0x00,                         // MS_OS_20_SET_HEADER_DESCRIPTOR
        0x00, 0x00, 0x03, 0x06,             // dwWindowsVersion = 0x06030000 (Win 8.1+)
        (uint8_t)(totalLen & 0xFF), (uint8_t)(totalLen >> 8)
    };
    memcpy(p, header, 10);
    p += 10;

    // 2. Function Subsets para cada interface (0 .. portCount - 1)
    for (uint8_t i = 0; i < portCount; i++) {
        const uint8_t subset[28] = {
            // Function Subset Header (8 bytes)
            0x08, 0x00,                     // wLength = 8
            0x02, 0x00,                     // MS_OS_20_SUBSET_HEADER_FUNCTION
            i,                              // bFirstInterface = i
            0x00,                           // bReserved
            0x1C, 0x00,                     // wSubsetLength = 28 (8 + 20)

            // Compatible ID Feature Descriptor (20 bytes)
            0x14, 0x00,                     // wLength = 20
            0x03, 0x00,                     // MS_OS_20_FEATURE_COMPATBLE_ID
            'X', 'U', 'S', 'B', '1', '0', 0x00, 0x00, // CompatibleID = "XUSB10"
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        memcpy(p, subset, 28);
        p += 28;
    }

    return totalLen;
}

extern "C" uint8_t const* tud_descriptor_bos_cb(void) {
    uint16_t msOs20Len = buildMsOs20DescriptorSet(s_activePortCount, nullptr);
    const uint8_t bosTemplate[33] = {
        TUD_BOS_DESCRIPTOR(33, 1),
        TUD_BOS_MS_OS_20_DESCRIPTOR(msOs20Len, MS_OS_20_VENDOR_CODE)
    };
    memcpy(s_bosDescBuf, bosTemplate, sizeof(bosTemplate));
    return s_bosDescBuf;
}

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
        // Requisicao Microsoft OS 2.0 Descriptor Set (wIndex = 0x0007)
        if (request->bRequest == MS_OS_20_VENDOR_CODE && request->wIndex == 0x0007) {
            uint16_t totalLen = buildMsOs20DescriptorSet(s_activePortCount, s_msOs20DescBuf);
            return tud_control_xfer(rhport, request, s_msOs20DescBuf, totalLen);
        }

        // Requisicao de Capabilities do driver Xbox 360 (bRequest = 0x01, wValue = 0x0100)
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
}

static uint16_t xinput_driver_open(uint8_t rhport, tusb_desc_interface_t const* desc_intf, uint16_t max_len) {
    if (desc_intf->bInterfaceClass != 0xFF ||
        desc_intf->bInterfaceSubClass != 0x5D ||
        desc_intf->bInterfaceProtocol != 0x01) {
        return 0;
    }

    if (max_len < XINPUT_DESC_LEN) {
        return 0;
    }

    uint8_t slot = desc_intf->bInterfaceNumber;
    if (slot >= GP_MAX_CONTROLLERS) {
        return 0;
    }

    s_rhport = rhport;
    uint16_t drv_len = sizeof(tusb_desc_interface_t);
    uint8_t const* p_desc = tu_desc_next(desc_intf);

    uint8_t found_endpoints = 0;
    while (found_endpoints < desc_intf->bNumEndpoints && drv_len < max_len) {
        if (tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT) {
            tusb_desc_endpoint_t const* desc_ep = (tusb_desc_endpoint_t const*)p_desc;
            if (!usbd_edpt_open(rhport, desc_ep)) {
                return 0;
            }
            if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
                s_epIn[slot] = desc_ep->bEndpointAddress;
            } else {
                s_epOut[slot] = desc_ep->bEndpointAddress;
            }
            found_endpoints++;
        }
        drv_len += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    }

    // Arma o endpoint OUT deste controle para receber Rumble e Player LED do PC
    if (s_epOut[slot] != 0) {
        usbd_edpt_xfer(rhport, s_epOut[slot], s_outBuf[slot], sizeof(s_outBuf[slot]), false);
    }

    return drv_len;
}

static bool xinput_driver_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    return tud_vendor_control_xfer_cb(rhport, stage, request);
}

static bool xinput_driver_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)result;

    // Verifica se o pacote recebido pertence ao endpoint OUT de algum dos 4 controles
    for (uint8_t slot = 0; slot < GP_MAX_CONTROLLERS; slot++) {
        if (s_epOut[slot] != 0 && ep_addr == s_epOut[slot]) {
            if (xferred_bytes >= 3 && s_feedbackCb != nullptr) {
                const uint8_t* buf = s_outBuf[slot];
                // Pacote de Rumble: [0x00, 0x08, 0x00, strong/left, weak/right, ...]
                if (buf[0] == 0x00 && buf[1] == 0x08 && xferred_bytes >= 5) {
                    s_feedbackCb(slot, GP_FB_FLAG_RUMBLE, buf[3], buf[4], 0);
                }
                // Pacote de LED do Player: [0x01, 0x03, pattern]
                else if (buf[0] == 0x01 && buf[1] == 0x03) {
                    s_feedbackCb(slot, GP_FB_FLAG_PLAYER_LED, 0, 0, buf[2]);
                }
            }
            // Re-arma o endpoint OUT deste slot
            usbd_edpt_xfer(rhport, s_epOut[slot], s_outBuf[slot], sizeof(s_outBuf[slot]), false);
            break;
        }
    }

    return true;
}

static const usbd_class_driver_t s_xinputClassDriver = {
    .name            = "XINPUT",
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
// Implementacao de USBXInputInterface
// ----------------------------------------------------------------------------
USBXInputInterface::USBXInputInterface() {}

uint16_t USBXInputInterface::getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t* buf, uint16_t bufsize) {
    (void)itfnum_deprecated;

    if (buf == nullptr) {
        return XINPUT_DESC_LEN;
    }
    if (bufsize < XINPUT_DESC_LEN) {
        return 0;
    }

    uint8_t itfnum = TinyUSBDevice.allocInterface(1);
    uint8_t epIn   = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
    uint8_t epOut  = TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT);

    const uint8_t desc[XINPUT_DESC_LEN] = {
        // Interface Descriptor (9 bytes)
        0x09,                   // bLength
        0x04,                   // bDescriptorType (INTERFACE)
        itfnum,                 // bInterfaceNumber (0..3)
        0x00,                   // bAlternateSetting
        0x02,                   // bNumEndpoints (1 IN, 1 OUT)
        0xFF,                   // bInterfaceClass (Vendor Specific)
        0x5D,                   // bInterfaceSubClass (Xbox 360)
        0x01,                   // bInterfaceProtocol (Gamepad)
        0x00,                   // iInterface

        // Xbox 360 Vendor Descriptor (16 bytes - Tipo 0x21)
        0x10, 0x21, 0x10, 0x01, 0x01, 0x24,
        epIn,
        0x14, 0x03, 0x00, 0x03, 0x13,
        epOut,
        0x00, 0x03, 0x00,

        // Endpoint IN Descriptor (7 bytes - Polling 1ms / 1000Hz)
        0x07,                   // bLength
        0x05,                   // bDescriptorType (ENDPOINT)
        epIn,                   // bEndpointAddress (IN)
        0x03,                   // bmAttributes (Interrupt)
        0x20, 0x00,             // wMaxPacketSize (32 bytes)
        0x01,                   // bInterval (1 ms = 1000 Hz)

        // Endpoint OUT Descriptor (7 bytes - Rumble & LEDs)
        0x07,                   // bLength
        0x05,                   // bDescriptorType (ENDPOINT)
        epOut,                  // bEndpointAddress (OUT)
        0x03,                   // bmAttributes (Interrupt)
        0x20, 0x00,             // wMaxPacketSize (32 bytes)
        0x08                    // bInterval (8 ms)
    };

    memcpy(buf, desc, XINPUT_DESC_LEN);
    return XINPUT_DESC_LEN;
}

// ----------------------------------------------------------------------------
// Implementacao de USBXInputManager (1 a 4 Controles Dinamicos)
// ----------------------------------------------------------------------------
USBXInputManager::USBXInputManager() : _activeCount(1) {}

bool USBXInputManager::begin(uint8_t initialControllerCount) {
    _activeCount = 0; // Forca configuracao inicial
    return setControllerCount(initialControllerCount);
}

bool USBXInputManager::setControllerCount(uint8_t count) {
    if (count < 1) count = 1;
    if (count > GP_MAX_CONTROLLERS) count = GP_MAX_CONTROLLERS;

    if (count == _activeCount && TinyUSBDevice.isInitialized()) {
        return true;
    }

    // Desconecta do barramento USB para re-enumerar com a nova quantidade de controles
    if (TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.detach();
        delay(350);
    }

    memset(s_epIn,  0, sizeof(s_epIn));
    memset(s_epOut, 0, sizeof(s_epOut));

    _activeCount      = count;
    s_activePortCount = count;

    TinyUSBDevice.clearConfiguration();

    // Para 1 controle (0 ou 1 controle BT conectado):
    //   Usa PID 0x028E (Controle Xbox 360 com Fio puro)
    // Para 2, 3 ou 4 controles conectados:
    //   Usa PID 0x02A2..0x02A4 em modo Composto (bDeviceClass = 0x00) + MS OS 2.0 (XUSB10)
    //   para que o Windows (usbccgp.sys + xusb22.sys) e o Linux (xpad) exponham N controles independentes!
    uint16_t pid = (count == 1) ? 0x028E : (uint16_t)(0x02A0 + count);

    TinyUSBDevice.setID(0x045E, pid);
    TinyUSBDevice.setVersion(0x0210); // USB 2.1 habilita leitura do BOS / MS OS 2.0 no Windows
    TinyUSBDevice.setDeviceVersion(0x0114);
    TinyUSBDevice.setConfigurationMaxPower(500);
    TinyUSBDevice.setManufacturerDescriptor("Microsoft Corporation");
    TinyUSBDevice.setProductDescriptor("Controller (XBOX 360 For Windows)");
    TinyUSBDevice.setSerialDescriptor("1000001");

    tusb_desc_device_t* devDesc = (tusb_desc_device_t*)tud_descriptor_device_cb();
    if (devDesc != nullptr) {
        devDesc->bcdUSB          = 0x0210;
        devDesc->bDeviceClass    = (count == 1) ? 0xFF : 0x00;
        devDesc->bDeviceSubClass = (count == 1) ? 0xFF : 0x00;
        devDesc->bDeviceProtocol = (count == 1) ? 0xFF : 0x00;
        devDesc->idVendor        = 0x045E;
        devDesc->idProduct       = pid;
        devDesc->bcdDevice       = 0x0114;
    }

    bool ok = true;
    for (uint8_t i = 0; i < count; i++) {
        if (!TinyUSBDevice.addInterface(_interfaces[i])) {
            ok = false;
            break;
        }
    }

    if (TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.attach();
    }

    return ok;
}

bool USBXInputManager::ready(uint8_t playerIdx) {
    if (playerIdx >= _activeCount) return false;
    uint8_t ep = s_epIn[playerIdx];
    return tud_mounted() && (ep != 0) && !usbd_edpt_busy(s_rhport, ep);
}

bool USBXInputManager::sendReport(uint8_t playerIdx, const XInputReport20* report) {
    if (playerIdx >= _activeCount || !tud_mounted()) {
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

    memcpy(s_inBuf[playerIdx], report, sizeof(XInputReport20));
    return usbd_edpt_xfer(s_rhport, ep, s_inBuf[playerIdx], sizeof(XInputReport20), false);
}

bool USBXInputManager::sendFromGamepadPacket(uint8_t playerIdx, const GamepadStatePacket* pkt) {
    XInputReport20 rep;
    memset(&rep, 0, sizeof(rep));
    rep.reportId     = 0x00;
    rep.reportSize   = sizeof(XInputReport20); // 0x14 (20 bytes)
    rep.buttons      = pkt->buttons;
    rep.leftTrigger  = pkt->lt;
    rep.rightTrigger = pkt->rt;
    rep.thumbLX      = pkt->lx;
    rep.thumbLY      = pkt->ly;
    rep.thumbRX      = pkt->rx;
    rep.thumbRY      = pkt->ry;
    return sendReport(playerIdx, &rep);
}

bool USBXInputManager::sendNeutral(uint8_t playerIdx) {
    XInputReport20 rep;
    memset(&rep, 0, sizeof(rep));
    rep.reportId   = 0x00;
    rep.reportSize = sizeof(XInputReport20);
    return sendReport(playerIdx, &rep);
}

void USBXInputManager::setFeedbackCallback(XInputFeedbackCallback cb) {
    s_feedbackCb = cb;
}
