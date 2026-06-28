# 🏭 Industrial Substation IoT Monitoring & Control Suite

![Status: Active Development](https://img.shields.io/badge/Status-Active_Development-brightgreen)
![Platform: ESP32 & Arduino](https://img.shields.io/badge/Platform-ESP32%20%7C%20Arduino-blue)
![Protocol: CAN & LoRa](https://img.shields.io/badge/Protocol-CAN%20Bus%20%7C%20LoRaWAN-orange)
![Environment: FreeRTOS](https://img.shields.io/badge/Environment-FreeRTOS-yellow)

## 📖 1. Context & Problem Statement
In high-stakes industrial environments like the HPCL Mumbai Refinery, electrical substations and high-power motor control centers operate under extreme conditions (heat, dust, and high Electromagnetic Interference (EMI)). 

Existing SCADA systems are often legacy, expensive to expand, or completely offline. When field equipment fails (e.g., an overheating transformer bushing or a tripped Vacuum Circuit Breaker), the lack of real-time edge monitoring leads to delayed maintenance, extended downtime, and potential safety hazards. 

There is a critical need for a **low-cost, highly modular, and ruggedized IoT system** that can be retrofitted into existing electrical panels without disrupting core operations.

## 🎯 2. Main Aim of the Project
To design and deploy a fault-tolerant, modular edge-computing network that bridges raw physical field data to a centralized, air-gapped software dashboard. 

The system is designed with a **two-tier communication architecture**:
1. **Wired Local Bus:** Utilizes the robust **CAN (Controller Area Network)** protocol to gather data from various modular Local Monitoring Panels (LMPs) safely within a high-EMI substation room.
2. **Wireless Backhaul:** Utilizes **LoRa (Long Range Radio)** to securely bridge the collected substation data over long distances to a centralized IT server, entirely independent of existing corporate Wi-Fi or wired IT networks.

### Core Objectives:
* **Predictive Maintenance:** Capture real-time object temperatures (IR sensors), ambient humidity (AHT21B), and relay statuses to catch failures before they happen.
* **Deterministic Fault Tolerance:** Ensure that local hardware (OLED HMI, SD Card backups) continues to function safely even if the wireless connection to the main server drops.
* **True Modularity:** Allow future technicians to plug-and-play new sensors into the CAN bus without rewriting the master gateway's firmware.

---

## 🏗️ 3. High-Level System Architecture

*(GitHub will automatically render this diagram)*

```mermaid
graph TD
    subgraph "Level 1: Field Edge (LMPs)"
        LMP1[LMP Node 14<br/>Arduino Nano + CAN<br/>Group 1: IR Temp]
        LMP2[LMP Node 22<br/>Arduino Nano + CAN<br/>Group 2: HVAC/AHT21B]
        LMP3[LMP Node 101<br/>Arduino Nano + CAN<br/>Group 4: Relay/Switches]
    end

    subgraph "Level 2: Substation Master"
        GW[Heltec ESP32-S3 Gateway<br/>FreeRTOS Engine]
        OLED[Local OLED HMI]
        SD[MicroSD Offline Backup]
        
        GW -->|SPI 2| OLED
        GW -->|HSPI| SD
    end

    subgraph "Level 3: IT & Software (Air-Gapped)"
        SenseCAP[SenseCAP M2<br/>LoRaWAN Gateway]
        Python[Python UDP Listener<br/>Port 1700]
        Dash[AI / Dashboard Server<br/>Local Database]
    end

    %% Connections
    LMP1 <==>|Wired CAN Bus 250kbps| GW
    LMP2 <==>|Wired CAN Bus 250kbps| GW
    LMP3 <==>|Wired CAN Bus 250kbps| GW
    
    GW ==>|LoRa IN865 Radio| SenseCAP
    SenseCAP ==>|Local Wi-Fi UDP| Python
    Python ==> Dash
