// File: Pump.cpp
// Project: Medical Infusion Pump Simulation
// Author: Elsu DEMİRCİ

#include "Pump.h"

InfusionPump::InfusionPump(float targetVolume)
    : state(PumpState::Setup),
      targetVolume(targetVolume),
      alarmMessage("") {}

void InfusionPump::begin() {
    dropSensor.reset();
    state = PumpState::Setup;
    alarmMessage = "";
}

void InfusionPump::update() {
    // Only run safety checks while the pump is actively infusing
    if (state != PumpState::Infusing) return;

    // Simulate the drop sensor "silence" accumulating each tick
    // In real hardware this would be interrupt-driven; here we just increment
    dropSensor.incrementSilenceTick();

    checkFlowRateLimits();
    checkOcclusion();
    checkTargetReached();
}

void InfusionPump::start() {
    if (state == PumpState::Setup || state == PumpState::Paused) {
        state = PumpState::Infusing;
        dropSensor.resetSilenceTick();
    }
}

void InfusionPump::pause() {
    if (state == PumpState::Infusing) {
        state = PumpState::Paused;
    }
}

void InfusionPump::acknowledgeAlarm() {
    // Reset the pump fully so the user can reconfigure and restart
    if (state == PumpState::Alarm) {
        alarmMessage = "";
        dropSensor.reset();
        state = PumpState::Setup;
    }
}

void InfusionPump::setFlowRateDial(int potRawValue) {
    pot.setRawValue(potRawValue);
}

void InfusionPump::simulateDrop() {
    // Registers a drop and resets the occlusion silence counter
    dropSensor.registerDrop();
}

// --- Safety checks ---

void InfusionPump::checkFlowRateLimits() {
    float rate = pot.getFlowRate();
    // If the dial reading is outside safe bounds, stop immediately
    if (rate < PumpConfig::MIN_FLOW_RATE_ML_HR || rate > PumpConfig::MAX_FLOW_RATE_ML_HR) {
        triggerAlarm("Flow rate out of safe range: " + std::to_string((int)rate) + " mL/hr");
    }
}

void InfusionPump::checkOcclusion() {
    // If too many ticks have passed without a drop, the line is probably blocked
    if (dropSensor.getSilenceTicks() >= PumpConfig::OCCLUSION_TIMEOUT_TICKS) {
        triggerAlarm("Possible occlusion — no drops detected for " +
                     std::to_string(PumpConfig::OCCLUSION_TIMEOUT_TICKS) + " ticks");
    }
}

void InfusionPump::checkTargetReached() {
    if (dropSensor.getDeliveredVolume() >= targetVolume) {
        state = PumpState::Done;  // clean completion, not an alarm
    }
}

void InfusionPump::triggerAlarm(const std::string& reason) {
    state = PumpState::Alarm;
    alarmMessage = reason;
}

// --- Getters ---

PumpState InfusionPump::getState() const {
    return state;
}

std::string InfusionPump::getStateLabel() const {
    switch (state) {
        case PumpState::Setup:     return "SETUP";
        case PumpState::Infusing:  return "INFUSING";
        case PumpState::Paused:    return "PAUSED";
        case PumpState::Alarm:     return "ALARM";
        case PumpState::Done:      return "DONE";
        default:                   return "UNKNOWN";
    }
}

float InfusionPump::getCurrentFlowRate() const {
    return pot.getFlowRate();
}

float InfusionPump::getDeliveredVolume() const {
    return dropSensor.getDeliveredVolume();
}

float InfusionPump::getTargetVolume() const {
    return targetVolume;
}

std::string InfusionPump::getAlarmMessage() const {
    return alarmMessage;
}
