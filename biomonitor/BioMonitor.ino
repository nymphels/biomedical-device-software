/**
 * ============================================================
 *  BioMonitor — Pulse & Temperature Alert System
 * ============================================================
 *  Board   : Arduino Uno / Nano / Mega
 *  Purpose : Reads a simulated pulse sensor and a temperature
 *            sensor, prints readings to Serial Monitor, and
 *            triggers an alarm LED when thresholds are crossed.
 *
 *  Thresholds (configurable in config.h / top of file):
 *    - Heart rate  > 100 BPM  → alarm
 *    - Temperature > 39.0 °C  → alarm
 *
 *  Pin map:
 *    A0  — Pulse sensor analog output
 *    A1  — Temperature sensor analog output (e.g. LM35 / NTC)
 *    D13 — Alarm LED (built-in LED on most boards)
 *    D8  — Optional buzzer (active, 5 V)
 *
 *  Wiring notes:
 *    • LM35: OUT → A1, VCC → 5 V, GND → GND
 *             Formula: tempC = (Vout_mV) / 10.0
 *    • Pulse: many breakout boards output 0-5 V; peaks indicate
 *             heartbeats. This code counts peaks with a simple
 *             threshold approach (simulated here with analogRead).
 *    • LED  : 220 Ω resistor in series with D13 and GND.
 *
 *  Simulation mode (default ON):
 *    When SIMULATE is defined, random plausible values replace
 *    real sensor reads so the sketch runs without hardware.
 *    Remove the #define to use actual sensors.
 *
 *  Author  : <your name>
 *  License : MIT
 *  Repo    : https://github.com/<your-username>/BioMonitor
 * ============================================================
 */

// ── Simulation toggle ────────────────────────────────────
// Comment out this line to read from real hardware sensors.
#define SIMULATE

// ── Pin definitions ──────────────────────────────────────
constexpr uint8_t PIN_PULSE_SENSOR = A0;
constexpr uint8_t PIN_TEMP_SENSOR  = A1;
constexpr uint8_t PIN_ALARM_LED    = 13;
constexpr uint8_t PIN_BUZZER       = 8;   // set to 255 to disable

// ── Clinical thresholds ──────────────────────────────────
constexpr float   TEMP_THRESHOLD_C  = 39.0f;  // °C  (fever / hyperthermia)
constexpr uint8_t PULSE_THRESHOLD   = 100;    // BPM (tachycardia)

// ── Sampling configuration ───────────────────────────────
constexpr uint16_t SAMPLE_INTERVAL_MS   = 2000; // how often to log a reading
constexpr uint16_t PEAK_WINDOW_MS       = 500;  // window to detect one pulse peak
constexpr uint16_t PULSE_MEASURE_MS     = 10000;// BPM measurement window (10 s)

// ── ADC / voltage constants ──────────────────────────────
constexpr float ADC_REF_V    = 5.0f;    // Arduino reference voltage
constexpr float ADC_MAX      = 1023.0f; // 10-bit ADC
constexpr float MV_PER_COUNT = (ADC_REF_V / ADC_MAX) * 1000.0f; // mV per ADC step


// ╔══════════════════════════════════════════════════════════╗
// ║  class TemperatureSensor                                 ║
// ║  Reads an LM35-style analog temperature sensor.         ║
// ║  Override readRaw() for other sensor types (NTC, DS18). ║
// ╚══════════════════════════════════════════════════════════╝
class TemperatureSensor {
public:
    /**
     * @param pin      Analog pin the sensor is connected to.
     * @param threshold Alert temperature in °C.
     */
    explicit TemperatureSensor(uint8_t pin, float threshold)
        : _pin(pin), _threshold(threshold), _lastReading(0.0f) {}

    /** Initialize pin mode (analog pins don't need pinMode, but kept for clarity). */
    void begin() const {
        // analogRead works without explicit pinMode for analog pins.
    }

    /**
     * Reads the sensor and caches the result.
     * @return Temperature in °C.
     */
    float read() {
        _lastReading = convertTocelsius(readRaw());
        return _lastReading;
    }

    /** Returns the most recently read value without triggering a new read. */
    float lastReading() const { return _lastReading; }

    /** True if the cached temperature exceeds the configured threshold. */
    bool isAlert() const { return _lastReading > _threshold; }

    /** Configured alert threshold in °C. */
    float threshold() const { return _threshold; }

protected:
    /**
     * Returns a raw ADC value (0–1023).
     * In simulation mode this returns a pseudo-random plausible value.
     */
    virtual int readRaw() const {
#ifdef SIMULATE
        // Simulate ~36–40 °C (LM35 outputs 10 mV/°C → 360–400 mV → ~74–82 ADC)
        return random(71, 90);  // ~34.5 °C – 43.9 °C range, centred on normal
#else
        return analogRead(_pin);
#endif
    }

