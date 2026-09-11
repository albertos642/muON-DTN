# muON-DTN: Microcontroller Delay-Tolerant Network Stack

An experimental, zero-malloc Delay-Tolerant Networking (DTN) communication architecture and Bundle Protocol stack designed for deeply constrained microcontroller platforms and extreme IoT environments.

---

## Abstract and Overview

The **muON-DTN** network architecture addresses the challenges of operating Delay/Disruption-Tolerant Networks (DTN) on resource-constrained embedded nodes (e.g. 32-bit ARM Cortex-M0+ microcontrollers operating with 32 KB of RAM and 256 KB of Flash memory). In challenging environments characterized by intermittent connectivity, asymmetric bandwidth, significant propagation delays, and severe energy constraints, conventional TCP/IP protocol suites fail.

While standard implementations of the Bundle Protocol version 7 (BPv7, RFC 9171) assume workstation-class memory hierarchies and dynamic memory allocators, muON-DTN enforces strict deterministic execution and a **Zero Dynamic Allocation (Zero-Malloc)** discipline. By decoupling bundle control headers from payload streams and streaming data directly across non-volatile or block-managed storage media, muON-DTN achieves robust store-carry-and-forward operation without risking heap fragmentation or buffer exhaustion.

muON-DTN operates as an application-level protocol stack executing atop the modular, event-driven **GGG framework** (or compatible embedded abstraction layers), which provides underlying RTOS scheduling, static block storage, and peripheral access.

---

## Architectural Overview

```
+-------------------------------------------------------------------------+
|                       Application Layer / Sensors                       |
|           (ipn:1.1 Telemetry Producer / ipn:2.1 Data Consumer)          |
+----------------------------------------------------+--------------------+
                                                     |
+----------------------------------------------------v--------------------+
|                   Bundle Protocol Agent (BPA - RFC 9171)               |
|  - In-Stream CBOR Serializer / Deserializer (RFC 8949)                  |
|  - Storage-Centric Data Path (StorageHandle_t Zero-Copy Transfers)      |
|  - Static O(1) Metadata & Custody Table (Lifetime, Priorities, Retries) |
+----------------------------------------------------+--------------------+
                                                     |
+----------------------------------------------------v--------------------+
|                       Static Routing Engine                             |
|  - O(1) IPN Destination-to-Link Lookup Table                           |
|  - Default Gateway Fallback & Local Delivery Resolution                 |
+----------------------------------------------------+--------------------+
                                                     |
+----------------------------------------------------v--------------------+
|                  Convergence Layer Manager (CLM)                        |
|  - Link Dispatcher and Registration Interface (IConvergenceLayer)      |
+--------------------------+------------------------------+---------------+
                           |                              |
+--------------------------v---------------+  +-----------v---------------+
|        LoRa Convergence Layer            |  |    UART-COBS Conv. Layer  |
|  - Bitwise Polymorphic Headers           |  |  - COBS Stream Framer     |
|  - Multi-Segmentation (up to 256 segs)   |  |  - CRC-16 CCITT-FALSE     |
|  - Service Classes (Unreliable/Notified) |  |  - Stream-to-Storage with |
|  - ETSI 1% EU868 Duty Cycle Token Bucket |  |    Atomic Rollback        |
|  - Zero-Malloc RadioLib Driver           |  +---------------------------+
+------------------------------------------+
```

### 1. Bundle Protocol Agent (BPA) & Stream-Based CBOR Engine

- **Storage-Centric Data Path**: Bundles in transit are never buffered in whole within volatile RAM. The `SystemBus` transports opaque 32-bit storage identifiers (`StorageHandle_t`), while the `CBORSerializer` reads and writes payload bytes progressively through `ggg::hal::IInputStream` and `ggg::hal::IOutputStream` wrappers directly against storage blocks.
- **RFC 8949 Stream Deserialization**: The CBOR engine inspects and decodes the BPv7 Primary Block (Destination EID, Source EID, Report-to EID, Creation Timestamp, Lifetime, and Extension Blocks) on the fly, immediately committing metadata and halting parsing at the boundary of the payload block without allocating RAM.
- **Static Metadata Table**: Manages active bundles in a fixed-size array (`BundleMetadataTable`), tracking hop limits, custody state, retransmission counts, and expiration without dynamic allocation.

### 2. Static Routing Engine

- Provides $O(1)$ routing table lookups translating destination node numbers (in the `ipn:node.service` scheme) to Convergence Layer link adapters.
- Configurable default gateway routes handle non-local endpoints, while loopback detection routes locally addressed bundles directly to the local application delivery pipeline.

### 3. Convergence Layer Manager (CLM)

- Manages modular link adapters implementing the `muon::clm::IConvergenceLayer` interface.
- Subscribes to `MUON_EVT_ROUTE_REQ` on the system bus and coordinates link transmission based on destination routing tags and quality of service (QoS).

