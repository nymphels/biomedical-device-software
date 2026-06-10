/**
 * ============================================================
 *  MedFusion OS — Medical Infusion Pump Firmware Simulation
 * ============================================================
 *
 *  @file     InfusionPump.cpp
 *  @version  2.0.0
 *  @date     2026
 *  @author   <your name>
 *  @license  MIT
 *
 *  BIOMEDICAL CONTEXT
 *  ------------------
 *  An infusion pump is a Class II / Class III medical device
 *  (per FDA 21 CFR Part 880) that delivers precise volumes of
 *  intravenous (IV) fluids, medications, or nutrients directly
 *  into a patient's circulatory system.  Firmware defects in
 *  such devices are life-critical: over-infusion can cause
 *  fluid overload, toxicity, or cardiac arrest; under-infusion
 *  can lead to under-dosing of emergency medications.
 *
 *  This file simulates the core firmware layer of a single-
 *  channel peristaltic infusion pump.  It is designed to run
 *  both on a standard C++ toolchain (desktop/CI) and on an
 *  Arduino-compatible board with minimal porting effort.
 *
 *  SYSTEM OVERVIEW
 *  ---------------
 *  ┌──────────────────────────────────────────────────────┐
 *  │                   InfusionPump                       │
 *  │  ┌─────────────┐   ┌────────────┐   ┌────────────┐  │
 *  │  │  FlowSensor │   │ DropSensor │   │  Alarm     │  │
 *  │  │ (pot→mL/h)  │   │ (drops→mL) │   │ Controller │  │
 *  │  └──────┬──────┘   └─────┬──────┘   └─────┬──────┘  │
 *  │         │                │                 │         │
 *  │  ┌──────▼────────────────▼─────────────────▼──────┐  │
 *  │  │              PumpStateMachine                  │  │
 *  │  │  IDLE → RUNNING → [COMPLETE | ALARM | PAUSED]  │  │
 *  │  └────────────────────────┬───────────────────────┘  │
 *  │                           │                          │
 *  │                    ┌──────▼──────┐                   │
 *  │                    │  Display    │                   │
 *  │                    │  (LCD/Ser.) │                   │
 *  │                    └─────────────┘                   │
 *  └──────────────────────────────────────────────────────┘
 *
 *  SAFETY THRESHOLDS (configurable in PumpConfig)
 *  -----------------------------------------------
 *   Max flow rate  : 200 mL/h  (tachycardia / toxicity risk)
 *   Min flow rate  :   1 mL/h  (occlusion / pump-stopped)
 *   Drop factor    :  20 drops/mL (standard IV giving set)
 *
 *  HARDWARE PIN MAP (Arduino mode)
 *  --------------------------------
 *   A0  — Potentiometer (flow rate adjuster)
 *   D2  — Drop sensor (opto-interrupter, interrupt-capable)
 *   D9  — Buzzer (PWM, active alarm)
 *   D10 — Alarm LED (red)
 *   D11 — Status LED (green, pump running)
 *
 *  BUILD INSTRUCTIONS
 *  ------------------
 *   Desktop (GCC/Clang):
 *     g++ -std=c++17 -O2 -Wall -o pump InfusionPump.cpp && ./pump
 *
 *   Arduino IDE:
 *     1. Rename file to InfusionPump.ino
 *     2. Comment out the "DESKTOP ENTRY POINT" section at the bottom
 *     3. Uncomment the Arduino-specific pin read macros (marked below)
 *     4. Flash to your board
 *
 * ============================================================
 */

// ── Standard library headers (desktop mode) ──────────────
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <random>
#include <array>

// ── Platform abstraction ──────────────────────────────────
// On Arduino, replace these with actual hardware calls.
// On desktop, they are replaced by the simulator below.
#ifdef ARDUINO
  #include <Arduino.h>
  #include <LiquidCrystal.h>
  #define PLATFORM_DELAY_MS(ms)   delay(ms)
  #define PLATFORM_MILLIS()       millis()
  #define PLATFORM_ANALOG_READ(p) analogRead(p)
  #define PLATFORM_PRINT(s)       Serial.print(s)
  #define PLATFORM_PRINTLN(s)     Serial.println(s)
#else
  // Desktop stubs — implemented by SimulatedHardware below
  static uint32_t g_simMillis = 0;
  #define PLATFORM_DELAY_MS(ms)   std::this_thread::sleep_for(std::chrono::milliseconds(ms))
  #define PLATFORM_MILLIS()       g_simMillis
  #define PLATFORM_PRINT(s)       std::cout << s
  #define PLATFORM_PRINTLN(s)     std::cout << s << '\n'
#endif


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 1 — CONFIGURATION CONSTANTS                    ║
// ╚══════════════════════════════════════════════════════════╝

/**
 * @brief Central configuration namespace for all pump parameters.
 *
 * Grouping constants here (rather than scattering #defines) makes
 * FDA 21 CFR Part 11 configuration management audits much simpler.
 */
namespace PumpConfig {

    // ── Physical / clinical constants ────────────────────
    constexpr float  DROP_FACTOR_PER_ML   = 20.0f;  ///< Standard IV giving set (drops/mL)
    constexpr float  MAX_FLOW_RATE_ML_H   = 200.0f; ///< Hard upper safety limit (mL/h)
    constexpr float  MIN_FLOW_RATE_ML_H   =   1.0f; ///< Minimum meaningful flow rate
    constexpr float  DEFAULT_TARGET_ML    = 500.0f; ///< Default VTBI (Volume To Be Infused)
    constexpr float  ML_PER_DROP          = 1.0f / DROP_FACTOR_PER_ML; ///< 0.05 mL/drop

