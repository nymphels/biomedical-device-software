# 💉 MedFusion OS — Medical Infusion Pump Firmware Simulation

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Arduino Compatible](https://img.shields.io/badge/Arduino-Compatible-teal.svg)](https://www.arduino.cc/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-Desktop%20%7C%20Arduino-lightgrey.svg)]()
[![IEC 62304](https://img.shields.io/badge/Standard-IEC%2062304-red.svg)]()

> A production-grade firmware simulation for a single-channel peristaltic IV infusion pump, written in object-oriented C++17. Designed to run on both a standard desktop compiler (GCC/Clang) and an Arduino-compatible embedded board with minimal porting effort.

---

## 🏥 Biomedical Context

An **infusion pump** is a regulated medical device (FDA Class II/III, per 21 CFR Part 880) that mechanically delivers precise volumes of intravenous (IV) fluids, medications, or nutrients directly into a patient's bloodstream. They are among the most critical devices in clinical environments — found in ICUs, operating theatres, oncology wards, and ambulatory care.

### Why firmware quality is life-critical

| Failure Mode | Clinical Consequence |
|---|---|
| Over-infusion (excess flow rate) | Fluid overload, drug toxicity, cardiac arrest |
| Under-infusion (occlusion undetected) | Under-dosing of vasopressors, antibiotics, or insulin |
| Alarm failure | Operator unaware of completed infusion or malfunction |
| Software hang / watchdog miss | Pump continues running indefinitely |

This project demonstrates the core firmware architecture required to mitigate these risks through validated state machine transitions, independent safety interlocks, and a structured alarm system aligned with **IEC 60601-1-8** (Medical Electrical Equipment — Alarm Systems).

---

## ✨ Features

| Feature | Details |
|---|---|
| **OOP Architecture** | Six distinct C++ classes with clear single responsibilities |
| **Dual-platform** | Runs unmodified on desktop (GCC/Clang) and Arduino (ATmega328P+) |
| **State Machine** | 6-state FSM (IDLE → RUNNING → ALARM / COMPLETE / PAUSED / FAULT) with validated transitions |
| **Flow Rate Sensor** | Potentiometer → 10-bit ADC → mL/h with moving-average noise filter |
| **Drop Sensor** | Opto-interrupter simulation; drop counting → volume (mL); Arduino ISR-ready |
| **Safety Interlock 1** | Flow rate > 200 mL/h → immediate ALARM state + buzzer + LED |
| **Safety Interlock 2** | Volume delivered ≥ VTBI target → COMPLETE state + alarm |
| **Occlusion Detection** | Cross-checks commanded vs. drop-derived flow rate; flags >90% discrepancy |
| **Alarm Controller** | Non-blocking 400 ms blink/beep pattern per IEC 60601-1-8 medium-priority spec |
| **Audit Logger** | Structured, timestamped event log (INFO / WARNING / ERROR / CRITICAL) |
| **Software Watchdog** | Detects main-loop stalls; transitions to FAULT state |
| **Display Layer** | 20-character-wide panel with progress bar, ETA, and ANSI color alarms |
| **Hardware Simulator** | Deterministic random sensor inputs with an injected high-flow spike scenario |

---

## 🏗️ Architecture

```
┌───────────────────────────────────────────────────────────┐
│                      InfusionPump                         │
│                                                           │
│  ┌─────────────────┐   ┌─────────────────┐               │
│  │  FlowRateSensor  │   │   DropSensor    │               │
│  │  Potentiometer   │   │  Opto-interrupt │               │
│  │  ADC → mL/h      │   │  Drops → mL    │               │
│  └────────┬─────────┘   └───────┬─────────┘               │
│           │                     │                         │
│  ┌────────▼─────────────────────▼─────────────────────┐   │
│  │               PumpStateMachine                      │   │
│  │                                                     │   │
│  │   IDLE ──► RUNNING ──► ALARM ──► IDLE (ack)        │   │
│  │                │                                   │   │
│  │                ├──► PAUSED ──► RUNNING             │   │
│  │                │                                   │   │
│  │                ├──► COMPLETE                       │   │
│  │                │                                   │   │
│  │                └──► FAULT (terminal)               │   │
│  └──────────────────────────┬──────────────────────────┘  │
│                             │                             │
│  ┌──────────────┐  ┌────────▼────────┐  ┌─────────────┐  │
│  │ AlarmControl │  │   PumpDisplay   │  │ AuditLogger │  │
│  │ Buzzer + LED │  │  LCD / Serial   │  │ Event trail │  │
│  └──────────────┘  └─────────────────┘  └─────────────┘  │
│                                                           │
│  ┌──────────────────┐                                     │
│  │ SoftwareWatchdog │  (must be kicked every loop tick)   │
│  └──────────────────┘                                     │
└───────────────────────────────────────────────────────────┘
```

### Class responsibilities

| Class | Role |
|---|---|
| `InfusionPump` | Root controller; owns all subsystems; implements `begin()` / `start()` / `tick()` |
| `FlowRateSensor` | ADC read → calibrated mL/h conversion + moving-average filter |
| `DropSensor` | Drop event counting → cumulative volume + instantaneous drop rate |
| `AlarmController` | Non-blocking LED/buzzer driver; cause bitmask; IEC 60601-1-8 pattern |
| `PumpDisplay` | Formatted 20-char panel with progress bar; rate-limited refresh |
| `AuditLogger` | Structured timestamped log to Serial / stdout |
| `SoftwareWatchdog` | millis()-based stall detector; triggers FAULT on timeout |
| `HardwareSimulator` | Desktop-only deterministic sensor input generator |

---

## 🛡️ Safety Interlocks

Two independent safety checks run every control-loop tick while the pump is `RUNNING`:

### Interlock 1 — VTBI Complete (volume limit)
```
IF volume_delivered_mL >= target_VTBI_mL
    → trigger ALARM_VTBI_COMPLETE
    → transition to COMPLETE state
    → activate alarm (buzzer + LED)
```

### Interlock 2 — Flow Rate Exceeded
```
IF commanded_flow_rate_mL_h > 200.0
    → trigger ALARM_FLOW_TOO_HIGH
    → transition to ALARM state
    → activate alarm (buzzer + LED)
    → requires operator acknowledgement before restart
```

### Occlusion Detection (every 10 ticks, after 30 s)
```
IF actual_drop_derived_flow < 10% of commanded_flow
    → trigger ALARM_OCCLUSION
    → transition to ALARM state
```

---

## ⚙️ Configuration

All tunable parameters live in the `PumpConfig` namespace (top of file):

```cpp
namespace PumpConfig {
    constexpr float    DROP_FACTOR_PER_ML   = 20.0f;   // Standard IV giving set
    constexpr float    MAX_FLOW_RATE_ML_H   = 200.0f;  // Hard safety limit
    constexpr float    MIN_FLOW_RATE_ML_H   =   1.0f;  // Minimum valid rate
    constexpr float    DEFAULT_TARGET_ML    = 500.0f;  // VTBI default
    constexpr uint32_t MAIN_LOOP_INTERVAL_MS = 500;    // 2 Hz control loop
    constexpr uint32_t WATCHDOG_TIMEOUT_MS   = 5000;   // 5 s watchdog
}
```

---

## 🚀 Running the Simulation

### Desktop (Linux / macOS / Windows)

**Requirements:** GCC 9+ or Clang 10+ with C++17 support.

```bash
# Clone the repository
git clone https://github.com/<your-username>/MedFusion-OS.git
cd MedFusion-OS

# Compile
g++ -std=c++17 -O2 -Wall -Wextra -o pump InfusionPump.cpp

# Run
./pump
```

**Expected output (excerpt):**
```
[00001] T+000000ms INFO     [INIT] MedFusion OS v2.0 starting up
[00002] T+000000ms INFO     [INIT] Target VTBI = 10.000000 mL
[00003] T+000000ms INFO     [STATE] Transition: IDLE     -> RUNNING
┌──────────────────────────────────────────┐
  MedFusion OS v2.0 — IV Pump
  State  : RUNNING
  Rate   : 152.3 mL/h
  Volume : 0.30 / 10.00 mL
  [██░░░░░░░░░░░░░░] 3.0%
  Drops  : 6
  ETA    : 3.8 min
  Elapsed: 0.0 min
└──────────────────────────────────────────┘

[00012] T+006000ms CRITICAL [SAFETY] FLOW RATE EXCEEDED: 206.8 mL/h > max 200.0 mL/h
[ALARM TRIGGERED] [FLOW RATE EXCEEDED MAX]
```

---

### Arduino Deployment

**Requirements:** Arduino IDE 1.8+ or PlatformIO. Target board: Uno / Nano / Mega.

**Wiring:**

| Component | Arduino Pin |
|---|---|
| Potentiometer (wiper) | A0 |
| Drop sensor (opto-interrupter OUT) | D2 (INT0) |
| Alarm buzzer (active, 5 V) | D9 |
| Alarm LED (red, + 220 Ω) | D10 |
| Status LED (green, + 220 Ω) | D11 |

**Steps:**
1. Rename `InfusionPump.cpp` → `InfusionPump.ino`
2. Comment out the `#ifndef ARDUINO ... #endif` desktop block at the bottom
3. Uncomment the `void setup()` / `void loop()` section and the ISR stub
4. Upload via Arduino IDE

---

## 🗂️ Project Structure

```
MedFusion-OS/
├── InfusionPump.cpp     # Full firmware source (single-file for portability)
├── README.md            # This file
└── docs/
    ├── state_machine.md # FSM transition table and rationale
    └── alarm_spec.md    # IEC 60601-1-8 alarm classification notes
```

---

## 📋 Regulatory Context

This codebase is structured with awareness of the following standards. It is a **simulation / reference implementation** and is **not** a certified medical device.

| Standard | Applicability |
|---|---|
| **IEC 62304** | Medical device software lifecycle — motivates audit logging, state validation, and fault isolation |
| **IEC 60601-1-8** | Medical alarm systems — motivates the 400 ms blink/beep pattern and alarm priority classification |
| **FDA 21 CFR Part 880** | US device classification for infusion pumps |
| **FDA 21 CFR Part 11** | Electronic records / audit trail requirements — motivates the `AuditLogger` class |

---

## 🔭 Potential Extensions

- [ ] PID motor controller loop (stepper motor speed → closed-loop flow rate)
- [ ] I²C LCD integration (LiquidCrystal_I2C library)
- [ ] EEPROM-backed audit log with wear-levelling
- [ ] Wi-Fi / BLE telemetry (ESP32 port) for nurse station integration
- [ ] Unit test suite (Google Test / Unity) targeting the state machine and safety interlocks
- [ ] Second infusion channel (multi-channel pump support)
- [ ] Drug library with pre-programmed concentration profiles

---

## 🤝 Contributing

Pull requests are welcome. For significant changes, please open an issue first to discuss the proposed modification. All contributions must:
- Pass compilation with `-Wall -Wextra -Werror`
- Include comments explaining clinical/safety rationale for any threshold changes
- Not weaken existing safety interlocks

---

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

> **Disclaimer:** This project is an educational simulation. It is not approved for clinical use and must not be used on or near patients. Medical devices require formal regulatory approval, verified testing, and clinical validation before deployment.
