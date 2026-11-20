# Changelog

Todas as mudanças notáveis deste projeto estão documentadas neste arquivo.

## [v1.2.0] - 2026-11-20

### Adicionado
- **Suporte dedicado ao ESP32-CAM (`esp32_cam_bt_receiver/`):**
  - Nova versão independente do receptor Bluetooth Bluepad32 para placas **AI-Thinker ESP32-CAM** (com suporte a antena externa IPEX), preservando integralmente o firmware original do ESP32-WROOM.
  - Mapeamento adaptado para evitar conflito com a PSRAM interna (`GPIO 16`/`17`): UART2 em **`GPIO 14` (RX)** e **`GPIO 15` (TX)**, botão de pareamento no **`GPIO 13`** e LED de status integrado no **`GPIO 33`** com lógica invertida (`LOW` = aceso).
- **Scripts de compilação e gravação individuais:**
  - `compile_esp32.bat` (ESP32-WROOM), `compile_esp32_cam.bat` (ESP32-CAM), `compile_rp2040.bat` (RP2040-Zero) e `flash_esp32_cam.bat`.
  - `compile_all.bat` atualizado para compilar os 3 firmwares em sequência (`build/esp32/`, `build/esp32_cam/` e `build/rp2040/`).

### Corrigido
- **Conflito de hot-plug USB no Windows com o RP2040-Zero (`usb_xinput.cpp` e `rp2040_xinput_usb.ino`):**
  - Adicionado bloqueio de exclusão mútua (`ScopedUsbLock` com `__usb_mutex`) em todas as chamadas TinyUSB feitas no `loop()`, eliminando condição de corrida com a interrupção `usb_task_irq()` do RP2040.
  - Corrigido o tratamento de requisições Vendor IN desconhecidas em `tud_vendor_control_xfer_cb()` para retornar `STALL` (`false`) quando `wLength > 0`, seguindo estritamente a máquina de estados EP0 do USB 2.0.
  - `XInputUSB.begin()` movido para o início do `setup()` com intervalo de `detach()` aumentado para `500 ms`, garantindo enumeração limpa como `045E:0719`.
  - Taxa de polling dos endpoints de dados ajustada para **250 Hz (`4 ms`)**, padrão do receptor oficial da Microsoft, mantendo todos os 4 controles simultâneos estáveis sem bloquear outros dispositivos USB.

---

## [v1.1.1] - 2026-11-17

### Alterado
- **Presença 100% dinâmica no Windows (`joy.cpl`) para todos os 4 slots (`rp2040_xinput_usb.ino`):**
  - Removida a exibição fixa do Slot 0 ao plugar na USB.
  - Agora, com **0 controles Bluetooth conectados**, **nenhum controle** aparece no `joy.cpl` (apenas o receptor no Gerenciador de Dispositivos).
  - Cada controle (1º, 2º, 3º e 4º) só aparece no `joy.cpl` no momento em que é conectado ao ESP32 e desaparece automaticamente ao ser desligado, mantendo zero desconexões nos demais jogadores.

---

## [v1.1.0] - 2026-11-16

### Adicionado
- **Emulação do Receptor Sem Fio Oficial Xbox 360 (`VID 0x045E / PID 0x0719`)** no RP2040-Zero:
  - Descritores USB completos (`0x22` de Dados e Áudio nos 4 canais sem fio, 8 interfaces) capturados e validados a partir de um receptor físico original com o driver nativo `xusb22.sys` do Windows e `xpad` do Linux.
  - Máquina de estados de presença e handshake sem fio (`[0x08, 0x80]` / `[0x08, 0x00]`, pacote `0x0F Announce` de 29 bytes e pacote `0x13` de status/bateria), além de suporte a comandos de **Rumble** e **LED de Jogador** no formato wireless.
- **Conexão Contínua Sem Desconexão na USB (Zero USB Reset):**
  - Ao plugar na USB, apenas **1 controle** (Slot 0) aparece inicialmente no Windows (`joy.cpl`).
  - Quando o 2º, 3º ou 4º controle conecta no Bluetooth do ESP32, ele passa a aparecer dinamicamente no Windows **sem desconectar ou interromper** os controles que já estavam jogando.
  - Ao desligar um controle adicional, apenas o slot correspondente é removido do `joy.cpl`.

### Corrigido
- **Controle rigoroso do modo de pareamento no ESP32-WROOM (`esp32_bt_receiver.ino`):**
  - O receptor agora inicia com o pareamento **fechado** (`setPairingMode(false)`), permitindo apenas a reconexão automática de controles já pareados (`gap_connectable_control(1)`).
  - Ao encerrar o pareamento, `stopBtScanOnMainThread()` agora interrompe de fato o scan periódico (`gap_inquiry_stop()`, `uni_bt_le_scan_stop()`), desativa a descoberta (`gap_discoverable_control(0)`) e bloqueia novas conexões não pareadas (`uni_bt_enable_new_connections_unsafe(false)`).
  - Novos controles só podem ser pareados quando o botão **`BOOT`** do ESP32 é pressionado e o LED está piscando.

---

## [v1.0.0] - 2026-11-15

### Adicionado
- Versão inicial com receptor Bluetooth universal no ESP32-WROOM (`Bluepad32` com patch `maxACL=7` / `maxBLE=5`) + emulador USB XInput 1000 Hz no RP2040-Zero via UART (`921600 bps`) com CRC8.
- Suporte a até 4 controles simultâneos (PS4, PS5, Xbox Wireless, Switch Pro, Joy-Con L/R, 8BitDo, GameSir, genéricos) com Rumble e LEDs de jogador.
- Toolkit portátil (`setup_tools.bat`, `compile_all.bat`, `flash_esp32.bat`, `flash_rp2040.bat`).