    // ── ADC / hardware ────────────────────────────────────
    constexpr uint8_t  PIN_POTENTIOMETER  = A0;  ///< Analog pin for flow rate control
    constexpr uint8_t  PIN_DROP_SENSOR    = 2;   ///< Digital interrupt pin
    constexpr uint8_t  PIN_BUZZER         = 9;   ///< PWM pin for alarm tone
    constexpr uint8_t  PIN_LED_ALARM      = 10;  ///< Red alarm LED
    constexpr uint8_t  PIN_LED_STATUS     = 11;  ///< Green running LED
    constexpr uint16_t ADC_MAX            = 1023; ///< 10-bit ADC maximum value

    // ── Timing ────────────────────────────────────────────
    constexpr uint32_t MAIN_LOOP_INTERVAL_MS    = 500;  ///< Control loop tick rate (2 Hz)
    constexpr uint32_t DISPLAY_REFRESH_MS       = 1000; ///< LCD refresh rate
    constexpr uint32_t ALARM_BEEP_INTERVAL_MS   = 400;  ///< Buzzer pulse period
    constexpr uint32_t WATCHDOG_TIMEOUT_MS      = 5000; ///< Software watchdog

    // ── Simulation parameters (desktop only) ─────────────
    constexpr uint32_t SIM_TOTAL_TICKS   = 60;   ///< How many loop ticks to simulate
    constexpr uint32_t SIM_TICK_STEP_MS  = 500;  ///< Simulated time per tick (ms)
    constexpr float    SIM_TARGET_ML     = 10.0f;///< Small target for quick demo run

} // namespace PumpConfig


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 2 — PUMP STATE MACHINE                         ║
// ╚══════════════════════════════════════════════════════════╝

/**
 * @brief Strongly-typed pump operating states.
 *
 * The state machine follows IEC 62304 (Medical Device Software
 * Life Cycle) guidance for safety-critical state transitions.
 * Invalid transitions (e.g. COMPLETE → RUNNING without a reset)
 * are blocked in PumpStateMachine::transition().
 */
enum class PumpState : uint8_t {
    IDLE        = 0,  ///< Powered on, not yet started
    RUNNING     = 1,  ///< Active infusion in progress
    PAUSED      = 2,  ///< Temporarily halted by operator
    ALARM       = 3,  ///< Safety threshold breached — requires acknowledgement
    COMPLETE    = 4,  ///< VTBI delivered successfully
    FAULT       = 5   ///< Unrecoverable hardware/software fault
};

/**
 * @brief Human-readable names for logging and display.
 */
inline const char* pumpStateLabel(PumpState s) {
    switch (s) {
        case PumpState::IDLE:     return "IDLE    ";
        case PumpState::RUNNING:  return "RUNNING ";
        case PumpState::PAUSED:   return "PAUSED  ";
        case PumpState::ALARM:    return "** ALARM **";
        case PumpState::COMPLETE: return "COMPLETE";
        case PumpState::FAULT:    return "!! FAULT !!";
        default:                  return "UNKNOWN ";
    }
}

/**
 * @brief Alarm cause flags — a bitmask so multiple causes can coexist.
 */
enum AlarmCause : uint8_t {
    ALARM_NONE           = 0x00,
    ALARM_FLOW_TOO_HIGH  = 0x01, ///< Flow rate exceeded MAX_FLOW_RATE_ML_H
    ALARM_VTBI_COMPLETE  = 0x02, ///< Target volume successfully delivered
    ALARM_OCCLUSION      = 0x04, ///< No drops detected while pump running
    ALARM_WATCHDOG       = 0x08  ///< Software watchdog expired
};


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 3 — SENSOR CLASSES                             ║
// ╚══════════════════════════════════════════════════════════╝

// ──────────────────────────────────────────────────────────
//  3a. FlowRateSensor
//      Reads a potentiometer and maps ADC counts → mL/h.
//      On a real pump this would read a stepper motor encoder
//      or a differential pressure transducer, but a
//      potentiometer gives a useful physical analogue for
//      simulation and bench testing.
// ──────────────────────────────────────────────────────────

class FlowRateSensor {
public:
    /**
     * @param pin        Analog pin number.
     * @param maxFlowMlH Upper range of the sensor (maps to ADC_MAX).
     */
    explicit FlowRateSensor(uint8_t pin, float maxFlowMlH = PumpConfig::MAX_FLOW_RATE_ML_H)
        : _pin(pin), _maxFlowMlH(maxFlowMlH), _lastReadingMlH(0.0f) {}

    /**
     * @brief Read and convert the current potentiometer value to mL/h.
     *
     * Linear mapping: 0 ADC counts = 0 mL/h, 1023 = maxFlowMlH.
     * A real implementation would apply calibration coefficients,
     * sensor linearisation, and a low-pass filter to remove noise.
     *
     * @param rawADC   Raw ADC value (0–1023). Pass -1 to read hardware.
     * @return         Calibrated flow rate in mL/h.
     */
    float read(int rawADC = -1) {
#ifndef ARDUINO
        // Desktop: accept injected value
        if (rawADC < 0) rawADC = 512; // default mid-scale
#else
        rawADC = analogRead(_pin);
#endif
        // Clamp to valid ADC range (defensive against hardware glitches)
        rawADC = std::max(0, std::min(rawADC, static_cast<int>(PumpConfig::ADC_MAX)));

        // Linear mapping to mL/h
        _lastReadingMlH = (static_cast<float>(rawADC) / PumpConfig::ADC_MAX) * _maxFlowMlH;
        return _lastReadingMlH;
    }

    /** @return Most recently sampled flow rate in mL/h (no new hardware read). */
    float lastReading() const { return _lastReadingMlH; }

    /**
     * @brief Apply a simple moving-average filter over N samples.
     *
     * Used to debounce mechanical potentiometer jitter.
     * In a real device, a 4–8 sample average at 10 Hz is typical.
     *
     * @param samples Array of raw ADC readings.
     * @param n       Number of samples.
     * @return        Filtered flow rate in mL/h.
     */
    float readFiltered(const int* samples, size_t n) {
        if (n == 0) return _lastReadingMlH;
        float sum = 0.0f;
        for (size_t i = 0; i < n; ++i) sum += samples[i];
        return read(static_cast<int>(sum / n));
    }

private:
    uint8_t _pin;
    float   _maxFlowMlH;
    float   _lastReadingMlH;
};


