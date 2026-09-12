# muON-DTN Paper Experiment Reproduction Guide

## Reference Paper
> **"Integrating LoRa nodes into global DTN networks"**  
> *Carlo Caini, Alberto Soncini, Samo Grasic (2026)*  
> Alma Mater Studiorum – University of Bologna

---

## 1. System Topology & Architecture

This guide provides step-by-step instructions to physically assemble, compile, configure, and execute the end-to-end DTN experiment described in the paper. The network consists of three DTN endpoints spanning two physical convergence layers (**LoRaCL** and **UARTCL-COBS**):

```
+-----------------------------------------------------------------------------------+
|                                  DTN OVERLAY                                      |
|                                                                                   |
|  [ Node A (1.1) ]  <====== LoRaCL ======>  [ Node B (2.1) ]  <=== UARTCL-COBS ===>  [ PC (3.1) ] |
|  Sensor Source                             Relay / Gateway                          Linux IONe    |
+-----------------------------------------------------------------------------------+
```

### Node Profiles
| Node | Role | Hardware | Endpoints | Primary Interfaces | Storage Backend |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Node A** | Sensor Source & Actuator | Adafruit Feather M0 LoRa + BME280 + DS3231 + Button | `ipn:1.1` (BPA)<br>`ipn:1.2` (LED Actuator) | LoRa (SX1276, 868MHz) | Zero-Malloc RAM Storage (`RamStorage`) |
| **Node B** | DTN Gateway & Relay | Adafruit Feather M0 LoRa + SSD1306 OLED + DS3231 + SPI Flash | `ipn:2.1` (BPA)<br>`ipn:2.2` (LED Actuator)<br>`ipn:2.10` (OLED Display) | LoRa (Link 0)<br>UART-COBS (Link 1) | 8 MB Winbond W25Q64 SPI Flash (`SpiFlashStorage`) |
| **Node C (PC)** | Mission Ops / Gateway | Linux PC running `ione-uartcl-cobs` | `ipn:3.1` (Telemetry Sink) | USB Serial CDC (`/dev/ttyACM0`) | ION SDR / POSIX FS |

---

## 2. Hardware Wiring & Pinout

### 2.1 Critical Hardware Caveats for Adafruit Feather M0 LoRa
1. **LoRa DIO0 Jumper (Mandatory)**:  
   On standard Adafruit Feather M0 LoRa boards, the SX1276 `DIO0` interrupt line is **not connected** to the microcontroller by default. You **must solder a jumper wire or bridge** between the `DIO0` pad and **GPIO Pin 3** on the bottom of the board.
2. **SPI Flash CS Pin Conflict (Strict Rule)**:  
   On the Feather M0 LoRa, **Pin 4 is hardwired to the SX1276 LoRa RESET line**.  
   **NEVER use Pin 4 as Chip Select (CS) for SPI Flash**. Using Pin 4 will reset the LoRa modem on every SPI Flash transaction!  
   *The SPI Flash CS pin is assigned to **GPIO Pin 5** on Node B.*
3. **Dedicated Serial Port on Node B (Binary Safety)**:  
   On Node B, USB `Serial` is dedicated to binary COBS framing with IONe. All debug `Serial.print()` calls are automatically suppressed on Node B to prevent stream corruption.

---

### 2.2 Node A Wiring Diagram (Sensor Source)
*Microcontroller: Adafruit Feather M0 LoRa (SAMD21G18A)*

| Peripheral | Peripheral Pin | Feather M0 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **LoRa DIO0** | DIO0 Pad | **Pin 3** | Solder bridge on board underside |
| **Pushbutton** | Leg 1 | **Pin 5** | Configured with internal `INPUT_PULLUP` |
| | Leg 2 | **GND** | Pressing button pulls Pin 5 to GND |
| **BME280 Sensor** | VCC | **3.3V (3V)** | 3.3V logic level |
| | GND | **GND** | Common ground |
| | SCL | **SCL (Pin 21)** | Shared I2C Clock |
| | SDA | **SDA (Pin 20)** | Shared I2C Data |
| | SDO | **GND** | Selects I2C address `0x76` |
| **DS3231 RTC** | VCC | **3.3V (3V)** | |
| | GND | **GND** | Common ground |
| | SCL | **SCL (Pin 21)** | Shared I2C Clock |
| | SDA | **SDA (Pin 20)** | Shared I2C Data (Address `0x68`) |
| **Builtin LED** | Anode | **Pin 13** | Built-in LED (`LED_BUILTIN`) |

