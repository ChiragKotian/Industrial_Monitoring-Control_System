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
        LMP3[LMP 23<br/>Arduino Nano + CAN<br/>Group 2: 2 IR Temp sensors]
        LMP4[LMP 101<br/>Arduino Nano + CAN<br/>Group 4: Relay/Switches/Actuator]
    end

    subgraph "Level 2: Substation Master(Node)"
        GW[Heltec ESP32-S3 Gateway<br/>FreeRTOS Engine]
        OLED[Local OLED HMI]
        SD[MicroSD Offline Backup]
        
        GW -->|SPI 2| OLED
        GW -->|HSPI| SD
    end

    subgraph "Level 3: IT & Software (Air-Gapped)"
        SenseCAP[SenseCAP M2<br/>LoRaWAN Gateway]
        Python[Python UDP Listener<br/>Port 1700]
    end

    %% Connections
    LMP1 <==>|Wired CAN Bus 250kbps| GW
    LMP2 <==>|Wired CAN Bus 250kbps| GW
    LMP3 <==>|Wired CAN Bus 250kbps| GW
    
    GW ==>|LoRa IN865 Radio| SenseCAP
    SenseCAP ==>|Local Wi-Fi UDP| Python