// ──────────────────────────────────────────────────────────
//  3b. DropSensor
//      Counts drops falling through an opto-interrupter
//      (infrared beam break) in the drip chamber.  Each
//      drop corresponds to ~0.05 mL (for a 20 drops/mL set).
//      On Arduino, this is wired to an interrupt pin so no
//      drops are missed even during display updates.
// ──────────────────────────────────────────────────────────

class DropSensor {
public:
    explicit DropSensor(uint8_t pin)
        : _pin(pin), _dropCount(0), _volumeDeliveredMl(0.0f),
          _lastDropTime(0), _dropsPerMinute(0.0f) {}

    /** @brief Reset counters (call on new infusion session). */
    void reset() {
        _dropCount         = 0;
        _volumeDeliveredMl = 0.0f;
        _lastDropTime      = 0;
        _dropsPerMinute    = 0.0f;
    }

    /**
     * @brief Register a new drop event (called from ISR on Arduino,
     *        or from simulator on desktop).
     *
     * Calculates an instantaneous drop rate (drops/min) which can
     * be used to cross-validate the commanded flow rate.
     */
    void registerDrop() {
        ++_dropCount;
        _volumeDeliveredMl += PumpConfig::ML_PER_DROP;

        uint32_t now = PLATFORM_MILLIS();
        if (_lastDropTime > 0) {
            uint32_t intervalMs = now - _lastDropTime;
            if (intervalMs > 0) {
                // Instantaneous rate: convert ms interval between drops → drops/min
                _dropsPerMinute = 60000.0f / static_cast<float>(intervalMs);
            }
        }
        _lastDropTime = now;
    }

    /** @brief Simulate N drops arriving (desktop mode only). */
    void simulateDrops(uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) registerDrop();
    }

    // ── Accessors ─────────────────────────────────────────
    uint32_t totalDrops()         const { return _dropCount; }
    float    volumeDeliveredMl()  const { return _volumeDeliveredMl; }
    float    dropsPerMinute()     const { return _dropsPerMinute; }

    /**
     * @brief Derive flow rate from drop count (ml/h).
     *
     * Used to cross-check commanded rate vs. actual delivery —
     * a discrepancy of >10% should raise an occlusion alarm.
     *
     * @param elapsedSeconds Total elapsed infusion time in seconds.
     */
    float derivedFlowRateMlH(float elapsedSeconds) const {
        if (elapsedSeconds < 1.0f) return 0.0f;
        return (_volumeDeliveredMl / elapsedSeconds) * 3600.0f;
    }

private:
    uint8_t  _pin;
    uint32_t _dropCount;
    float    _volumeDeliveredMl;
    uint32_t _lastDropTime;     ///< millis() at last drop (for rate calculation)
    float    _dropsPerMinute;
};


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 4 — ALARM CONTROLLER                           ║
// ╚══════════════════════════════════════════════════════════╝

/**
 * @brief Manages the hardware alarm outputs (buzzer + LED).
 *
 * Uses non-blocking timing (millis-based) so the control loop
 * is never stalled by a delay().  On Arduino this drives a PWM
 * buzzer at 2 kHz and a digital LED.  On desktop it prints to
 * the console and uses ANSI colour codes for visibility.
 */
class AlarmController {
public:
    AlarmController(uint8_t buzzerPin, uint8_t ledPin)
        : _buzzerPin(buzzerPin), _ledPin(ledPin),
          _active(false), _cause(ALARM_NONE),
          _ledState(false), _lastToggle(0) {}

    void begin() const {
#ifdef ARDUINO
        pinMode(_buzzerPin, OUTPUT);
        pinMode(_ledPin,    OUTPUT);
        digitalWrite(_buzzerPin, LOW);
        digitalWrite(_ledPin,    LOW);
#endif
    }

    /**
     * @brief Activate the alarm with a specific cause code.
     * @param cause  Bitmask of AlarmCause flags.
     */
    void trigger(uint8_t cause) {
        _active = true;
        _cause |= cause; // accumulate causes (multiple may coexist)
        PLATFORM_PRINTLN("\033[1;31m[ALARM TRIGGERED] " + causeDescription() + "\033[0m");
    }

    /**
     * @brief Acknowledge and silence the alarm (operator action).
     *        In a real device this would require a physical button
     *        press and would be logged to the audit trail.
     */
    void acknowledge() {
        _active = false;
        _cause  = ALARM_NONE;
        _ledState = false;
        silenceHardware();
        PLATFORM_PRINTLN("[ALARM] Acknowledged and silenced.");
    }

    bool    isActive() const { return _active; }
    uint8_t cause()    const { return _cause; }

    /**
     * @brief Non-blocking alarm driver — call every loop iteration.
     *
     * Produces a 400 ms on / 400 ms off blink+beep pattern,
     * which matches IEC 60601-1-8 (Medical Device Alarm Systems)
     * requirements for a "medium priority" alarm.
     */
    void update() {
        if (!_active) return;

        uint32_t now = PLATFORM_MILLIS();
        if (now - _lastToggle >= PumpConfig::ALARM_BEEP_INTERVAL_MS) {
            _ledState   = !_ledState;
            _lastToggle = now;

#ifdef ARDUINO
            digitalWrite(_ledPin, _ledState ? HIGH : LOW);
            if (_ledState)
                tone(_buzzerPin, 2000, PumpConfig::ALARM_BEEP_INTERVAL_MS);
#else
            // Desktop: use ANSI escape codes to simulate LED flash
            if (_ledState)
                PLATFORM_PRINT("\033[1;31m  ♦ ALARM ♦  \033[0m");
#endif
        }
    }