    /**
     * Converts a raw ADC reading to °C.
     * LM35 formula: Vout(mV) = Temp(°C) × 10  →  Temp = Vout_mV / 10
     */
    float convertTocelsius(int raw) const {
        float millivolts = static_cast<float>(raw) * MV_PER_COUNT;
        return millivolts / 10.0f;
    }

private:
    uint8_t _pin;
    float   _threshold;
    float   _lastReading;
};


// ╔══════════════════════════════════════════════════════════╗
// ║  class PulseSensor                                       ║
// ║  Counts heartbeat peaks in a fixed time window to       ║
// ║  derive BPM using a simple analog threshold approach.   ║
// ╚══════════════════════════════════════════════════════════╝
class PulseSensor {
public:
    /**
     * @param pin        Analog pin the sensor is connected to.
     * @param threshold  Alert BPM level.
     * @param peakLevel  ADC value above which a beat is detected (0–1023).
     */
    explicit PulseSensor(uint8_t pin, uint8_t threshold, int peakLevel = 550)
        : _pin(pin), _threshold(threshold), _peakLevel(peakLevel),
          _lastBPM(0), _beatCount(0), _windowStart(0), _inPeak(false) {}

    void begin() {
        _windowStart = millis();
    }

    /**
     * Call this as fast as possible in loop() — ideally every few ms.
     * Counts rising edges above _peakLevel within a rolling 10-second window,
     * then converts count → BPM when the window closes.
     *
     * @return true if a new BPM reading was just calculated.
     */
    bool update() {
        int raw = readRaw();

        // Detect rising edge (new peak)
        if (raw > _peakLevel && !_inPeak) {
            _inPeak = true;
            _beatCount++;
        } else if (raw <= _peakLevel) {
            _inPeak = false;
        }

        // Check if measurement window has elapsed
        unsigned long elapsed = millis() - _windowStart;
        if (elapsed >= PULSE_MEASURE_MS) {
            // BPM = beats counted in window × (60 000 ms / window duration ms)
            _lastBPM = static_cast<uint8_t>(
                (_beatCount * 60000UL) / elapsed
            );
            _beatCount   = 0;
            _windowStart = millis();
            return true;  // fresh reading available
        }
        return false;
    }

    /** Most recently calculated BPM. Updates every PULSE_MEASURE_MS ms. */
    uint8_t lastBPM() const { return _lastBPM; }

    /** True if the last BPM reading exceeds the configured threshold. */
    bool isAlert() const { return _lastBPM > _threshold; }

    /** Configured alert threshold in BPM. */
    uint8_t threshold() const { return _threshold; }

protected:
    /** Returns raw ADC value; overridden by simulation. */
    virtual int readRaw() const {
#ifdef SIMULATE
        // Simulate a heartbeat signal: sine-like pulses at 60–110 BPM.
        // We simply return random peaks to mimic real sensor noise.
        // 5% of samples exceed the peak level to simulate beats.
        return (random(0, 100) < 5) ? 600 : random(300, 500);
#else
        return analogRead(_pin);
#endif
    }

private:
    uint8_t       _pin;
    uint8_t       _threshold;
    int           _peakLevel;
    uint8_t       _lastBPM;
    uint16_t      _beatCount;
    unsigned long _windowStart;
    bool          _inPeak;
};


// ╔══════════════════════════════════════════════════════════╗
// ║  class AlarmController                                   ║
// ║  Drives an LED (and optional buzzer) with a non-        ║
// ║  blocking blink pattern when an alert is active.        ║
// ╚══════════════════════════════════════════════════════════╝
class AlarmController {
public:
    /**
     * @param ledPin    Digital output pin for the alarm LED.
     * @param buzzerPin Digital output pin for an active buzzer.
     *                  Pass 255 to disable buzzer.
     * @param blinkMs   LED on/off duration in milliseconds.
     */
    explicit AlarmController(uint8_t ledPin, uint8_t buzzerPin = 255,
                             uint16_t blinkMs = 300)
        : _ledPin(ledPin), _buzzerPin(buzzerPin), _blinkMs(blinkMs),
          _active(false), _ledState(false), _lastToggle(0) {}

    void begin() const {
        pinMode(_ledPin, OUTPUT);
        digitalWrite(_ledPin, LOW);
        if (_buzzerPin != 255) {
            pinMode(_buzzerPin, OUTPUT);
            digitalWrite(_buzzerPin, LOW);
        }
    }

    /**
     * Set alarm state.  Must call update() in loop() to run the blink pattern.
     * @param state true = alarm on, false = alarm off.
     */
    void setAlarm(bool state) {
        _active = state;
        if (!state) {
            // Immediately silence everything when alarm is cleared
            digitalWrite(_ledPin, LOW);
            if (_buzzerPin != 255) digitalWrite(_buzzerPin, LOW);
            _ledState = false;
        }
    }