---

### 2.3 Node B Wiring Diagram (Gateway / Relay)
*Microcontroller: Adafruit Feather M0 LoRa (SAMD21G18A)*

| Peripheral | Peripheral Pin | Feather M0 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **LoRa DIO0** | DIO0 Pad | **Pin 3** | Solder bridge on board underside |
| **W25Q64 SPI Flash** | VCC | **3.3V (3V)** | External 8 MB NOR Flash |
| | GND | **GND** | Common ground |
| | CS | **Pin 5** | **Dedicated Flash CS** (Pin 4 is LoRa RST!) |
| | SCK | **SCK (Pin 24)** | Shared SPI Clock |
| | MOSI | **MOSI (Pin 23)**| Shared SPI Master Out |
| | MISO | **MISO (Pin 22)**| Shared SPI Master In |
| **SSD1306 OLED** | VCC | **3.3V (3V)** | 128x64 Monochrome I2C Display |
| | GND | **GND** | Common ground |
| | SCL | **SCL (Pin 21)** | Shared I2C Clock |
| | SDA | **SDA (Pin 20)** | Shared I2C Data (Address `0x3C`) |
| **DS3231 RTC** | VCC | **3.3V (3V)** | Synchronized via UARTCL SYNC frames |
| | GND | **GND** | Common ground |
| | SCL | **SCL (Pin 21)** | Shared I2C Clock |
| | SDA | **SDA (Pin 20)** | Shared I2C Data (Address `0x68`) |
| **USB CDC Port** | MicroUSB | **Linux PC USB** | Dedicated binary COBS link (115200 baud) |

---

## 3. Bidirectional Data Path Mechanics

### 3.1 Forward Telemetry Path (Node A $\to$ Node B $\to$ PC IONe)
1. **Trigger**: User presses the pushbutton on Node A (Pin 5).
2. **Integral Debounce**: `ButtonPlugin` samples the pin at 5 ms intervals. Upon validating 6 consecutive stable samples on the falling edge, it publishes `GGG_EVT_APP_TRIGGER` (`0x0100`) with payload `1` to the `SystemBus`.
3. **Sampling**: `Bme280Plugin` catches the trigger event, commands the BME280 into forced sampling mode, and formats the sensor reading into a compact JSON payload:
   ```json
   {"node":1,"t":23.4,"p":1012.8,"h":48.2}
   ```
4. **BPv7 Bundle Creation**: `Bme280Plugin` calls `BundleAgent::sendLocalData()` with destination `ipn:3.1`, priority `1` (Normal), and lifetime `3600` s. `BundleAgent` streams the RFC 9171 Primary Block and Payload Block directly into `RamStorage` without dynamic memory allocation, registers the metadata, and publishes `MUON_EVT_ROUTE_REQ`.
5. **Routing & Transmission**: `ConvergenceLayerManager` evaluates the destination (`ipn:3.1`) against Node A's `StaticRoutingEngine`. The route resolves to `CONFIG_MUON_LORA_LINK_ID` (Link 0).
6. **LoRa Transfer**: `LoRaConvergenceLayer` fragments the bundle if needed, prepends the polymorphic LoRaCL header, and transmits the frame(s) via the SX1276 modem at 868 MHz (SF9, BW 250 kHz, CR 4/5).
7. **Reception on Node B**: Node B's SX1276 receives the frame and signals DIO0. `ClmTickTask` services the interrupt, streams the packet directly into the external W25Q64 SPI Flash storage (`SpiFlashStorage`), verifies reassembly, and publishes `MUON_EVT_RX_READY`.
8. **Relay Routing**: Node B's `BundleAgent` evaluates destination `ipn:3.1`. The routing engine resolves Node 3 to `CONFIG_MUON_UART_COBS_LINK_ID` (Link 1) and emits `MUON_EVT_ROUTE_REQ`.
9. **UARTCL-COBS Transmission**: `UartCobsConvergenceLayer` streams the bundle from SPI Flash, appends CRC-16-CCITT, encodes with Consistent Overhead Byte Stuffing (COBS), delimits with `0x00`, and transmits across USB Serial.
10. **Delivery to IONe**: The `uartcl-cobs` adapter in IONe on Linux reads the COBS frame, validates CRC-16, ingests the BPv7 bundle into the SDR store, and delivers it to the listener endpoint `ipn:3.1` (`bpsink`).

---