    /**
     * @brief Returns a human-readable alarm description for logging.
     */
    std::string causeDescription() const {
        std::string desc;
        if (_cause & ALARM_FLOW_TOO_HIGH) desc += "[FLOW RATE EXCEEDED MAX] ";
        if (_cause & ALARM_VTBI_COMPLETE) desc += "[VTBI DELIVERED — TARGET REACHED] ";
        if (_cause & ALARM_OCCLUSION)     desc += "[OCCLUSION DETECTED] ";
        if (_cause & ALARM_WATCHDOG)      desc += "[SOFTWARE WATCHDOG TIMEOUT] ";
        if (desc.empty())                 desc  = "[UNKNOWN ALARM CAUSE]";
        return desc;
    }

private:
    void silenceHardware() const {
#ifdef ARDUINO
        noTone(_buzzerPin);
        digitalWrite(_ledPin,    LOW);
#endif
    }

    uint8_t  _buzzerPin;
    uint8_t  _ledPin;
    bool     _active;
    uint8_t  _cause;
    bool     _ledState;
    uint32_t _lastToggle;
};


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 5 — DISPLAY / LOGGER                           ║
// ╚══════════════════════════════════════════════════════════╝

/**
 * @brief Renders pump status to LCD (Arduino) or terminal (desktop).
 *
 * On Arduino, drive a 20×4 LCD via the LiquidCrystal library.
 * The same interface works on desktop via std::cout with column
 * formatting that mirrors a physical 20-character display line.
 *
 * Display layout (20×4):
 *   Line 0:  "MedFusion OS v2.0   "
 *   Line 1:  "Rate:  XXX.X mL/h   "
 *   Line 2:  "Vol : XX.XX / XX mL "
 *   Line 3:  "State: XXXXXXXXXX   "
 */
class PumpDisplay {
public:
    PumpDisplay() : _lastRefresh(0) {}

    /** @brief Initial splash screen. */
    void showSplash() const {
        PLATFORM_PRINTLN("╔══════════════════════╗");
        PLATFORM_PRINTLN("║  MedFusion OS v2.0   ║");
        PLATFORM_PRINTLN("║  IV Infusion Pump    ║");
        PLATFORM_PRINTLN("║  Initialising...     ║");
        PLATFORM_PRINTLN("╚══════════════════════╝");
    }

    /**
     * @brief Refresh display with current pump telemetry.
     *
     * Rate-limited to DISPLAY_REFRESH_MS to avoid flooding the
     * serial port / LCD controller.
     *
     * @param state          Current pump state.
     * @param flowRateMlH    Commanded flow rate (mL/h).
     * @param volumeDoneMl   Volume delivered so far (mL).
     * @param targetMl       VTBI target (mL).
     * @param elapsedSec     Elapsed infusion time (seconds).
     * @param dropsTotal     Total drops counted.
     * @param alarmDesc      Current alarm description string.
     */
    void refresh(PumpState state,
                 float     flowRateMlH,
                 float     volumeDoneMl,
                 float     targetMl,
                 float     elapsedSec,
                 uint32_t  dropsTotal,
                 const std::string& alarmDesc = "") {
        uint32_t now = PLATFORM_MILLIS();
        if (now - _lastRefresh < PumpConfig::DISPLAY_REFRESH_MS) return;
        _lastRefresh = now;

        // Estimated time remaining (hours)
        float remainingMl  = std::max(0.0f, targetMl - volumeDoneMl);
        float etaHours     = (flowRateMlH > 0.0f) ? remainingMl / flowRateMlH : 0.0f;
        float etaMinutes   = etaHours * 60.0f;
        float pctComplete  = (targetMl > 0.0f) ? (volumeDoneMl / targetMl) * 100.0f : 0.0f;
        pctComplete        = std::min(pctComplete, 100.0f);

        // Build a 20-character progress bar
        std::string bar = buildProgressBar(pctComplete, 16);

        // ── Print formatted panel ──────────────────────────
        PLATFORM_PRINTLN("┌──────────────────────────────────────┐");

        // Line 1 — header
        printRow("  MedFusion OS v2.0 — IV Pump");

        // Line 2 — state
        {
            std::ostringstream ss;
            ss << "  State  : " << pumpStateLabel(state);
            printRow(ss.str());
        }

        // Line 3 — flow rate with safety annotation
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1);
            ss << "  Rate   : " << flowRateMlH << " mL/h";
            if (flowRateMlH > PumpConfig::MAX_FLOW_RATE_ML_H)
                ss << "  \033[1;31m[OVER LIMIT]\033[0m";
            printRow(ss.str());
        }

        // Line 4 — volume delivered
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "  Volume : " << volumeDoneMl << " / " << targetMl << " mL";
            printRow(ss.str());
        }

        // Line 5 — progress bar
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1);
            ss << "  " << bar << " " << pctComplete << "%";
            printRow(ss.str());
        }

        // Line 6 — drops & ETA
        {
            std::ostringstream ss;
            ss << "  Drops  : " << dropsTotal;
            printRow(ss.str());
        }
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1);
            ss << "  ETA    : " << etaMinutes << " min";
            printRow(ss.str());
        }
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1);
            ss << "  Elapsed: " << (elapsedSec / 60.0f) << " min";
            printRow(ss.str());
        }

        // Line 7 — alarm info (only if active)
        if (!alarmDesc.empty()) {
            printRow("\033[1;31m  *** " + alarmDesc + " ***\033[0m");
        }

        PLATFORM_PRINTLN("└──────────────────────────────────────┘");
        PLATFORM_PRINTLN(""); // blank line for readability
    }

private:
    uint32_t _lastRefresh;

    static void printRow(const std::string& content) {
        PLATFORM_PRINTLN(content);
    }

    /**
     * @brief Builds a text-mode progress bar of the given width.
     * @param pct   Percentage complete (0–100).
     * @param width Total bar width in characters.
     */
    static std::string buildProgressBar(float pct, int width) {
        int filled = static_cast<int>((pct / 100.0f) * width);
        filled     = std::max(0, std::min(filled, width));
        std::string bar = "[";
        for (int i = 0; i < width; ++i)
            bar += (i < filled) ? '█' : '░';
        bar += "]";
        return bar;
    }
};


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 6 — AUDIT LOGGER                               ║
// ╚══════════════════════════════════════════════════════════╝

