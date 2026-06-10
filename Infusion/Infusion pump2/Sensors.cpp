// File: Sensors.cpp
// Project: Medical Infusion Pump Simulation
// Author: Elsu DEMİRCİ

#include "Sensors.h"

// --- Potentiometer ---

Potentiometer::Potentiometer() : rawValue(0) {}

void Potentiometer::setRawValue(int raw) {
    // Clamp to valid ADC range
    if (raw < PumpConfig::POT_MIN_RAW) raw = PumpConfig::POT_MIN_RAW;
    if (raw > PumpConfig::POT_MAX_RAW) raw = PumpConfig::POT_MAX_RAW;
    rawValue = raw;
}

int Potentiometer::getRawValue() const {
    return rawValue;
}

float Potentiometer::getFlowRate() const {
    return mapToRange(rawValue,
                      PumpConfig::POT_MIN_RAW, PumpConfig::POT_MAX_RAW,
                      PumpConfig::MIN_FLOW_RATE_ML_HR, PumpConfig::MAX_FLOW_RATE_ML_HR);
}

float Potentiometer::mapToRange(int value, int inMin, int inMax, float outMin, float outMax) const {
    // Standard linear map: scales value proportionally from input range to output range
    return outMin + (float)(value - inMin) / (float)(inMax - inMin) * (outMax - outMin);
}


// --- DropSensor ---

DropSensor::DropSensor() : totalDrops(0), silenceTicks(0) {}

void DropSensor::registerDrop() {
    totalDrops++;
    silenceTicks = 0;  // reset silence counter since we just saw a drop
}

int DropSensor::getTotalDrops() const {
    return totalDrops;
}

float DropSensor::getDeliveredVolume() const {
    // Convert accumulated drops to mL using the configured drops-per-mL constant
    return (float)totalDrops / PumpConfig::DROPS_PER_ML;
}

void DropSensor::reset() {
    totalDrops = 0;
    silenceTicks = 0;
}

void DropSensor::incrementSilenceTick() {
    silenceTicks++;
}

void DropSensor::resetSilenceTick() {
    silenceTicks = 0;
}

int DropSensor::getSilenceTicks() const {
    return silenceTicks;
}