    bool isActive() const { return _active; }

    /**
     * Non-blocking blink driver.  Call every loop iteration.
     */
    void update() {
        if (!_active) return;

        unsigned long now = millis();
        if (now - _lastToggle >= _blinkMs) {
            _ledState = !_ledState;
            digitalWrite(_ledPin, _ledState ? HIGH : LOW);
            if (_buzzerPin != 255) {
                digitalWrite(_buzzerPin, _ledState ? HIGH : LOW);
            }
            _lastToggle = now;
        }
    }

private:
    uint8_t       _ledPin;
    uint8_t       _buzzerPin;
    uint16_t      _blinkMs;
    bool          _active;
    bool          _ledState;
    unsigned long _lastToggle;
};


// ╔══════════════════════════════════════════════════════════╗
// ║  class SerialLogger                                      ║
// ║  Formats and prints readings to Serial Monitor.         ║
// ╚══════════════════════════════════════════════════════════╝
class SerialLogger {
public:
    explicit SerialLogger(uint32_t baudRate = 9600) : _baud(baudRate) {}

    void begin() const {
        Serial.begin(_baud);
        while (!Serial) {}  // wait for USB serial on Leonardo / Micro

        Serial.println(F("================================================"));
        Serial.println(F("  BioMonitor — Pulse & Temperature Alert System"));
        Serial.println(F("================================================"));
        Serial.println(F("  Time(ms) | Temp(C) | BPM  | Status"));
        Serial.println(F("------------------------------------------------"));
    }

    /**
     * Logs one reading row.
     * @param tempC      Temperature in °C.
     * @param bpm        Heart rate in BPM.
     * @param alarmOn    Whether any threshold is currently breached.
     * @param tempAlert  Which sensor triggered (for the message).
     * @param pulseAlert Which sensor triggered (for the message).
     */
    void log(float tempC, uint8_t bpm, bool alarmOn,
             bool tempAlert, bool pulseAlert) const {
        char buf[56];
        // Right-pad timestamp in a fixed column
        snprintf(buf, sizeof(buf), "  %8lu |  %5.2f  |  %3u | ",
                 millis(), tempC, bpm);
        Serial.print(buf);

        if (!alarmOn) {
            Serial.println(F("OK"));
        } else {
            Serial.print(F("ALERT"));
            if (tempAlert)  Serial.print(F(" [HIGH TEMP]"));
            if (pulseAlert) Serial.print(F(" [HIGH BPM]"));
            Serial.println();
        }
    }

private:
    uint32_t _baud;
};


// ╔══════════════════════════════════════════════════════════╗
// ║  Global instances                                        ║
// ╚══════════════════════════════════════════════════════════╝
TemperatureSensor tempSensor(PIN_TEMP_SENSOR, TEMP_THRESHOLD_C);
PulseSensor       pulseSensor(PIN_PULSE_SENSOR, PULSE_THRESHOLD);
AlarmController   alarm(PIN_ALARM_LED, PIN_BUZZER, 250);
SerialLogger      logger(9600);

// Tracks when we last printed a summary line (independent of BPM window)
unsigned long lastLogTime = 0;


// ╔══════════════════════════════════════════════════════════╗
// ║  setup()                                                 ║
// ╚══════════════════════════════════════════════════════════╝
void setup() {
    randomSeed(analogRead(2)); // seed RNG from floating pin (simulation only)

    tempSensor.begin();
    pulseSensor.begin();
    alarm.begin();
    logger.begin();
}


// ╔══════════════════════════════════════════════════════════╗
// ║  loop()                                                  ║
// ╚══════════════════════════════════════════════════════════╝
void loop() {
    // 1. Keep the pulse-peak detector running every iteration
    bool newBPM = pulseSensor.update();

    // 2. Read temperature on each SAMPLE_INTERVAL tick
    unsigned long now = millis();
    if (now - lastLogTime >= SAMPLE_INTERVAL_MS || newBPM) {

        float   tempC = tempSensor.read();
        uint8_t bpm   = pulseSensor.lastBPM();

        bool tempAlert  = tempSensor.isAlert();
        bool pulseAlert = pulseSensor.isAlert();
        bool anyAlert   = tempAlert || pulseAlert;

        // 3. Update alarm state
        alarm.setAlarm(anyAlert);

        // 4. Log to Serial Monitor
        logger.log(tempC, bpm, anyAlert, tempAlert, pulseAlert);

        lastLogTime = now;
    }

    // 5. Drive the non-blocking LED blink (must be called every loop)
    alarm.update();
}