/**
 * @brief Write-once event log for regulatory compliance.
 *
 * FDA 21 CFR Part 11 requires that safety-critical medical
 * devices maintain an immutable audit trail of all state
 * changes, alarm events, and operator interventions.
 *
 * On a real device this would write to EEPROM, an SD card,
 * or a secure flash partition.  Here we print structured
 * log entries to stdout/Serial with ISO 8601-style timestamps
 * derived from millis().
 */
class AuditLogger {
public:
    /**
     * @brief Log levels following IEC 62304 criticality tiers.
     */
    enum class Level : uint8_t { INFO, WARNING, ERROR, CRITICAL };

    AuditLogger() : _entryIndex(0) {}

    /**
     * @brief Write a structured log entry.
     * @param level   Severity level.
     * @param tag     Source module identifier (e.g. "STATE", "ALARM").
     * @param message Human-readable event description.
     */
    void log(Level level, const std::string& tag, const std::string& message) {
        ++_entryIndex;
        std::ostringstream entry;
        entry << "[" << std::setw(5) << std::setfill('0') << _entryIndex << "] "
              << "T+" << std::setw(6) << std::setfill('0') << PLATFORM_MILLIS() << "ms "
              << levelLabel(level) << " "
              << "[" << tag << "] "
              << message;
        PLATFORM_PRINTLN(entry.str());
    }

    void info    (const std::string& tag, const std::string& msg) { log(Level::INFO,     tag, msg); }
    void warning (const std::string& tag, const std::string& msg) { log(Level::WARNING,  tag, msg); }
    void error   (const std::string& tag, const std::string& msg) { log(Level::ERROR,    tag, msg); }
    void critical(const std::string& tag, const std::string& msg) { log(Level::CRITICAL, tag, msg); }

private:
    uint32_t _entryIndex;

    static const char* levelLabel(Level l) {
        switch (l) {
            case Level::INFO:     return "INFO    ";
            case Level::WARNING:  return "WARNING ";
            case Level::ERROR:    return "ERROR   ";
            case Level::CRITICAL: return "CRITICAL";
            default:              return "UNKNOWN ";
        }
    }
};


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 7 — SOFTWARE WATCHDOG                          ║
// ╚══════════════════════════════════════════════════════════╝

/**
 * @brief Software watchdog timer.
 *
 * Must be "kicked" (reset) each main loop iteration.  If the
 * loop stalls for longer than WATCHDOG_TIMEOUT_MS (e.g. due to
 * a blocking peripheral call or infinite loop bug), the watchdog
 * fires and forces the pump into FAULT state.
 *
 * On real hardware, complement this with the MCU's hardware WDT
 * (e.g. avr-libc wdt_enable() / wdt_reset() on ATmega).
 */
class SoftwareWatchdog {
public:
    explicit SoftwareWatchdog(uint32_t timeoutMs)
        : _timeoutMs(timeoutMs), _lastKick(0), _expired(false) {}

    void begin() { _lastKick = PLATFORM_MILLIS(); }

    /** @brief Reset the watchdog — call every main loop iteration. */
    void kick() {
        _lastKick = PLATFORM_MILLIS();
        _expired  = false;
    }

    /**
     * @brief Check if the watchdog has expired.
     * @return true if the loop has stalled beyond the timeout.
     */
    bool check() {
        if (!_expired && (PLATFORM_MILLIS() - _lastKick) > _timeoutMs) {
            _expired = true;
        }
        return _expired;
    }

    bool isExpired() const { return _expired; }

private:
    uint32_t _timeoutMs;
    uint32_t _lastKick;
    bool     _expired;
};


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 8 — INFUSION PUMP MAIN CLASS                   ║
// ╚══════════════════════════════════════════════════════════╝

/**
 * @brief Root controller class for the MedFusion infusion pump.
 *
 * Owns all subsystem objects and implements:
 *  - Session configuration (VTBI, rate limits)
 *  - Main control loop tick()
 *  - State machine transitions
 *  - Safety interlock logic
 *  - Occlusion detection via commanded vs. actual flow comparison
 *
 * Usage pattern (matches Arduino setup()/loop()):
 * @code
 *   InfusionPump pump;
 *   pump.begin(500.0f);   // configure 500 mL VTBI
 *   pump.start();
 *   while (pump.isRunning()) {
 *       pump.tick(rawADC, simulatedDrops);
 *   }
 * @endcode
 */
class InfusionPump {
public:

    // ── Constructor ───────────────────────────────────────
    InfusionPump()
        : _state(PumpState::IDLE)
        , _flowSensor(PumpConfig::PIN_POTENTIOMETER)
        , _dropSensor(PumpConfig::PIN_DROP_SENSOR)
        , _alarm(PumpConfig::PIN_BUZZER, PumpConfig::PIN_LED_ALARM)
        , _watchdog(PumpConfig::WATCHDOG_TIMEOUT_MS)
        , _targetVolumeMl(PumpConfig::DEFAULT_TARGET_ML)
        , _flowRateMlH(0.0f)
        , _sessionStartMs(0)
        , _lastTickMs(0)
        , _tickCount(0)
        , _occlusionCheckCounter(0)
    {}

    // ── Public API ────────────────────────────────────────

    /**
     * @brief Initialise all subsystems (replaces Arduino setup()).
     * @param targetMl  Volume To Be Infused in millilitres.
     */
    void begin(float targetMl = PumpConfig::DEFAULT_TARGET_ML) {
        _targetVolumeMl = targetMl;

        _alarm.begin();
        _watchdog.begin();
        _display.showSplash();

        _log.info("INIT", "MedFusion OS v2.0 starting up");
        _log.info("INIT", "Target VTBI = " + std::to_string(_targetVolumeMl) + " mL");
        _log.info("INIT", "Max flow rate = " +
                  std::to_string(PumpConfig::MAX_FLOW_RATE_ML_H) + " mL/h");

        transition(PumpState::IDLE);
    }