### 3.2 Reverse Command Path (PC IONe $\to$ Node B $\to$ Node A Actuator)
1. **Command Generation**: Operator on Linux PC issues a command bundle addressed to `ipn:1.2`:
   ```bash
   bpsendfile ipn:3.1 ipn:1.2 command.txt
   ```
2. **Ingress at Node B**: Node B receives the COBS frame on USB Serial. `UartCobsConvergenceLayer` decodes stream-to-storage into SPI Flash, verifies CRC, and emits `MUON_EVT_RX_READY`.
3. **Forwarding over LoRa**: Node B's `BundleAgent` inspects destination `ipn:1.2`. Routing table resolves Node 1 to `CONFIG_MUON_LORA_LINK_ID`. Emits `MUON_EVT_ROUTE_REQ`. `LoRaConvergenceLayer` transmits across LoRa to Node A.
4. **Ingress at Node A**: Node A receives the bundle via `LoRaConvergenceLayer` into `RamStorage` and emits `MUON_EVT_RX_READY`.
5. **Local Delivery**: Node A's `BundleAgent` matches destination node `1` with local node `1`. It emits `MUON_EVT_BUNDLE_DELIVERED` with `payload.u32[1] = (1 << 16) | 2`.
6. **Actuation**: `LedActuatorPlugin` (registered for Service ID 2) captures the event, matches service `2`, toggles Pin 13 (`LED_BUILTIN`), and calls `BundleAgent::consumeDeliveredBundle()` to release the storage record.

---

## 4. Compilation & Flashing Instructions

### Prerequisites
- Install **PlatformIO Core (CLI)** (`pio`)
- Connect the Adafruit Feather M0 via USB

### 4.1 Flashing Node A (Sensor Source)
```bash
cd d:/Programmazione/workspace_muON_refactoring/muON-DTN

# 1. Apply Node A Kconfig configuration profile
cp configs/node_a.config .config

# 2. Build and upload firmware to Node A
pio run -e adafruit_feather_m0 --target upload
```

### 4.2 Flashing Node B (Gateway / Relay)
```bash
cd d:/Programmazione/workspace_muON_refactoring/muON-DTN

# 1. Apply Node B Kconfig configuration profile
cp configs/node_b.config .config

# 2. Build and upload firmware to Node B
pio run -e adafruit_feather_m0 --target upload
```

---

## 5. Linux PC IONe Gateway Setup

### 5.1 Compilation of IONe with UARTCL-COBS
On the Linux PC (Ubuntu / Debian / Raspberry Pi OS):
```bash
cd ione-uartcl-cobs
git checkout uartcl-cobs

# Compile and install IONe
./configure
make -j$(nproc)
sudo make install
sudo ldconfig
```

### 5.2 Configurazione Nome Seriale Persistente (Risoluzione Flapping ttyACM0 / ttyACM1)
Su Linux, ogni disconnessione o riavvio del microcontroller CDC può causare l'incremento del nome del device (es. `/dev/ttyACM0` $\to$ `/dev/ttyACM1`). Per garantire un collegamento **100% stabile e persistente**, adottare una delle due seguenti soluzioni:

#### Metodo A: Utilizzo del path univoco `by-id` (Consigliato, senza configurazioni di sistema)
Linux crea automaticamente link simbolici immutabili legati all'identità hardware USB della scheda:
```bash
ls -l /dev/serial/by-id/
# Esempio output:
# usb-Adafruit_Feather_M0_... -> ../../ttyACM0
```
È possibile avviare il gateway passando direttamente questo percorso (o con wildcard):
```bash
./start_gateway.sh /dev/serial/by-id/usb-Adafruit_Feather_M0* 115200
```

#### Metodo B: Regola `udev` per Device Name Persistente (`/dev/ttyNodeB`)
Per associare in modo permanente il Nodo B al nome `/dev/ttyNodeB` con permessi automatici:
1. Creare la regola udev:
   ```bash
   sudo bash -c 'cat <<EOF > /etc/udev/rules.d/99-muon-nodeb.rules
   SUBSYSTEM=="tty", ATTRS{idVendor}=="239a", SYMLINK+="ttyNodeB", MODE="0666", GROUP="dialout"
   EOF'
   ```
2. Ricaricare le regole udev:
   ```bash
   sudo udevadm control --reload-rules && sudo udevadm trigger
   ```
