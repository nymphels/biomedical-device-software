// File: Config.h
// Project: Medical Infusion Pump Simulation
// Author: Elsu DEMİRCİ

#pragma once

namespace PumpConfig {

    // Flow rate boundaries in mL/hr
    constexpr float MIN_FLOW_RATE_ML_HR = 1.0f;
    constexpr float MAX_FLOW_RATE_ML_HR = 250.0f;

    // Default infusion target if none is set by the user
    constexpr float DEFAULT_TARGET_VOLUME_ML = 100.0f;

    // How often the main loop checks sensor states (in milliseconds, simulated)
    constexpr int LOOP_INTERVAL_MS = 500;

    // Drop sensor: expected drops per mL (roughly typical for IV sets)
    constexpr float DROPS_PER_ML = 20.0f;

    // If no drop is detected for this many consecutive checks, raise an occlusion alarm
    constexpr int OCCLUSION_TIMEOUT_TICKS = 6;

    // Potentiometer simulates a dial that maps 0-1023 to MIN-MAX flow rate
    constexpr int POT_MIN_RAW = 0;
    constexpr int POT_MAX_RAW = 1023;

    // Pin definitions (kept here even though we simulate — good habit for hardware portability)
    // A0 = 14 on most Arduino boards; using the numeric value so this compiles outside the Arduino IDE
    constexpr int PIN_POTENTIOMETER = 14;
    constexpr int PIN_DROP_SENSOR   = 2;
    constexpr int PIN_BUZZER        = 8;
    constexpr int PIN_START_BTN     = 4;
    constexpr int PIN_PAUSE_BTN     = 5;

}
