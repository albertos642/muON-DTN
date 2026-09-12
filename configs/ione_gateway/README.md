# IONe UART-COBS Gateway Configuration for muON-DTN

This directory provides a ready-to-run configuration to operate **IONe** (NASA-JPL Interplanetary Overlay Network, experimental branch with `uartcl-cobs` CLA) as a DTN Gateway on a PC, server, or Raspberry Pi communicating with a microcontroller running **muON-DTN**.

---

## 1. Architectural Overview

```
+------------------------------------+          COBS-Encoded Serial/USB          +------------------------------------+
|       muON-DTN (Node 1)            | ----------------------------------------> |        IONe Gateway (Node 3)       |
|                                    | <---------------------------------------- |                                    |
| - Adafruit Feather M0 / ESP32      |        [0x00] [Frame] [CRC16] [0x00]      | - Linux / Raspberry Pi / WSL2      |
| - BPA (RFC 9171 / RFC 8949)        |                                           | - BPv7 / SDR Database              |
| - UARTCL-COBS Convergence Layer    |   Baud: 115200 bps                        | - uartcobscli (Induct)             |
| - Application Endpoint: ipn:1.10   |   Sync: 3B Request -> 7B Response         | - uartcobsclo (Outduct)            |
|   (e.g., OLED Display / Telemetry) |                                           | - Gateway Endpoint: ipn:3.1        |
+------------------------------------+                                           +------------------------------------+
```

---

## 2. Prerequisites & Building IONe

Compile and install `ione-uartcl-cobs` on your host machine (Linux, Raspberry Pi OS, or WSL2):

```bash
cd /path/to/workspace_muON_refactoring/ione-uartcl-cobs
./configure
make -j$(nproc)
sudo make install
sudo ldconfig
```

Verify that `uartcobscli` and `uartcobsclo` are installed:
```bash
which uartcobscli uartcobsclo ionadmin bpadmin ipnadmin
```

---

## 3. Serial Port Setup & Permissions

Connect your microcontroller via USB. Identify the serial device:
```bash
# Common Linux/Raspberry Pi device nodes:
ls -l /dev/ttyUSB* /dev/ttyACM*
```

Grant read/write permissions to your user:
```bash
sudo usermod -aG dialout $USER
# Or temporary direct chmod:
sudo chmod 666 /dev/ttyUSB0
```

---

## 4. Starting the Gateway

Run the startup script:
```bash
cd configs/ione_gateway
chmod +x start_gateway.sh stop_gateway.sh

# Default uses /dev/ttyUSB0 @ 115200:
./start_gateway.sh

# Or specify a different port/baudrate:
./start_gateway.sh /dev/ttyACM0 115200
```

The script performs:
1. `ionadmin node_gateway.ionrc`: Allocates SDR memory and establishes contact/range plans between Node 3 and Node 1.
2. `bpadmin node_gateway.bprc`: Registers the `uartcobs` protocol and spawns `uartcobscli` (Induct) and `uartcobsclo` (Outduct).
3. `ipnadmin node_gateway.ipnrc`: Adds the egress routing plan routing bundles addressed to Node 1 via the UART-COBS CLA.

---

## 5. Exchanging BPv7 Bundles

### A. Receiving Bundles from Microcontroller on PC
Start a receiver daemon on endpoint `ipn:3.1`:
```bash
# Option 1: Save incoming payloads to files
bprecvfile ipn:3.1

# Option 2: Dump payload stream to terminal
bpsink ipn:3.1
```

When muON transmits sensor telemetry (e.g., from the BME280 sensor plugin or application task), the bundle will be deframed, CRC-verified, delivered to ION's SDR, and saved by `bprecvfile`.

### B. Sending Bundles from PC to Microcontroller
Create a test message:
```bash
echo '{"cmd":"DISPLAY","text":"Hello from IONe!","alert":true}' > test_msg.json
```

Send to the muON application endpoint (Node 1, Service 10):
```bash
# bpsendfile <source_eid> <destination_eid> <filepath> [class-of-service]
bpsendfile ipn:3.1 ipn:1.10 test_msg.json
```

The outduct `uartcobsclo` will encode the bundle into a COBS frame with CRC-16 CCITT and transmit it over serial. The muON UARTCL-COBS layer will receive, verify CRC, and deliver the payload directly to the OLED display or application listener.

---

## 6. Time Synchronization

muON microcontrollers can query IONe for network time:
- muON issues a 3-byte timesync request frame (`[0x18, CRC_H, CRC_L]`).
- IONe's `receiveFrameByUartCobs` detects `UARTCOBS_FLAG_SYNC`, extracts the current ION DTN Epoch timestamp (seconds since 2000-01-01 00:00:00 UTC), and responds with a 7-byte frame (`[0x18, T3, T2, T1, T0, CRC_H, CRC_L]`).
- muON updates its internal RTC (e.g. DS3231) and emits `MUON_EVT_TIME_SYNC`.

---

## 7. Persistent Serial Port Device Names (udev rule)

To prevent device node renumbering (e.g. `/dev/ttyACM0` jumping to `/dev/ttyACM1` upon MCU reboot), create a persistent symlink based on the USB serial or vendor/product ID:

1. Identify your microcontroller's USB attributes:
   ```bash
   udevadm info -a -n /dev/ttyACM0 | grep -E "idVendor|idProduct|serial" | head -n 3
   ```
   *(For Adafruit Feather M0: `idVendor="239a"`, `idProduct="800b"`)*

2. Create a udev rule in `/etc/udev/rules.d/99-muon-node.rules`:
   ```bash
   sudo bash -c 'cat << "EOF" > /etc/udev/rules.d/99-muon-node.rules
   SUBSYSTEM=="tty", ATTRS{idVendor}=="239a", ATTRS{idProduct}=="800b", SYMLINK+="muon_node_b", MODE="0666"
   EOF'
   ```

3. Reload udev rules:
   ```bash
   sudo udevadm control --reload-rules && sudo udevadm trigger
   ```

4. You can now reliably launch the gateway with:
   ```bash
   ./start_gateway.sh /dev/muon_node_b 115200
   ```

---

## 8. Live Diagnostics & Activity Watching

The gateway configuration enables real-time BP activity watching (`w 1` in `node_gateway.bprc`).

To inspect ION activity live while bundles are being transferred:
```bash
tail -f ion.log
```

### Watch Characters Legend:
- `a`: **Bundle Acquired** — bundle successfully received and validated by induct `uartcobscli`.
- `d`: **Bundle Delivered** — bundle delivered to local endpoint (e.g. `ipn:3.1` sink).
- `b`: **Bundle Queued** — bundle enqueued for transmission on an outduct.
- `c`: **Bundle Transmitted** — bundle successfully transmitted over outduct.
- `e`: **Bundle Expired** — bundle discarded because its expiration time was reached.
- `y`: **Bundle Refused** — bundle rejected by admission control.
- `z`: **Bundle Discarded** — bundle discarded due to storage or malformation.

---

## 9. Stopping the Gateway

To cleanly terminate all ION daemons and release shared memory resources:
```bash
./stop_gateway.sh
```

