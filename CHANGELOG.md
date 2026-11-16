# Changelog

Todas as mudanças notáveis deste projeto estão documentadas neste arquivo.

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
