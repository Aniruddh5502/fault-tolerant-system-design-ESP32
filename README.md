# ESP32 Triple OTA with TMR Fault Tolerance

*A robust embedded firmware system for ESP32 that implements **three-way OTA partition management** with automatic corruption detection, self-repair, and **two-layer Triple Modular Redundancy (TMR)** for critical data and arithmetic operations.*

*This project demonstrates an attempt to industrial-grade approach to firmware resilience on ESP32. It uses three OTA partitions (`ota_0`, `ota_1`, `ota_2`) with runtime health monitoring. If any partition becomes corrupted (e.g., by bit flips, incomplete writes, or fault injection), the system automatically repairs it by copying from a known-good partition. Additionally, the firmware implements software-based TMR for sensor data and arithmetic operations to tolerate single-bit errors or soft faults in memory.*

The system is designed for:

- **Industrial IoT** (long unattended operation)
- **Safety-critical systems** (fail-operational requirements)
- Target *Space/radiation* prone environments related systems as future plan with standards implementations
- Not currently recomended for space related operations.

## Features

### 1. Triple OTA Partition Management

- Three application partitions scanned at boot and periodically
- SHA-256 hash validation using `esp_partition_get_sha256()`
- Automatic repair of invalid partitions from a valid source
- Smart boot partition selection with round-robin fallback
- Runtime corruption detection (periodic scans every 5 seconds)

### 2. Fault Injection Test Mode (Optional)

- Randomly corrupts any OTA partition
- Safe corruption of running partition (writes last sector only)
- Automatic reboot after running partition corruption
- Configurable probability (50% for running vs. non-running)

### 3. Two-Layer Triple Modular Redundancy (TMR)

- **9-copy storage** (3×3 matrix) for critical variables
- Two-stage majority voting:
    - Layer 1: Majority per row (3 rows → 3 candidates)
    - Layer 2: Majority of row results → final value
- TMR-wrapped arithmetic: `add()`, `sub()`, `mul()`, `div()`
- Median fallback when no exact majority (protects against two divergent values)

### 4. Watchdog Protection

- Task Watchdog Timer (30-second timeout)
- Reset on every long operation (partition copy chunks)
- Prevents system hangs during flash operations

### 5. Build & Flash Automation (Python)

- Compiles three identical firmware binaries
- Reads partition offsets from CSV
- Flashes all three OTA slots in one command
- Automatic `boot_app0.bin` and partition table location


```text
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 Flash Layout                       │
├─────────────┬─────────────┬─────────────┬───────────────────┤
│   ota_0     │   ota_1     │   ota_2     │  meta (SPIFFS)    │
│  (0x10000)  │ (0x150000)  │ (0x290000)  │    (0x3D0000)     │
│   Size:     │   Size:     │   Size:     │    Size: 0x4000   │
│   0x140000  │   0x140000  │   0x140000  │                   │
└─────────────┴─────────────┴─────────────┴───────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    Firmware Tasks                           │
├─────────────────────────────────────────────────────────────┤
│ firmwareTask (prio 5)  → Partition scan & repair loop       │
│ baseOperation (prio 4) → TMR sensor & math demo             │
│ faultInjection (prio 1) → Optional test corruption          │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                 TMR Data Flow (var class)                   │
├─────────────────────────────────────────────────────────────┤
│  Write: value → 9 copies (3x3 array)                        │
│  Read:  9 copies → Row majorities (3 values) → Final value  │
└─────────────────────────────────────────────────────────────┘
```

## Hardware Requirements

|Component|Specification|
|---|---|
|**MCU**|ESP32 (any variant with ≥4MB flash)|
|**Flash**|Minimum 4MB (for 3× OTA + metadata)|
|**UART**|USB-to-Serial (for flashing & serial monitor)|
|**Power**|3.3V or 5V via USB|

## Software Requirements

|Tool|Version|Purpose|
|---|---|---|
|Arduino CLI|0.35+|Compilation|
|Python|3.8+|Build automation script|
|esptool.py|5.2+|Flashing (bundled with ESP32 core)|
|ESP32 core|3.0.0+|Arduino-ESP32 support|
## Installation & Setup

### 1. Clone Repository

```bash
git clone <repository-url>
cd <project-folder>
```

### 2. Directory Structure (Expected)

```bash
project/
├── firmware_01/
│   └── firmware_01.ino          (main firmware - copy to all three)
├── firmware_02/                 (identical copy of firmware_01.ino)
│   └── firmware_02.ino
├── firmware_03/                 (identical copy)
│   └── firmware_03.ino
├── scripts/
│   ├── partitions.csv           (partition table)
│   └── main.py                  (build & flash script)
└── README.md
```

### 3. Configure Paths in `main.py`

Edit these variables to match your system:

```python
ARDUINO_CLI = "arduino-cli.exe"          # Path to arduino-cli
PORT = "COM3"                            # ESP32 serial port
ESPTOOL = r"C:\Users\...\esptool.exe"   # Full path to esptool
```

### 4. Run Build and Flash

```bash
cd scripts
python main.py
```


The script will:
- Clear old build folders
- Copy `partitions.csv` to each firmware folder
- Compile all three firmwares (same code, three binaries)
- Flash all three OTA slots + partition table + bootloader data

### 5. Monitor Output

```bash
arduino-cli monitor -p COM3 -b esp32:esp32:esp32
```

Expected output sample
[[LOG]]

## How It Works

### Boot-Time Flow

1. **Watchdog initialization** (30 seconds)
2. **Scan all three OTA partitions** – check SHA-256 validity
3. **Repair invalid partitions** (except running one) from first valid source
4. **Check running partition validity** – if invalid, switch to another valid partition and reboot
5. **Enter main loop** – periodic scans & repairs every 5 seconds

### Partition Repair Mechanism

- Erases destination partition
- Copies in 4KB chunks (to avoid watchdog timeout)
- Verifies copy with SHA-256
- Only repairs _non-running_ invalid partitions


### TMR Implementation (`var` class)

```cpp
// Store value with 9x redundancy
sensor_reading.write(42.0);

// Retrieve with two-stage voting
float value = sensor_reading.read();

// TMR arithmetic
float sum = math::add(a, b);   // computes a+b 9 times, then votes
```


The redundancy protects against:
- Memory bit flips (in `.bss` or heap)
- Stack corruption
- Compiler optimization errors (volatile + compiler barriers)



## Pros

|Aspect|Benefit|
|---|---|
|**Self-healing**|Automatically repairs corrupted OTA partitions without external intervention|
|**Triple redundancy**|Can tolerate up to 2 corrupted OTA partitions (if at least one remains valid)|
|**TMR for data**|Protects critical variables from single-bit errors in RAM|
|**TMR for arithmetic**|Operations are computed 9 times, voted – immune to transient CPU faults|
|**Fault injection**|Built-in test mode validates recovery logic|
|**No external dependencies**|All logic runs on ESP32; no cloud or additional hardware|
|**Deterministic boot**|SHA-256 validation ensures only valid firmware executes|
|**Watchdog protection**|Prevents hangs during flash erase/write cycles|
