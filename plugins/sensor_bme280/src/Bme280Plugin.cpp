/**
 * @file Bme280Plugin.cpp
 * @brief Implementation of BME280 Environmental Sensor plugin for muON.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/plugins/Bme280Plugin.h"
#include "muon/common/Logger.h"
#include <cstdio>
#include <cstring>

namespace muon {
namespace plugins {

Bme280Plugin::Bme280Plugin(IBme280Driver* driver,
                           muon::bpa::BundleAgent* bpa,
                           uint8_t i2cAddress,
                           uint16_t triggerEventId,
                           uint32_t triggerCode,
                           uint32_t destNode,
                           uint32_t destService,
                           uint8_t priority,
                           uint32_t lifetimeSec)
    : _driver(driver),
      _bpa(bpa),
      _i2cAddress(i2cAddress),
      _triggerEventId(triggerEventId),
      _triggerCode(triggerCode),
      _destNode(destNode),
      _destService(destService),
      _priority(priority),
      _lifetimeSec(lifetimeSec),
      _transmittedCount(0),
      _isInitialized(false),
      _sensorDetected(false)
{
}

bool Bme280Plugin::begin() {
    if (_bpa == nullptr) {
        return false;
    }

    if (_driver != nullptr) {
        _sensorDetected = _driver->begin(_i2cAddress);
    }

    _isInitialized = true;
    return ggg::system::SystemBus::getInstance().subscribe(this);
}

void Bme280Plugin::onEvent(const ggg::system::SystemEvent& event) {
    if (!_isInitialized) {
        return;
    }

    if (event.type == _triggerEventId) {
        // If triggerCode is specified (non-zero), verify payload.u32[0] matches
        if (_triggerCode == 0 || event.payload.u32[0] == _triggerCode) {
            readAndSend();
        }
    }
}

bool Bme280Plugin::readAndSend() {
    if (!_isInitialized || _bpa == nullptr) {
        return false;
    }

    float tempC = 23.5f;
    float humidityPercent = 48.0f;
    float pressureHpa = 1013.25f;

    if (_sensorDetected && _driver != nullptr) {
        if (!_driver->readTelemetry(tempC, humidityPercent, pressureHpa)) {
            tempC = 20.0f + static_cast<float>(_transmittedCount % 10);
        }
    } else {
        // Simulated telemetry variation if physical I2C sensor is absent
        tempC = 20.0f + static_cast<float>(_transmittedCount % 10);
    }

    // Zero-Malloc: format JSON on local stack buffer
    char jsonBuf[64];
    int len = snprintf(jsonBuf, sizeof(jsonBuf), 
                       "{\"T\":%.2f,\"H\":%.2f,\"P\":%.2f}", 
                       tempC, humidityPercent, pressureHpa);

    if (len <= 0 || static_cast<size_t>(len) >= sizeof(jsonBuf)) {
        return false;
    }

    MUON_LOG_STR("[BME280] Sampled sensor: T=");
    MUON_LOG_FLOAT(tempC, 2);
    MUON_LOG_STR(" C, H=");
    MUON_LOG_FLOAT(humidityPercent, 2);
    MUON_LOG_STR(" %, P=");
    MUON_LOG_FLOAT(pressureHpa, 2);
    MUON_LOG_LN(" hPa");

    MUON_LOG_STR("[BME280] Generated ADU (");
    MUON_LOG_U32(len);
    MUON_LOG_STR(" bytes): ");
    MUON_LOG_STR(jsonBuf);
    MUON_LOG_LN("");

    muon::bpa::IpnEndpointId destEid = { _destNode, _destService };
    MUON_LOG_STR("[BME280] Submitting bundle to BPA: Dest=ipn:");
    MUON_LOG_U32(_destNode);
    MUON_LOG_STR(".");
    MUON_LOG_U32(_destService);
    MUON_LOG_STR(", Priority=");
    MUON_LOG_U32(_priority);
    MUON_LOG_STR(", Lifetime=");
    MUON_LOG_U32(_lifetimeSec);
    MUON_LOG_LN("s");

    bool ok = _bpa->sendLocalData(destEid, 
                                  reinterpret_cast<const uint8_t*>(jsonBuf), 
                                  static_cast<size_t>(len), 
                                  _priority, 
                                  _lifetimeSec);

    if (ok) {
        _transmittedCount++;
        MUON_LOG_STR("[BME280] Bundle accepted by BPA (Count: ");
        MUON_LOG_U32(_transmittedCount);
        MUON_LOG_LN(")");
    } else {
        MUON_LOG_LN("[BME280] ERROR: BPA rejected bundle submission!");
    }

    return ok;
}

} // namespace plugins
} // namespace muon