### 4. LoRa Convergence Layer (`lora_cl`)

- **Polymorphic Physical Headers**:
  - **Segment Header (3 bytes)**: Packed bitfields encoding Frame Type (`00`), Service Class (`SC`), Segment Index, Total Segment Count (supporting up to 256 fragments), and Segment Flags (e.g. `LAST_SEG`).
  - **Block-ACK Header (1 byte)**: Frame Type (`01`), SubType, and 4-bit `ACK_ID`.
  - **Refuse / NACK Header (2 bytes)**: Frame Type (`10`), SubType, `ACK_ID`, and 8-bit Reason Code (`0x01` Out of Memory, `0x02` Duty Cycle Exceeded, `0x03` Invalid Segment, `0x04` Sequence Error).
- **Service Classes**:
  - *Unreliable* (SC=0): Opportunistic fire-and-forget transmission.
  - *Notified* (SC=1): Stop-and-wait fragment acknowledgment using Block-ACKs and retransmissions.
- **ETSI EN 300 220 1% EU868 Duty Cycle Token Bucket**:
  - Continuous duty cycle monitoring enforcing the 1% hourly transmission budget (36,000 ms per 3,600 s) on the 868.1 MHz ISM band.
  - Analytical Time-on-Air (ToA) model factoring in Spreading Factor (SF7-SF12), Bandwidth (125/250/500 kHz), Coding Rate (4/5-4/8), and explicit preamble lengths.
- **Zero-Malloc RadioLib Driver**:
  - Embeds Semtech radio abstractions (`SX1276`, `SX1278`, `SX1272`, `SX1262`) directly as class members, avoiding dynamic heap allocation.

### 5. UART-COBS Convergence Layer (`uart_cobs_cl`)

- **RFC 1662 & Consistent Overhead Byte Stuffing (COBS)**: Robust serial frame delimitation using `0x00` bytes, version identifier `0x10`, and flags (`0x00` DATA, `0x08` SYNC).
- **CRC-16 CCITT-FALSE**: Frame integrity validation using polynomial `0x1021` with a precomputed 256-entry lookup table.
- **Stream-to-Storage with Rollback**:
  - Incoming serial stream bytes are decoded from COBS directly into `IStorage` via transactional `beginWrite`.
  - If the computed CRC matches the frame trailer upon encountering a delimiter, `commitWrite` finalizes the bundle and triggers `MUON_EVT_RX_READY`.
  - If framing errors, buffer overruns, or CRC mismatches occur, `abortWrite` is executed immediately, discarding partial fragments and guaranteeing zero memory leaks.

---

## Two-Node Testbed Guide: LoRa Bundle Exchange

This section provides a complete, reproducible walkthrough for deploying and validating an end-to-end DTN communication link between two **Adafruit Feather M0 RFM95 LoRa** microcontrollers (Node A and Node B).

### Hardware Requirements

1. **2x Adafruit Feather M0 with RFM95 LoRa** (Atmel ATSAMD21G18A ARM Cortex-M0+, SX1276 transceiver, 868 MHz).
2. **2x LoRa Antennas** (e.g. 868 MHz wire antennas cut to 8.2 cm, or SMA antennas connected to the uFL/SMA pads).  
   *Caution*: Never transmit with a LoRa radio without an attached antenna to avoid damaging the RF power amplifier.
3. **2x Micro-USB to USB-A Cables**.
4. **Host Workstation** running Linux, macOS, or Windows.

### Software Prerequisites

Install Python 3.8+ and PlatformIO Core:

```bash
pip install platformio
pip install kconfiglib windows-curses   # Windows
# or: pip install kconfiglib curses     # Linux/macOS
```

---

### Step 1: Workspace Setup and Repository Cloning

muON-DTN consumes the GGG framework as a modular dependency. Clone both repositories into the same parent workspace directory:

```bash
# Create a dedicated workspace directory
mkdir dtn_workspace && cd dtn_workspace

# Clone GGG (framework layer)
git clone https://github.com/albertos642/ggg.git

# Clone muON-DTN (network application layer)
git clone https://github.com/albertos642/muON-DTN.git

# Navigate to muON-DTN
cd muON-DTN
```

The directory structure must be:
```
dtn_workspace/
├── ggg/
└── muON-DTN/
```

---

### Step 2: Verification of Native Unit Tests

Before flashing physical hardware, execute the automated desktop test suite to verify the protocol state machines, CBOR encoding, and mock modem reassembly:

```bash
pio test -e native
```

All 32 test cases across `test_bpa`, `test_clm_routing`, `test_convergence_layers`, and `test_integration` should report `[PASSED]`.

---

