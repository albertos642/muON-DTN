/**
 * @file OledDisplayPlugin.cpp
 * @brief Implementation of OLED Display status dashboard and DTN message receiver.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/plugins/OledDisplayPlugin.h"
#include <muon/bpa/MuonEvents.h>
#include <cstdio>
#include <cstring>

namespace muon {
namespace plugins {

OledDisplayPlugin::OledDisplayPlugin(IOledRenderer* renderer, 
                                     ggg::hal::IStorage* storage, 
                                     uint8_t appServiceId,
                                     uint8_t refreshRateHz)
    : _renderer(renderer),
      _storage(storage),
      _appServiceId(appServiceId),
      _minRefreshIntervalMs((refreshRateHz > 0) ? (1000 / refreshRateHz) : 500),
      _lastDrawTimeMs(0),
      _isDirty(true),
      _isInitialized(false)
{
    memset(&_data, 0, sizeof(_data));
    snprintf(_data.roleStr, sizeof(_data.roleStr), "muON Node");
    snprintf(_data.statusStr, sizeof(_data.statusStr), "BOOT");
    snprintf(_data.lastMessage, sizeof(_data.lastMessage), "No messages");
    _data.lastRssi = 0;
    _data.lastSnr = 0;
}

bool OledDisplayPlugin::begin() {
    if (_renderer == nullptr) {
        return false;
    }

    if (!_renderer->begin()) {
        return false;
    }

    _isInitialized = true;
    updateStorageCount();
    forceRedraw();

    return ggg::system::SystemBus::getInstance().subscribe(this);
}

void OledDisplayPlugin::setRoleString(const char* role) {
    if (role != nullptr) {
        strncpy(_data.roleStr, role, sizeof(_data.roleStr) - 1);
        _data.roleStr[sizeof(_data.roleStr) - 1] = '\0';
        _isDirty = true;
    }
}

void OledDisplayPlugin::updateRfTelemetry(int16_t rssi, int8_t snr) {
    _data.lastRssi = rssi;
    _data.lastSnr = snr;
    _isDirty = true;
}

void OledDisplayPlugin::updateStorageCount() {
    if (_storage != nullptr) {
        _data.storageCount = static_cast<uint16_t>(_storage->getCommittedCount());
    }
}

void OledDisplayPlugin::forceRedraw() {
    if (_renderer != nullptr) {
        updateStorageCount();
        _renderer->draw(_data);
        _isDirty = false;
    }
}

void OledDisplayPlugin::tick(uint32_t nowMs) {
    if (!_isInitialized || !_isDirty) {
        return;
    }

    if (nowMs - _lastDrawTimeMs >= _minRefreshIntervalMs) {
        _lastDrawTimeMs = nowMs;
        forceRedraw();
    }
}

void OledDisplayPlugin::onEvent(const ggg::system::SystemEvent& event) {
    switch (event.type) {
        case ggg::system::GGG_EVT_STARTUP:
            snprintf(_data.statusStr, sizeof(_data.statusStr), "IDLE");
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_TX_SUCCESS:
            _data.txSuccessCount++;
            snprintf(_data.statusStr, sizeof(_data.statusStr), "TX OK");
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_TX_FAILURE:
            _data.txFailureCount++;
            snprintf(_data.statusStr, sizeof(_data.statusStr), "TX ERR");
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_RX_READY:
            _data.rxReadyCount++;
            snprintf(_data.statusStr, sizeof(_data.statusStr), "RX OK");
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_BUNDLE_DELIVERED: {
            // payload.u32[0] = StorageHandle_t
            // payload.u32[1] = (destNode << 16) | destService
            uint16_t service = static_cast<uint16_t>(event.payload.u32[1] & 0xFFFF);
            if (service == _appServiceId && _storage != nullptr) {
                ggg::hal::StorageHandle_t handle = static_cast<ggg::hal::StorageHandle_t>(event.payload.u32[0]);
                size_t sz = _storage->getSize(handle);
                if (sz > 0) {
                    size_t toRead = (sz < sizeof(_data.lastMessage) - 1) ? sz : sizeof(_data.lastMessage) - 1;
                    _storage->readData(handle, 0, reinterpret_cast<uint8_t*>(_data.lastMessage), toRead);
                    _data.lastMessage[toRead] = '\0';
                    _data.hasNewMessage = true;
                    snprintf(_data.statusStr, sizeof(_data.statusStr), "MSG RCVD");
                    updateStorageCount();
                    _isDirty = true;
                }
            }
            break;
        }

        case ggg::system::GGG_EVT_HARDWARE_FAULT:
            snprintf(_data.statusStr, sizeof(_data.statusStr), "FAULT!");
            _isDirty = true;
            break;

        default:
            break;
    }
}

} // namespace plugins
} // namespace muon
