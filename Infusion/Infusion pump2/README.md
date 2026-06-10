# Medical Infusion Pump Simulation (C++)

This project is a C++ simulation designed to understand the core working logic and fundamental safety mechanisms of a medical infusion pump. It is not intended to represent real clinical software — it is a learning exercise in embedded systems thinking, OOP design, and basic safety state machines.

---

## What It Simulates

An infusion pump controls the rate at which a fluid (typically medication) is delivered into a patient's bloodstream. The key concerns from an engineering perspective are:

- Keeping the flow rate within a safe, configured range
- Detecting line blockages (occlusion) before they become dangerous
- Stopping automatically when the prescribed volume has been delivered

This simulation models those behaviours in software using a potentiometer (flow rate dial) and a drop sensor as the two main inputs.

---

## Project Structure

```
InfusionPump/
├── Config.h        # Constants: flow limits, pin numbers, timing thresholds
├── Sensors.h/.cpp  # Potentiometer and DropSensor classes
├── Pump.h/.cpp     # Main pump class with state machine and safety checks
└── main.cpp        # Terminal simulation — runs three test scenarios
```

### File Responsibilities

**`Config.h`**  
A single namespace (`PumpConfig`) holding all the tuneable constants. Keeping these in one place makes it easy to adjust limits without hunting through the code.

**`Sensors.h` / `Sensors.cpp`**  
Two classes:
- `Potentiometer` — stores a raw ADC value (0–1023) and converts it to a flow rate in mL/hr.
- `DropSensor` — counts simulated drops, converts them to delivered volume, and tracks how long the line has been silent (for occlusion detection).

**`Pump.h` / `Pump.cpp`**  
The `InfusionPump` class owns both sensors and manages a five-state machine:

| State      | Meaning                                              |
|------------|------------------------------------------------------|
| `Setup`    | Waiting for configuration before starting            |
| `Infusing` | Actively delivering fluid                            |
| `Paused`   | Temporarily stopped by the user                     |
| `Alarm`    | A safety check failed — pump stopped, waiting for ack |
| `Done`     | Target volume reached — clean completion             |

**`main.cpp`**  
Runs three scripted scenarios to exercise the pump logic:
1. A normal infusion that completes successfully
2. Occlusion detection (drops stop mid-infusion)
3. Flow rate boundary behaviour

---

## How to Compile and Run

No external libraries needed — just a C++11 (or newer) compiler.

```bash
g++ main.cpp Pump.cpp Sensors.cpp -o InfusionSim -std=c++11
./InfusionSim
```

On Windows (MinGW):
```bash
g++ main.cpp Pump.cpp Sensors.cpp -o InfusionSim.exe -std=c++11
InfusionSim.exe
```

---

## Safety Logic Overview

Three checks run on every update tick while the pump is in the `Infusing` state:

1. **Flow rate limits** — if the potentiometer reading maps to a value outside `[MIN_FLOW_RATE, MAX_FLOW_RATE]`, the pump alarms immediately.
2. **Occlusion timeout** — if `OCCLUSION_TIMEOUT_TICKS` consecutive ticks pass without a drop being registered, the pump assumes the line is blocked and alarms.
3. **Target volume reached** — once the drop sensor calculates that the delivered volume has met or exceeded the target, the pump transitions to `Done` (not an alarm — a clean stop).

All thresholds are defined in `Config.h` and can be adjusted without touching the pump logic itself.

---

## Possible Extensions

A few directions this could go next:

- Add a simple ncurses or Dear ImGui interface instead of raw `cout` output
- Replace the simulated drop counter with a proper timer-based flow rate calculation
- Implement a PID-style correction loop that adjusts a simulated motor speed to hit the target flow rate
- Port the logic to an Arduino sketch (the class structure was intentionally kept hardware-agnostic)

---