    /**
     * @brief Start a new infusion session.
     *
     * Guards against starting from an invalid state (alarm/fault).
     * Resets all delivery counters.
     */
    void start() {
        if (_state != PumpState::IDLE && _state != PumpState::PAUSED) {
            _log.warning("CTRL", "Start ignored — pump not in IDLE or PAUSED state");
            return;
        }
        _dropSensor.reset();
        _sessionStartMs = PLATFORM_MILLIS();
        transition(PumpState::RUNNING);
        _log.info("CTRL", "Infusion started. Target = " +
                  std::to_string(_targetVolumeMl) + " mL");
    }

    /** @brief Temporarily pause the infusion (e.g. bag change). */
    void pause() {
        if (_state != PumpState::RUNNING) return;
        transition(PumpState::PAUSED);
        _log.warning("CTRL", "Infusion PAUSED by operator");
    }

    /** @brief Resume from PAUSED state. */
    void resume() {
        if (_state != PumpState::PAUSED) return;
        transition(PumpState::RUNNING);
        _log.info("CTRL", "Infusion RESUMED");
    }

    /**
     * @brief Acknowledge an alarm and return to IDLE for re-configuration.
     *
     * In a certified device, this would also require a PIN code and
     * would be logged to the tamper-evident audit trail.
     */
    void acknowledgeAlarm() {
        if (_state != PumpState::ALARM) return;
        _alarm.acknowledge();
        transition(PumpState::IDLE);
        _log.info("ALARM", "Alarm acknowledged. Pump returned to IDLE.");
    }

    /**
     * @brief Main control loop tick — call from Arduino loop() or simulation.
     *
     * Responsibilities:
     *  1. Kick software watchdog
     *  2. Read flow rate sensor
     *  3. Simulate drop arrival (or read ISR counter on Arduino)
     *  4. Enforce safety interlocks
     *  5. Check occlusion
     *  6. Update display
     *  7. Drive alarm outputs
     *
     * @param rawADC           Raw ADC reading for flow rate sensor (0–1023).
     *                         Pass -1 to use hardware read on Arduino.
     * @param newDropsThisTick Number of drops to register this tick (simulation).
     *                         On Arduino, read and clear the ISR counter instead.
     */
    void tick(int rawADC = -1, uint32_t newDropsThisTick = 0) {
        ++_tickCount;
        _watchdog.kick();

        // Guard: only process sensor data while RUNNING
        if (_state == PumpState::RUNNING) {

            // ── 1. Read flow rate ──────────────────────────
            _flowRateMlH = _flowSensor.read(rawADC);

            // ── 2. Register new drops ──────────────────────
#ifndef ARDUINO
            _dropSensor.simulateDrops(newDropsThisTick);
#else
            // On Arduino, the ISR increments a volatile counter.
            // Read & clear it atomically here.
            // _dropSensor.simulateDrops(isrDropCount);
            // isrDropCount = 0;
#endif

            // ── 3. Safety interlocks ──────────────────────
            enforceSafetyInterlocks();

            // ── 4. Occlusion check (every 10 ticks) ───────
            if (++_occlusionCheckCounter >= 10) {
                _occlusionCheckCounter = 0;
                checkOcclusion();
            }
        }

        // ── 5. Refresh display ─────────────────────────────
        float elapsedSec = elapsedSeconds();
        std::string alarmDesc = _alarm.isActive() ? _alarm.causeDescription() : "";
        _display.refresh(
            _state,
            _flowRateMlH,
            _dropSensor.volumeDeliveredMl(),
            _targetVolumeMl,
            elapsedSec,
            _dropSensor.totalDrops(),
            alarmDesc
        );

        // ── 6. Drive alarm hardware ────────────────────────
        _alarm.update();

        // ── 7. Watchdog check ──────────────────────────────
        // (This would only trip if the tick() itself stalled, which is
        //  a belt-and-suspenders check in production firmware.)
        if (_watchdog.check()) {
            _log.critical("WDT", "Software watchdog expired — entering FAULT state");
            _alarm.trigger(ALARM_WATCHDOG);
            transition(PumpState::FAULT);
        }

        _lastTickMs = PLATFORM_MILLIS();
    }

    // ── State / telemetry accessors ───────────────────────

    PumpState state()             const { return _state; }
    float     flowRateMlH()       const { return _flowRateMlH; }
    float     volumeDeliveredMl() const { return _dropSensor.volumeDeliveredMl(); }
    float     targetVolumeMl()    const { return _targetVolumeMl; }
    float     elapsedSeconds()    const {
        if (_sessionStartMs == 0) return 0.0f;
        return static_cast<float>(PLATFORM_MILLIS() - _sessionStartMs) / 1000.0f;
    }
    bool      isRunning()         const {
        return _state == PumpState::RUNNING || _state == PumpState::PAUSED;
    }
    bool      isAlarmActive()     const { return _alarm.isActive(); }
    uint32_t  tickCount()         const { return _tickCount; }


private:

    // ── Subsystems ────────────────────────────────────────
    PumpState        _state;
    FlowRateSensor   _flowSensor;
    DropSensor       _dropSensor;
    AlarmController  _alarm;
    PumpDisplay      _display;
    AuditLogger      _log;
    SoftwareWatchdog _watchdog;

    // ── Session parameters ────────────────────────────────
    float    _targetVolumeMl;
    float    _flowRateMlH;
    uint32_t _sessionStartMs;
    uint32_t _lastTickMs;
    uint32_t _tickCount;
    uint8_t  _occlusionCheckCounter;

    // ── State transition ──────────────────────────────────