3. Verificare la creazione del link simbolico:
   ```bash
   ls -l /dev/ttyNodeB
   # lrwxrwxrwx 1 root root 7 ... /dev/ttyNodeB -> ttyACM0
   ```
4. Assicurarsi che il proprio utente appartenga al gruppo `dialout`:
   ```bash
   sudo usermod -a -G dialout $USER
   ```

### 5.3 Launching IONe Gateway
Pre-configured scripts are located in `muON-DTN/configs/ione_gateway/`:
```bash
cd muON-DTN/configs/ione_gateway

# Ensure executable permissions
chmod +x *.sh

# Start IONe Node 3 daemon targeting persistent node:
./start_gateway.sh /dev/ttyNodeB 115200
# oppure: ./start_gateway.sh /dev/serial/by-id/usb-Adafruit_Feather_M0* 115200
```

The startup script loads:
- `node_gateway.ionconfig`: Configures 20 MB SDR memory pool.
- `node_gateway.ionrc`: Node 3 initialization, clock synchronization.
- `node_gateway.ipnrc`: IPN routing rules.
- `node_gateway.bprc`: Starts the `uartcl-cobs` CLA on `/dev/ttyACM0` at 115200 baud.

---

## 6. Running the Experiment

### 6.1 Test 1: Forward Telemetry (Node A $\to$ Node B $\to$ PC)
1. On the Linux PC, open a terminal and start the DTN sink application on `ipn:3.1`:
   ```bash
   bpsink ipn:3.1
   ```
2. Press the pushbutton on **Node A** (connected to Pin 5).
3. **Observe**:
   - **Node A**: Pin 13 LED flashes briefly indicating bundle generation and LoRa transmission.
   - **Node B**: OLED display status changes:
     - Line 1: `Node B (2.1)`
     - Line 2: `TX: 0 | RX: 1`
     - Line 3: `RSSI: -72 | SNR: 8`
     - Line 4: `STATUS: RX OK -> FWD`
   - **Linux PC (`bpsink`)**: Outputs received bundle payload:
     ```
     {"node":1,"t":23.4,"p":1012.8,"h":48.2}
     ```

### 6.2 Test 2: Reverse Command Actuation (PC $\to$ Node B $\to$ Node A)
1. On the Linux PC, create a small command payload:
   ```bash
   echo "TOGGLE" > cmd.txt
   ```
2. Send the bundle to Node A's actuator endpoint (`ipn:1.2`):
   ```bash
   bpsendfile ipn:3.1 ipn:1.2 cmd.txt
   ```
3. **Observe**:
   - **Node B**: OLED increments RX count from UARTCL, forwards frame across LoRa.
   - **Node A**: Pin 13 `LED_BUILTIN` turns **ON**.
4. Issue the command a second time:
   ```bash
   bpsendfile ipn:3.1 ipn:1.2 cmd.txt
   ```
   - **Node A**: Pin 13 `LED_BUILTIN` turns **OFF** (toggled).

### 6.3 Stopping IONe
To cleanly shut down the IONe gateway daemon:
```bash
cd muON-DTN/configs/ione_gateway
./stop_gateway.sh
```

---

## 7. Troubleshooting & Diagnostic Guide

| Symptom | Probable Cause | Corrective Action |
| :--- | :--- | :--- |
| **LoRa never receives any packet** | Missing DIO0 hardware bridge | Solder the jumper wire between pad `DIO0` and `Pin 3` on the Feather M0 underside. |
| **Node B crashes or reboots on write** | SPI Flash CS conflict with LoRa RST | Confirm `CONFIG_GGG_SPIFLASH_CS_PIN=5` in `.config`. Pin 4 MUST remain dedicated to LoRa RST. |
| **IONe shows COBS framing errors** | Serial log corruption | Verify `CONFIG_MUON_DEBUG_USE_SERIAL1` is NOT enabled and direct `Serial.print` statements are omitted on Node B. |
| **Permission denied on `/dev/ttyACM0`** | Missing `dialout` group membership | Run `sudo usermod -a -G dialout $USER` and log out/in, or `sudo chmod 666 /dev/ttyACM0`. |
| **RTC time not progressing** | DS3231 oscillator stopped | Ensure coin battery (CR1220) is inserted, or allow UARTCL auto-sync from PC to set authoritative epoch. |
| **FreeRTOS HardFault on boot** | Queue saturation or stack overflow | Ensure `CONFIG_GGG_SYSTEM_BUS_TASK_STACK_SIZE >= 512` words in `Kconfig`. |
