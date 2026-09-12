/**
 * @file LedActuatorPlugin.h
 * @brief Lightweight LED actuator plugin consuming DTN bundles and toggling GPIO.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_PLUGINS_LED_ACTUATOR_PLUGIN_H
#define MUON_PLUGINS_LED_ACTUATOR_PLUGIN_H

#include <ggg/core/IPlugin.h>
#include <ggg/system/SystemBus.h>
#include <ggg/hal/IStorage.h>
#include <muon/bpa/MuonEvents.h>
#include <stdint.h>

#if __has_include("autoconf.h")
#include "autoconf.h"
#endif

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
#include <Arduino.h>
#endif

#ifndef CONFIG_MUON_ACTUATOR_PIN
#define CONFIG_MUON_ACTUATOR_PIN 13
#endif

#ifndef CONFIG_MUON_ACTUATOR_SERVICE_ID
#define CONFIG_MUON_ACTUATOR_SERVICE_ID 2
#endif

namespace muon {
namespace bpa {
class BundleAgent;
}

namespace plugins {

/**
 * @brief Lightweight actuator plugin registering a specific DTN service ID.
 * Consumes delivered bundles addressed to its service and toggles a physical GPIO pin.
 */
class LedActuatorPlugin : public ggg::core::IPlugin {
private:
    uint8_t _pin;
    uint16_t _serviceId;
    bpa::BundleAgent* _bpa;
    ggg::hal::IStorage* _storage;
    bool _state;
    uint32_t _toggleCount;
    bool _isInitialized;

public:
    LedActuatorPlugin(uint8_t pin = CONFIG_MUON_ACTUATOR_PIN,
                      uint16_t serviceId = CONFIG_MUON_ACTUATOR_SERVICE_ID,
                      bpa::BundleAgent* bpa = nullptr,
                      ggg::hal::IStorage* storage = nullptr);

    // IPlugin interface
    bool begin() override;
    void onEvent(const ggg::system::SystemEvent& event) override;
    void tick() override {}
    void end();

    // Actuator controls
    void toggle();
    void setState(bool high);
    bool getState() const { return _state; }
    uint32_t getToggleCount() const { return _toggleCount; }
    uint16_t getServiceId() const { return _serviceId; }
    uint8_t getPin() const { return _pin; }

    void setBundleAgent(bpa::BundleAgent* bpa) { _bpa = bpa; }
    void setStorage(ggg::hal::IStorage* storage) { _storage = storage; }
};

} // namespace plugins
} // namespace muon

#endif // MUON_PLUGINS_LED_ACTUATOR_PLUGIN_H