    /**
     * @brief Perform a validated state machine transition.
     *
     * Logs every transition.  Invalid transitions (e.g. COMPLETE →
     * RUNNING without IDLE reset) are blocked with a warning.
     *
     * @param next  The desired next state.
     */
    void transition(PumpState next) {
        if (!isValidTransition(_state, next)) {
            _log.warning("STATE",
                std::string("Invalid transition: ") +
                pumpStateLabel(_state) + " -> " + pumpStateLabel(next));
            return;
        }
        _log.info("STATE",
            std::string("Transition: ") +
            pumpStateLabel(_state) + " -> " + pumpStateLabel(next));
        _state = next;
    }

    /**
     * @brief Define legal state transitions.
     *
     * This encodes the finite-state machine topology:
     *
     *   IDLE ──────► RUNNING ◄──── PAUSED
     *                  │   │          ▲
     *                  ▼   └──────────┘
     *                ALARM ──► IDLE (after ack)
     *                  │
     *                  ▼
     *               COMPLETE
     *
     *   Any state ──► FAULT (unrecoverable)
     */
    static bool isValidTransition(PumpState from, PumpState to) {
        if (to == PumpState::FAULT) return true; // always allowed
        switch (from) {
            case PumpState::IDLE:
                return to == PumpState::RUNNING;
            case PumpState::RUNNING:
                return to == PumpState::PAUSED
                    || to == PumpState::ALARM
                    || to == PumpState::COMPLETE;
            case PumpState::PAUSED:
                return to == PumpState::RUNNING
                    || to == PumpState::ALARM;
            case PumpState::ALARM:
                return to == PumpState::IDLE;
            case PumpState::COMPLETE:
                return to == PumpState::IDLE; // allow re-use after new config
            case PumpState::FAULT:
                return false; // FAULT is terminal — requires physical power cycle
            default:
                return false;
        }
    }

    // ── Safety interlocks ─────────────────────────────────

    /**
     * @brief Enforce all safety interlocks on every running tick.
     *
     * Two independent safety conditions, checked in priority order:
     *
     *  1. VTBI complete:  volume delivered ≥ target — highest priority,
     *     stops infusion immediately to prevent over-delivery.
     *
     *  2. Flow rate limit: commanded rate > 200 mL/h — this could
     *     indicate a stuck potentiometer or operator error.  A brief
     *     de-bounce window (here: immediate) is used for simplicity;
     *     a real device would require 2 consecutive readings above
     *     threshold before triggering.
     *
     * Both interlocks halt the pump motor (simulated here as a state
     * transition) and activate the alarm.
     */
    void enforceSafetyInterlocks() {

        // ── Interlock 1: VTBI complete ─────────────────────
        if (_dropSensor.volumeDeliveredMl() >= _targetVolumeMl) {
            _log.critical("SAFETY",
                "VTBI COMPLETE: delivered " +
                std::to_string(_dropSensor.volumeDeliveredMl()) +
                " mL of target " +
                std::to_string(_targetVolumeMl) + " mL");
            _alarm.trigger(ALARM_VTBI_COMPLETE);
            transition(PumpState::COMPLETE);
            return; // no further checks needed
        }

        // ── Interlock 2: Flow rate exceeded maximum ─────────
        if (_flowRateMlH > PumpConfig::MAX_FLOW_RATE_ML_H) {
            _log.critical("SAFETY",
                "FLOW RATE EXCEEDED: " +
                std::to_string(_flowRateMlH) +
                " mL/h > max " +
                std::to_string(PumpConfig::MAX_FLOW_RATE_ML_H) + " mL/h");
            _alarm.trigger(ALARM_FLOW_TOO_HIGH);
            transition(PumpState::ALARM);
        }
    }

    /**
     * @brief Cross-check commanded flow rate against drop-derived rate.
     *
     * A significant discrepancy suggests:
     *  - Occlusion (tubing kinked or IV site infiltrated)
     *  - Free-flow (clamp not applied, gravity override)
     *  - Sensor failure
     *
     * Tolerance: if commanded > 0 and actual is < 10% of commanded,
     * raise an occlusion alarm.  Only meaningful after at least 30 s
     * of infusion to allow the drop rate to stabilise.
     */
    void checkOcclusion() {
        float elapsed = elapsedSeconds();
        if (elapsed < 30.0f || _flowRateMlH < PumpConfig::MIN_FLOW_RATE_ML_H) return;

        float actualMlH = _dropSensor.derivedFlowRateMlH(elapsed);
        float ratio     = actualMlH / _flowRateMlH;

        if (ratio < 0.10f) {
            _log.warning("OCCL",
                "Occlusion suspected: commanded=" +
                std::to_string(_flowRateMlH) +
                " mL/h, actual=" +
                std::to_string(actualMlH) + " mL/h");
            _alarm.trigger(ALARM_OCCLUSION);
            transition(PumpState::ALARM);
        }
    }
};


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 9 — HARDWARE SIMULATOR (desktop mode only)     ║
// ╚══════════════════════════════════════════════════════════╝

#ifndef ARDUINO

/**
 * @brief Simulates realistic sensor inputs for desktop testing.
 *
 * Generates a scenario that exercises all major code paths:
 *  - Normal infusion (ticks 0–30)
 *  - Flow rate spike above 200 mL/h (tick 35) → ALARM
 *  - Alarm acknowledgement → IDLE → restart
 *  - VTBI completion (remaining ticks)
 *
 * The random noise on ADC values mimics real potentiometer jitter.
 */
class HardwareSimulator {
public:
    HardwareSimulator()
        : _rng(42), // fixed seed for reproducibility
          _adcDist(400, 700),   // normal: ~130–170 mL/h range
          _dropDist(1, 3)       // 1–3 drops per 500 ms tick
    {}

    struct TickInput {
        int      rawADC;           ///< Simulated potentiometer reading
        uint32_t drops;            ///< Drops to register this tick
        bool     triggerHighFlow;  ///< True on the "spike" tick
    };

