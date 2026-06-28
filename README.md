# 🏭 AgnostiLink: Open Wireless Platform for Industrial Monitoring & Automation 
*(HPCL Substation IoT Suite)*

![Status: Active Development](https://img.shields.io/badge/Status-Active_Development-brightgreen)
![Platform: ESP32 & Arduino](https://img.shields.io/badge/Platform-ESP32%20%7C%20Arduino-blue)
![Protocol: CAN & LoRa](https://img.shields.io/badge/Protocol-CAN%20Bus%20%7C%20LoRaWAN-orange)
![Environment: FreeRTOS](https://img.shields.io/badge/Environment-FreeRTOS-yellow)

## 📖 1. Context & Problem Statement
Large industrial facilities—such as oil refineries, supply depots, and solar farms—contain thousands of secondary components (like localized electrical panels, auxiliary motors, and switchgears) that require constant monitoring to prevent breakdowns. 

While expensive, certified vendor SCADA networks are excellent for major, highly critical infrastructure, expanding them to cover these smaller, widespread components is financially impossible due to massive wiring costs and heavy proprietary software licensing fees. Because of these high costs, many secondary assets are left in a complete **"operational blind spot"** with no automated way to track conditions, view live trends, or respond to issues from a distance. 

When field equipment fails in these unmonitored zones (e.g., an overheating transformer bushing or tracking in switchgears due to humidity), it leads to delayed maintenance, extended downtime, and severe potential safety hazards. To solve this problem, we developed **AgnostiLink**, an in-house, low-cost, and highly modular IoT platform designed to be retrofitted into existing electrical panels without disrupting core operations.

## 🎯 2. Main Aim of the Project
To design and deploy a fault-tolerant, modular edge-computing network that bridges raw physical field data to a centralized, air-gapped software dashboard. 

The system relies on a **two-tier communication architecture**:
1. **Field Electronics Layer (Wired Local Bus):** Utilizes microcontrollers (Arduino Nano Core), universal environmental sensors, and the robust **CAN (Controller Area Network)** protocol to gather data safely within high-EMI substation rooms.
2. **Wireless Security Network (Backhaul):** Utilizes long-range **LoRa** hardware modules to securely bridge the collected substation data over long distances to a centralized IT server, entirely independent of existing corporate Wi-Fi or wired IT networks.

### Core Objectives Achieved:
* **Edge-Level Acquisition:** Capture real-time object temperatures (IR sensors), ambient humidity (AHT21B), and relay statuses continuously.
* **Deterministic Fault Tolerance:** Ensure that local hardware (OLED HMI, SD Card backups) continues to function safely even if the wireless connection to the main server drops.
* **True Modularity:** Allow technicians to plug-and-play new sensors into the CAN bus without rewriting the master gateway's firmware.

---

## 🏗️ 3. High-Level System Architecture

```mermaid
graph TD
    subgraph "Level 1: Field Edge (LMPs)"
        LMP1[LMP 14<br/>Arduino Nano + CAN<br/>Group 1: IR Temp]
        LMP2[LMP 22<br/>Arduino Nano + CAN<br/>Group 2: IR Temp + Humidity]
        LMP3[LMP 23<br/>Arduino Nano + CAN<br/>Group 3: 2 IR Temp sensors]
        LMP4[LMP 101<br/>Arduino Nano + CAN<br/>Group 4: Relay/Switches/Actuator]
    end

    subgraph "Level 2: Substation Master(Node)"
        GW[Heltec ESP32-S3 Gateway<br/>FreeRTOS Engine]
        OLED[Local OLED HMI]
        SD[MicroSD Offline Backup]
        
        GW -->|I2C| OLED
        GW -->|HSPI| SD
    end

    subgraph "Level 3: IT & Software (Air-Gapped)"
        SenseCAP[SenseCAP M2<br/>LoRaWAN Gateway]
        Python[Python UDP Listener<br/>Port 1700]
    end

    %% Connections
    LMP1 <==>|Wired CAN Bus 250kbps via HSPI| GW
    LMP2 <==>|Wired CAN Bus 250kbps via HSPI| GW
    LMP3 <==>|Wired CAN Bus 250kbps via HSPI| GW
    LMP4 <==>|Wired CAN Bus 250kbps via HSPI| GW
    
    GW ==>|LoRa IN865 Radio via FSPI| SenseCAP
    SenseCAP ==>|Local Wi-Fi UDP| Python

```
## ✨ 4. Core Features & Engineering Innovations

The AgnostiLink ecosystem was built to survive in unforgiving industrial environments. Standard hobbyist approaches (like running all code in a single `loop()`) fail under refinery conditions. Instead, this system utilizes enterprise-grade embedded architecture.

### 🧠 4.1 Asymmetric Dual-Core Processing (FreeRTOS)
To prevent network bottlenecks and screen freezing, the ESP32-S3 Gateway runs a deeply decoupled FreeRTOS multi-threaded environment:
* **Core 0 (The Nervous System):** Exclusively dedicated to deterministic, real-time CAN bus polling, hardware interrupts, and network discovery. It never waits for slow peripherals.
* **Core 1 (The Brain):** Handles heavy lifting—LoRa AES encryption/transmission, SD Card file I/O operations, and OLED rendering.
* **Thread Safety:** Cores do not share global variables. Data is securely passed between the hardware layer and the transmission layer using **FreeRTOS Queues and Mutex Semaphores**, completely eliminating race conditions and memory corruption.

### 🔌 4.2 Deterministic Auto-Discovery & "Plug-and-Play" Expansion
The system requires zero firmware modifications when the facility expands. It utilizes a **4-Phase Boot Sequence**:
1. **Bus Flooding:** The gateway broadcasts a `CMD_DISCOVER` Opcode (`0x01`).
2. **Identity Reply:** Any connected Local Monitoring Panel (LMP) catches the broadcast and instantly replies with its unique Node ID and **Group ID**.
3. **Sequential ACK:** The gateway verifies stable bidirectional links and locks the roster.
4. **Type-Group Addressing:** The Gateway automatically knows how to parse data based on the Group ID (e.g., Group 1 = IR Temp, Group 2 = IR Temp + Humidity). To expand the substation network, a technician simply wires a new LMP to the CAN bus—the Gateway will automatically discover it, register its sensor profile, and begin logging its data.

### 📡 4.3 Industrial CAN Bus Architecture
Designed to punch through high Electromagnetic Interference (EMI) generated by massive transformers and switchgears.
* **Physical Layer:** Operates at a highly stable **250 KBPS** over twisted-pair Cat6 cabling, fortified by physical **120Ω terminating resistors** to eliminate signal reflection.
* **Bare-Metal Opcodes:** The CAN network does not waste bandwidth transmitting heavy text strings or JSON. It uses a strict, 8-byte hexadecimal Opcode payload system.
* **Hardware Arbitration:** Native to the CAN protocol, if two devices attempt to transmit at the exact same millisecond, the hardware mathematically resolves the collision. The higher-priority frame continues uninterrupted, and the lower-priority **LMP** automatically re-queues its packet. Furthermore, the Master Node is assigned the lowest CAN ID, ensuring that its critical network commands hold the absolute highest priority on the bus.
* **Hardware Buffering:** The MCP2515 controllers feature built-in silicon RX buffers. Even if the ESP32 is busy writing to the SD card, the CAN module physically catches and holds incoming telemetry, ensuring **zero dropped packets**.

### 🛠️ 4.4 Fault Tolerance & Live Bitwise Diagnostics
The system is designed with a "Fail-Safe, Auto-Recover" philosophy:
* **Sensor Hot-Swapping & Live Error Bytes:** LMPs actively monitor their $I^2C$ communication lines via hardware watchdogs. If an environmental sensor is physically unplugged or destroyed, the LMP does not crash. Instead, it utilizes a dedicated **Error Byte** where each bit represents a specific fault status. For example, a sensor failure flips `Bit 0` to `1`. This byte is continuously streamed to the Gateway. As soon as the error is physically cleared and the data stream is restored, `Bit 0` dynamically restores to `0`. (The remaining bits are left open for future diagnostic expansion).
* **SD Card Hot-Unplug Protection:** If the MicroSD card is ejected while the system is live, the FreeRTOS Storage Engine instantly detects the missing hardware, suspends file I/O to prevent a fatal OS panic, alerts the OLED HMI, and continuously polls the SPI bus until a card is re-inserted and successfully remounted.
* **Digital Circuit Breakers:** Code is wrapped in `configASSERT()` checkpoints to instantly catch memory leaks or stack overflows during long-term continuous operation.

### 🔀 4.5 Intelligent SPI Hardware Isolation
The ESP32-S3 contains limited hardware SPI buses. To prevent peripheral collisions, AgnostiLink implements strict bus routing:
* **Bus 1 (FSPI):** Pin-locked and strictly dedicated to the SX1262 LoRa Radio. This prevents the complex, time-sensitive RF modulation from ever being interrupted.
* **Bus 2 (HSPI):** Safely shared between the MCP2515 CAN Controller and the MicroSD Card reader. Using ESP32 SPI Transactions and Chip Select (CS) logic, FreeRTOS seamlessly hands the bus back and forth between network polling and file-saving operations in microseconds.

### 🕹️ 4.6 Bi-Directional Control & "Newbie-Proof" HMI
The system is not just a passive listener; it is a full command-and-control suite.
* **Substation UI:** A built-in 128x64 OLED display utilizing a lag-free, double-buffered rendering engine. It features a 5-button tactile interface (Up, Down, Enter, Back, Home) driven by a strict State Machine.
* **Global Emergency Overrides:** No matter how deep a user navigates into the settings menu to adjust polling intervals, if a critical Level-2 temperature spike occurs on the CAN bus, the UI immediately hijacks the screen to flash a localized Danger Tag.
* **Actuator Downlink:** The network architecture reserves specific Node IDs (161–240) for Actuators. The central IT server can dispatch AES-encrypted command payloads back down the LoRa pipeline. The Gateway decrypts these, translates them into `CMD_ACTUATE` CAN Opcodes, and directs specific LMPs to toggle their onboard relay control circuits—completing the loop from cloud dashboard to physical edge device.

## 🖧 5. The AgnostiLink CAN Protocol (AL-CAN)

At the heart of the field network is a custom, bare-metal implementation of the Controller Area Network (CAN 2.0A) standard. To maximize efficiency and ensure deterministic performance under heavy loads, we bypassed text-heavy protocols (like JSON) and built the **AL-CAN Protocol**: a strict, byte-level quantization and multi-frame assembly standard.

### 📡 5.1 Network Addressing & Hardware Limits
The system uses standard 11-bit CAN Identifiers. Addressing is asymmetrical and physically prioritized. In CAN physics, lower IDs overwrite higher IDs on the electrical wire during collisions.

* **Master Gateway (ESP32-S3):** Hardcoded to **CAN ID `0x00`**. This ensures that network-critical commands (Discovery, Actuation, Resend Requests) possess absolute priority over the bus and cannot be delayed by sensor telemetry.
* **Telemetry LMPs (Sensors):** Assigned CAN IDs from **`0x01` to `0xA0` (1–160)**.
* **Actuator LMPs (Relays/Switches):** Assigned CAN IDs from **`0xA1` to `0xF0` (161–240)**.
* **Network Capacity:** The 1-byte addressing naturally allows for **240 active LMPs** per substation bus. 

---

### 📦 5.2 Dynamic Payload Structure & Multi-Frame Assembly
A fundamental constraint of standard CAN is that a single frame can only hold **8 Bytes** of data. However, our LMPs often generate larger payloads (e.g., dual-phase temperatures, humidity, and error masks). 

To solve this, AL-CAN implements a **Fragmented Multi-Frame Assembly Line** (`LMPAssemblyBuffer`). Instead of cramming data, the protocol dynamically shifts the meaning of the bytes based on the Instruction ID (Opcode). 

#### The Standard AL-CAN 8-Byte Frame Map:
| Byte | Field | Description / Context |
| :--- | :--- | :--- |
| **0** | `TARGET/SENDER_ID` | The Node ID targeted by the Master, or the LMP ID replying (0–240). |
| **1** | `INSTRUCTION_ID` | The Opcode (See 5.3). Defines the exact behavior of the frame. |
| **2** | `CONTEXT_BYTE_1` | Varies: `Packets Left` (0x04) / `Error Mask` (0x06) / `Poll Rate High` (0x07). |
| **3** | `CONTEXT_BYTE_2` | Varies: `Group Type` (0x01) / `Poll Rate Low` (0x07) / `Data Chunk` (0x04). |
| **4** | `DATA_0` | Fragmented payload chunk. |
| **5** | `DATA_1` | Fragmented payload chunk. |
| **6** | `DATA_2` | Fragmented payload chunk. |
| **7** | `DATA_3` | Fragmented payload chunk. |

#### How Data Larger Than 8 Bytes is Handled:
When an LMP sends telemetry (Opcode `0x04`), **Byte 2 acts as a reverse counter (`packetsLeft`)**.
1. The Gateway receives the first frame, sees `packetsLeft > 0`, and opens a 32-byte staging buffer in RAM (`assemblyLine[NodeID]`).
2. It strips Bytes 3 through 7 and writes them into the buffer (`session.bytesWritten`).
3. As subsequent frames arrive, it appends the new data chunks.
4. When a frame arrives with `packetsLeft == 0`, the Gateway locks the buffer, extracts the complete industrial CSV string, and executes the save/transmit logic.

---

### 🎛️ 5.3 The Instruction Set (Opcodes)
Byte 1 acts as the network router. The protocol supports up to 255 distinct commands. The current firmware state machine utilizes the following:

* `0x01` **[CMD_DISCOVER]:** Phase 1 Boot broadcast. Prompts LMPs to reply with their hardware Group Type.
* `0x02` **[CMD_SHIFT_MODE]:** Broadcasted by the Gateway to signal the end of the Discovery Phase and transition the network to Operational Listening.
* `0x04` **[DATA_STREAM]:** The standard multi-frame payload containing continuous sensor telemetry.
* `0x05` **[CMD_REQ_RESEND]:** Gateway command targeting a specific LMP to retransmit a dropped/corrupted multi-frame packet (Triggered by the Assembly Line).
* `0x06` **[CMD_REQ_DIAG]:** Gateway command forcing an LMP to bypass standard polling and instantly report its live Error Mask.
* `0x07` **[CMD_SET_POLL]:** Downlink command to change an LMP's physical polling speed. Bytes 2 & 3 contain the new interval.
* `0x09` **[CMD_PANIC]:** Emergency override broadcasted by an LMP if it detects an immediate hardware or environmental failure (`0xFF` error state).
* `0x0A` **[CMD_GET_POLL]:** Gateway command querying the LMP's current confirmed polling rate to sync the OLED HMI settings.

---

### 🧩 5.4 Group Profiles & Telemetry Parsing
Once the `DATA_STREAM` frames are completely assembled in RAM, the Gateway references the `GROUP_ID` it logged during the `0x01` Discovery Phase to decode the raw bytes. This ensures the Master Gateway never needs to be re-flashed when new LMPs are added.

The system supports up to **255 hardware profiles**. Currently deployed formats:

* **Group 1 (Standard IR Temp):**
  * Parses 4 bytes. 
  * Unpacks as: `OBJ1: XX.X; AMB: XX.X`
* **Group 2 (IR Temp + Humidity):**
  * Parses 5 bytes. 
  * Unpacks as: `OBJ1: XX.X; AMB: XX.X; RH: XX.X%`
* **Group 3 (Dual-Phase IR Temp):**
  * Parses 6 bytes. 
  * Unpacks as: `PHASE_A: XX.X; PHASE_B: XX.X; SHARED_AMB: XX.X`
* **Group 4 (Actuators / Switches):**
  * Parses 1 byte (Hex Mask).
  * Unpacks as: `ACTUATOR_MASK: 0xXX`

---

### 🛡️ 5.5 Multi-Tier Dropped Packet Recovery
Industrial substations generate massive electrical noise, which usually destroys Wi-Fi or I2C signals. The AL-CAN network guarantees data delivery through three layers of physical and software protection:

1. **Hardware Arbitration (CSMA/CR Layer):**
   CAN bus utilizes Carrier Sense Multiple Access with Collision Resolution. If LMP 14 and LMP 22 attempt to transmit telemetry at the exact same millisecond, the MCP2515 transceivers physically negotiate the line. The lower ID wins, and the losing LMP automatically holds its frame in a silicon buffer, re-transmitting the exact microsecond the line goes quiet.
2. **Missing Fragment Detection (Software Layer):**
   If a massive EMI spark destroys a specific fragment of a multi-frame `DATA_STREAM` transmission, the ESP32 Gateway's `LMPAssemblyBuffer` catches the discrepancy via the `expectedNextCount` tracking logic. 
3. **Targeted Resend (`CMD_REQ_RESEND`):**
   Upon detecting a dropped frame, the Gateway suspends parsing, issues Opcode `0x05` to the specific LMP, and increments a `retryCounter`. The LMP is allowed up to `MAX_RETRIES_ALLOWED` (3 attempts) to complete the multi-frame transfer before the Gateway flushes the corrupted buffer and moves on to the next task.
