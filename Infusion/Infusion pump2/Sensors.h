// File: Sensors.h
// Project: Medical Infusion Pump Simulation
// Author: Elsu DEMİRCİ

#pragma once
#include "Config.h"

// Simulates a potentiometer dial used to set the desired flow rate.
// In a real device this would read an ADC value; here we just store a raw value.
class Potentiometer {
public:
    Potentiometer();

    // Set a simulated raw reading (0–1023), as if someone turned the dial
    void setRawValue(int raw);

    // Returns raw ADC value
    int getRawValue() const;

    // Converts the raw 0-1023 reading to a flow rate in mL/hr
    float getFlowRate() const;

private:
    int rawValue;

    // Maps an integer from one range to another (linear interpolation)
    float mapToRange(int value, int inMin, int inMax, float outMin, float outMax) const;
};


// Simulates an optical drop sensor that counts drops passing through the IV line.
// Each detected drop is registered as a "tick". We use tick counts to estimate
// the volume delivered so far.
class DropSensor {
public:
    DropSensor();

    // Call this to simulate a drop being detected (e.g., from the drip chamber)
    void registerDrop();

    // Returns total number of drops counted since reset
    int getTotalDrops() const;

    // Converts drop count to estimated volume delivered in mL
    float getDeliveredVolume() const;

    // Resets the drop counter (used when starting a fresh infusion)
    void reset();

    // Increments the "no drop" counter; used to detect occlusion
    void incrementSilenceTick();

    // Resets the silence counter whenever a drop is detected
    void resetSilenceTick();

    // Returns how many consecutive checks have passed without a drop
    int getSilenceTicks() const;

private:
    int totalDrops;
    int silenceTicks;  // tracks how long the line has been quiet
};