    /**
     * @brief Generate input for one simulation tick.
     * @param tick     Current tick index.
     * @param spikeAt  Tick at which to inject a high-flow event.
     */
    TickInput generateTick(uint32_t tick, uint32_t spikeAt) {
        TickInput in{};

        if (tick == spikeAt) {
            // Spike: ADC near max → flow rate ~210+ mL/h
            in.rawADC         = 1020;
            in.drops          = 5;
            in.triggerHighFlow = true;
        } else {
            in.rawADC         = _adcDist(_rng);
            in.drops          = _dropDist(_rng);
            in.triggerHighFlow = false;
        }

        // Advance simulated time
        g_simMillis += PumpConfig::SIM_TICK_STEP_MS;
        return in;
    }

private:
    std::mt19937                     _rng;
    std::uniform_int_distribution<>  _adcDist;
    std::uniform_int_distribution<>  _dropDist;
};


// ╔══════════════════════════════════════════════════════════╗
// ║  SECTION 10 — DESKTOP ENTRY POINT                       ║
// ╚══════════════════════════════════════════════════════════╝

/**
 * @brief Desktop simulation entry point.
 *
 * On Arduino, remove this main() and provide setup()/loop() instead
 * (see "Arduino porting" at the bottom of this file).
 */
int main() {
    PLATFORM_PRINTLN("====================================================");
    PLATFORM_PRINTLN("  MedFusion OS — Infusion Pump Firmware Simulation  ");
    PLATFORM_PRINTLN("====================================================\n");

    // ── Create and configure the pump ──────────────────────
    InfusionPump     pump;
    HardwareSimulator sim;

    constexpr float  TARGET_ML  = PumpConfig::SIM_TARGET_ML;  // 10 mL for quick demo
    constexpr uint32_t SPIKE_AT = 12;  // inject high-flow at tick 12

    pump.begin(TARGET_ML);
    pump.start();

    // ── Simulation loop ────────────────────────────────────
    for (uint32_t tick = 0; tick < PumpConfig::SIM_TOTAL_TICKS; ++tick) {

        // Check terminal states before generating input
        PumpState st = pump.state();
        if (st == PumpState::FAULT) {
            PLATFORM_PRINTLN("[SIM] Pump entered FAULT state — halting simulation.");
            break;
        }

        // Handle ALARM: acknowledge after 3 ticks, then restart if VTBI not done
        if (st == PumpState::ALARM) {
            static uint32_t alarmTick = 0;
            if (alarmTick == 0) alarmTick = tick;

            // Run alarm update for a few ticks (simulate operator response time)
            auto in = sim.generateTick(tick, SPIKE_AT);
            pump.tick(in.rawADC, 0); // no drops while alarming

            if (tick - alarmTick >= 3) {
                PLATFORM_PRINTLN("\n[SIM] Operator acknowledged alarm.");
                pump.acknowledgeAlarm();
                alarmTick = 0;
                if (pump.volumeDeliveredMl() < TARGET_ML) {
                    pump.start(); // resume
                }
            }
            continue;
        }

        // Handle COMPLETE
        if (st == PumpState::COMPLETE) {
            PLATFORM_PRINTLN("[SIM] Infusion complete. Simulation ending.");
            // Run a few more ticks to show the final state on the display
            for (int i = 0; i < 3; ++i) {
                auto in = sim.generateTick(tick + i, SPIKE_AT);
                pump.tick(in.rawADC, 0);
            }
            break;
        }

        // Normal running tick
        auto in = sim.generateTick(tick, SPIKE_AT);
        pump.tick(in.rawADC, in.drops);

        // Small real-time delay so the output is readable in the terminal
        PLATFORM_DELAY_MS(80);
    }

    // ── Final session summary ──────────────────────────────
    PLATFORM_PRINTLN("\n====================================================");
    PLATFORM_PRINTLN("  FINAL SESSION SUMMARY");
    PLATFORM_PRINTLN("====================================================");
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "  Volume delivered  : " << pump.volumeDeliveredMl() << " / "
           << pump.targetVolumeMl() << " mL";
        PLATFORM_PRINTLN(ss.str());
    }
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1);
        ss << "  Elapsed time      : " << pump.elapsedSeconds() << " s";
        PLATFORM_PRINTLN(ss.str());
    }
    {
        std::ostringstream ss;
        ss << "  Total ticks       : " << pump.tickCount();
        PLATFORM_PRINTLN(ss.str());
    }
    {
        std::ostringstream ss;
        ss << "  Final state       : " << pumpStateLabel(pump.state());
        PLATFORM_PRINTLN(ss.str());
    }
    PLATFORM_PRINTLN("====================================================\n");

    return 0;
}

#else

// ╔══════════════════════════════════════════════════════════╗
// ║  ARDUINO ENTRY POINTS (uncomment when porting to board) ║
// ╚══════════════════════════════════════════════════════════╝
//
// Volatile ISR counter for drop sensor — must be volatile because
// it is written in an ISR and read in the main loop.
//
// volatile uint32_t isrDropCount = 0;
//
// void dropSensorISR() { ++isrDropCount; }
//
// InfusionPump pump;
//
// void setup() {
//     Serial.begin(115200);
//     pump.begin(500.0f);  // 500 mL VTBI
//     attachInterrupt(digitalPinToInterrupt(PumpConfig::PIN_DROP_SENSOR),
//                     dropSensorISR, FALLING);
//     pump.start();
// }
//
// void loop() {
//     // Atomically read and clear the ISR drop counter
//     noInterrupts();
//     uint32_t drops = isrDropCount;
//     isrDropCount   = 0;
//     interrupts();
//
//     pump.tick(analogRead(PumpConfig::PIN_POTENTIOMETER), drops);
//     delay(PumpConfig::MAIN_LOOP_INTERVAL_MS);
// }

#endif // ARDUINO
