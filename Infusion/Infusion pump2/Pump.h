// File: Pump.h
// Project: Medical Infusion Pump Simulation
// Author: Elsu DEMİRCİ

#pragma once
#include <string>
#include "Sensors.h"

// The five states the pump can be in at any given time
enum class PumpState {
    Setup,      // Waiting for the user to configure flow rate and target volume
    Infusing,   // Actively delivering fluid
    Paused,     // Temporarily stopped by the user
    Alarm,      // Safety issue detected (occlusion, flow out of range, target reached)
    Done        // Infusion completed successfully
};

class InfusionPump {
public:
    InfusionPump(float targetVolume = PumpConfig::DEFAULT_TARGET_VOLUME_ML);

    // Called once before the simulation loop begins
    void begin();

    // Called every loop tick — reads sensors, updates state, checks safety
    void update();

    // User controls
    void start();
    void pause();
    void acknowledgeAlarm();  // clears alarm and returns to Setup so user can restart

    // Let external code push simulated sensor data in
    void setFlowRateDial(int potRawValue);
    void simulateDrop();

    // Getters for display / logging
    PumpState   getState() const;
    std::string getStateLabel() const;
    float       getCurrentFlowRate() const;
    float       getDeliveredVolume() const;
    float       getTargetVolume() const;
    std::string getAlarmMessage() const;

private:
    PumpState       state;
    Potentiometer   pot;
    DropSensor      dropSensor;
    float           targetVolume;
    std::string     alarmMessage;

    // Safety checks run on every update tick while infusing
    void checkFlowRateLimits();
    void checkOcclusion();
    void checkTargetReached();

    void triggerAlarm(const std::string& reason);
};