### Step 3: Configuring and Flashing Node A (Transmitter, `ipn:1.1`)

Node A is configured to act as an autonomous sensor node that periodically produces BPv7 telemetry bundles addressed to `ipn:2.1`.

1. **Configure Node A**:
   Create or edit the `.config` file in the `muON-DTN/` root directory to activate Node Role A:

   ```ini
   CONFIG_MUON_NODE_ROLE_A=y
   # CONFIG_MUON_NODE_ROLE_B is not set
   CONFIG_MUON_LORA_CL_ENABLED=y
   CONFIG_MUON_LORA_MODEM_SX1276=y
   ```

   *(Optional: You may also run `pio run -t menuconfig` to set the role interactively).*

2. **Connect Board A**:
   Connect the first Adafruit Feather M0 board via USB. Identify its serial port (e.g. `COM3` on Windows or `/dev/ttyACM0` on Linux).

3. **Build and Upload to Node A**:
   ```bash
   pio run -e adafruit_feather_m0 -t upload
   ```

4. **Monitor Node A Output**:
   Open a serial monitor at 115200 baud:
   ```bash
   pio device monitor -b 115200
   ```
   You should observe the boot sequence, FreeRTOS scheduler initialization, and periodic bundle production:
   ```
   [SYS] muON-DTN Firmware Starting...
   [ROLE] Initialized as Node A (EID: ipn:1.1)
   [APP] Generating telemetry bundle -> ipn:2.1
   [BPA] Serializing BPv7 Primary Block to Storage Handle 0x0001
   [CLM] Routing to Link 1 (LoRaCL)
   [LORA] Transmitting 1 segments over RFM95W (ToA: 46 ms)...
   [LORA] TX Done. Waiting next duty cycle window.
   ```

---

### Step 4: Configuring and Flashing Node B (Receiver, `ipn:2.1`)

Node B is configured to listen continuously over LoRa, receive physical segments, reconstruct bundle payloads in static storage, and deliver verified payloads to local application endpoints.

1. **Disconnect Board A and Connect Board B**:
   Unplug Board A (or keep it powered via an external USB power bank / battery) and connect Board B to the host workstation.

2. **Configure Node B**:
   Edit the `.config` file in the `muON-DTN/` root directory to switch to Node Role B:

   ```ini
   # CONFIG_MUON_NODE_ROLE_A is not set
   CONFIG_MUON_NODE_ROLE_B=y
   CONFIG_MUON_LORA_CL_ENABLED=y
   CONFIG_MUON_LORA_MODEM_SX1276=y
   ```

3. **Build and Upload to Node B**:
   ```bash
   pio run -e adafruit_feather_m0 -t upload
   ```

4. **Monitor Node B Output**:
   Open the serial monitor for Board B:
   ```bash
   pio device monitor -b 115200
   ```
   You should observe:
   ```
   [SYS] muON-DTN Firmware Starting...
   [ROLE] Initialized as Node B (EID: ipn:2.1)
   [LORA] Modem SX1276 in continuous RX mode (868.1 MHz, SF7, BW125)
   ```

---

### Step 5: Observing Over-the-Air Bundle Exchange

1. Power both boards simultaneously (Board A transmitting, Board B receiving).
2. On Board B's serial monitor, observe incoming packet reception:
   ```
   [LORA] IRQ: Packet received (len=38 bytes, RSSI=-64 dBm, SNR=9.2 dB)
   [LORA] Segment 0/1 validated. Reassembly complete -> Storage Handle 0x0001.
   [SYS_BUS] Event MUON_EVT_RX_READY published.
   [BPA] Deserializing bundle header from storage...
   [BPA] Primary Block: Source=ipn:1.1, Destination=ipn:2.1, Lifetime=3600s
   [BPA] Destination matches local node. Delivering bundle!
   [SYS_BUS] Event MUON_EVT_BUNDLE_DELIVERED published.
   [APP] Local service 1 received 10 bytes payload: "muON-PING"
   ```
3. If fragment corruption or checksum mismatch is artificially induced, observe the atomic rollback mechanism:
   ```
   [LORA] CRC or segment integrity check failed. Aborting storage transaction.
   [HAL] RamStorage: abortWrite executed on Handle 0x0001. Zero leaked blocks.
   ```

---

## Status and Scientific Attribution

> **Notice**: This software is an active academic Work in Progress (WIP). Protocol formats, internal state machines, and API definitions are subject to evolution and revision without prior notice.

- **Author**: Alberto Soncini (`alberto.soncini3@studio.unibo.it`)
- **Academic Supervisor**: Prof. Carlo Caini (`carlo.caini@unibo.it`)
- **Affiliation**: Department of Computer Science and Engineering (DISI), Alma Mater Studiorum - Università di Bologna, Italy.
- **Copyright**: Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
