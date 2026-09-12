/**
 * @file LedActuatorPlugin.cpp
 * @brief Implementation of the lightweight LED actuator plugin.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/plugins/LedActuatorPlugin.h"
#include "muon/bpa/BundleAgent.h"

namespace muon {
namespace plugins {

LedActuatorPlugin::LedActuatorPlugin(uint8_t pin, uint16_t serviceId,
                                     bpa::BundleAgent *bpa,
                                     ggg::hal::IStorage *storage)
    : _pin(pin), _serviceId(serviceId), _bpa(bpa), _storage(storage),
      _state(false), _toggleCount(0), _isInitialized(false) {}

bool LedActuatorPlugin::begin() {
#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, _state ? HIGH : LOW);
#endif
  ggg::system::SystemBus::getInstance().subscribe(this);
  _isInitialized = true;
  return true;
}

void LedActuatorPlugin::end() {
  ggg::system::SystemBus::getInstance().unsubscribe(this);
  _isInitialized = false;
}

void LedActuatorPlugin::toggle() { setState(!_state); }

void LedActuatorPlugin::setState(bool high) {
  _state = high;
  _toggleCount++;
#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
  digitalWrite(_pin, _state ? HIGH : LOW);
#endif
}

void LedActuatorPlugin::onEvent(const ggg::system::SystemEvent &event) {
  if (event.type == muon::events::MUON_EVT_BUNDLE_DELIVERED) {
    // payload.u32[0] = StorageHandle_t
    // payload.u32[1] = (destNode << 16) | destService
    uint16_t destService = static_cast<uint16_t>(event.payload.u32[1] & 0xFFFF);
    if (destService == _serviceId) {
      toggle();

      ggg::hal::StorageHandle_t handle =
          static_cast<ggg::hal::StorageHandle_t>(event.payload.u32[0]);
      if (_bpa != nullptr) {
        _bpa->consumeDeliveredBundle(handle);
      } else if (_storage != nullptr && handle != GGG_INVALID_HANDLE) {
        _storage->deleteRecord(handle);
      }
    }
  }
}

} // namespace plugins
} // namespace muon
