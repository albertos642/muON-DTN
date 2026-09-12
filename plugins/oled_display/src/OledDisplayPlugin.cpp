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
#include <muon/bpa/StorageStream.h>
#include <muon/bpa/CBORSerializer.h>
#include <muon/bpa/BundleAgent.h>
#include <cstdio>
#include <cstring>

namespace muon {
namespace plugins {

OledDisplayPlugin::OledDisplayPlugin(IOledRenderer* renderer, 
                                     ggg::hal::IStorage* storage, 
                                     uint8_t appServiceId,
                                     uint8_t refreshRateHz,
                                     muon::bpa::ITimeProvider* timeProvider,
                                     muon::bpa::BundleAgent* bpa)
    : _renderer(renderer),
      _storage(storage),
      _appServiceId(appServiceId),
      _minRefreshIntervalMs((refreshRateHz > 0) ? (1000 / refreshRateHz) : 500),
      _lastDrawTimeMs(0),
      _lastScrollTickMs(0),
      _isDirty(true),
      _isInitialized(false),
      _hasHardwareError(false),
      _timeProvider(timeProvider),
      _bpa(bpa)
{
    memset(&_data, 0, sizeof(_data));
    snprintf(_data.roleStr, sizeof(_data.roleStr), "muON Node");
    snprintf(_data.statusStr, sizeof(_data.statusStr), "BOOT");
    snprintf(_data.lastMessage, sizeof(_data.lastMessage), "No messages");
    snprintf(_data.rtcStr, sizeof(_data.rtcStr), "RTC:--");
    _data.isRtcAuthoritative = false;
    _data.scrollOffset = 0;
    _data.lastRssi = 0;
    _data.lastSnr = 0;
    _data.uptimeSec = 0;
    _data.heartbeatChar = '|';
    _data.isHostConnected = false;
    snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "Waiting traffic");
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

void OledDisplayPlugin::setHostConnected(bool connected) {
    if (_data.isHostConnected != connected) {
        _data.isHostConnected = connected;
        _isDirty = true;
    }
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
    if (!_isInitialized) {
        return;
    }

    // Update uptime
    uint32_t currentUptime = nowMs / 1000;
    if (currentUptime != _data.uptimeSec) {
        _data.uptimeSec = currentUptime;
        _isDirty = true;
    }

    // 4-phase spinner rotating every 250ms: |, /, -, backslash
    static const char spinnerChars[] = { '|', '/', '-', '\\' };
    char currentSpinner = spinnerChars[(nowMs / 250) % 4];
    if (currentSpinner != _data.heartbeatChar) {
        _data.heartbeatChar = currentSpinner;
        _isDirty = true;
    }

    // Query Real-Time Clock from time provider
    if (_timeProvider != nullptr) {
        uint32_t dtnTime = _timeProvider->getDtnTimestamp();
        bool isAuth = _timeProvider->isAuthoritative() || (dtnTime >= 1000000UL);
        if (isAuth) {
            uint32_t daySec = dtnTime % 86400;
            uint32_t hh = daySec / 3600;
            uint32_t mm = (daySec % 3600) / 60;
            uint32_t ss = daySec % 60;
            char tempRtc[12];
            snprintf(tempRtc, sizeof(tempRtc), "%02lu:%02lu:%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
            if (strcmp(_data.rtcStr, tempRtc) != 0 || !_data.isRtcAuthoritative) {
                strncpy(_data.rtcStr, tempRtc, sizeof(_data.rtcStr) - 1);
                _data.rtcStr[sizeof(_data.rtcStr) - 1] = '\0';
                _data.isRtcAuthoritative = true;
                _isDirty = true;
            }
        } else {
            if (_data.isRtcAuthoritative || strcmp(_data.rtcStr, "RTC:--") != 0) {
                strncpy(_data.rtcStr, "RTC:--", sizeof(_data.rtcStr) - 1);
                _data.rtcStr[sizeof(_data.rtcStr) - 1] = '\0';
                _data.isRtcAuthoritative = false;
                _isDirty = true;
            }
        }
    }

    // Marquee horizontal scroll for payload message
    size_t msgLen = strlen(_data.lastMessage);
    uint16_t textPxWidth = static_cast<uint16_t>(msgLen * 6);
    if (textPxWidth > 124) {
        if (nowMs - _lastScrollTickMs >= 80) {
            _lastScrollTickMs = nowMs;
            _data.scrollOffset += 2;
            if (_data.scrollOffset > textPxWidth + 24) {
                _data.scrollOffset = 0;
            }
            _isDirty = true;
        }
    } else {
        if (_data.scrollOffset != 0) {
            _data.scrollOffset = 0;
            _isDirty = true;
        }
    }

    if (!_isDirty) {
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
            if (!_hasHardwareError) {
                snprintf(_data.statusStr, sizeof(_data.statusStr), "IDLE");
                snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "System Ready");
            }
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_TX_SUCCESS:
            _data.txSuccessCount++;
            snprintf(_data.statusStr, sizeof(_data.statusStr), "TX OK");
            snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "TX OK H:%u", (unsigned int)event.payload.u32[0]);
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_TX_FAILURE:
            _data.txFailureCount++;
            snprintf(_data.statusStr, sizeof(_data.statusStr), "TX ERR");
            snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "TX ERR H:%u", (unsigned int)event.payload.u32[0]);
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_RX_READY:
            _data.rxReadyCount++;
            snprintf(_data.statusStr, sizeof(_data.statusStr), "RX OK");
            snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "RX H:%u", (unsigned int)event.payload.u32[0]);
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_ROUTE_REQ:
            snprintf(_data.statusStr, sizeof(_data.statusStr), "ROUTING");
            snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "FWD H:%u", (unsigned int)event.payload.u32[0]);
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_BUNDLE_EXPIRED:
            snprintf(_data.statusStr, sizeof(_data.statusStr), "EXPIRED");
            snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "EXP H:%u", (unsigned int)event.payload.u32[0]);
            updateStorageCount();
            _isDirty = true;
            break;

        case muon::events::MUON_EVT_BUNDLE_DELIVERED: {
            // payload.u32[0] = StorageHandle_t
            // payload.u32[1] = (destNode << 16) | destService
            uint16_t service = static_cast<uint16_t>(event.payload.u32[1] & 0xFFFF);
            snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "DLV S:%u H:%u", service, (unsigned int)event.payload.u32[0]);
            if (service == _appServiceId && _storage != nullptr) {
                ggg::hal::StorageHandle_t handle = static_cast<ggg::hal::StorageHandle_t>(event.payload.u32[0]);
                if (handle != GGG_INVALID_HANDLE) {
                    bool decoded = false;
                    muon::bpa::StorageInputStream inStream(*_storage, handle);
                    muon::bpa::BundleHeader header;
                    size_t payloadLength = 0;

                    if (muon::bpa::CBORSerializer::deserializeBundleHeader(inStream, header, payloadLength)) {
                        size_t toRead = (payloadLength < sizeof(_data.lastMessage) - 1) ? payloadLength : (sizeof(_data.lastMessage) - 1);
                        size_t bytesRead = inStream.readBytes(reinterpret_cast<uint8_t*>(_data.lastMessage), toRead);
                        _data.lastMessage[bytesRead] = '\0';
                        decoded = true;
                    }

                    if (!decoded) {
                        // Fallback for raw data in storage
                        size_t sz = _storage->getSize(handle);
                        if (sz > 0) {
                            size_t toRead = (sz < sizeof(_data.lastMessage) - 1) ? sz : (sizeof(_data.lastMessage) - 1);
                            _storage->readData(handle, 0, reinterpret_cast<uint8_t*>(_data.lastMessage), toRead);
                            _data.lastMessage[toRead] = '\0';
                            decoded = true;
                        }
                    }

                    if (decoded) {
                        // Sanitize newlines and unprintable characters
                        size_t msgLen = strlen(_data.lastMessage);
                        for (size_t i = 0; i < msgLen; i++) {
                            if (_data.lastMessage[i] == '\r' || _data.lastMessage[i] == '\n') {
                                _data.lastMessage[i] = ' ';
                            } else if (static_cast<uint8_t>(_data.lastMessage[i]) < 32 || static_cast<uint8_t>(_data.lastMessage[i]) > 126) {
                                _data.lastMessage[i] = '.';
                            }
                        }
                        // Trim trailing whitespace
                        while (msgLen > 0 && _data.lastMessage[msgLen - 1] == ' ') {
                            _data.lastMessage[--msgLen] = '\0';
                        }
                        _data.hasNewMessage = true;
                        _data.scrollOffset = 0;
                        snprintf(_data.statusStr, sizeof(_data.statusStr), "MSG RCVD");
                        _isDirty = true;
                    }

                    if (_bpa != nullptr) {
                        _bpa->consumeDeliveredBundle(handle);
                    }
                    updateStorageCount();
                }
            }
            break;
        }

        case ggg::system::GGG_EVT_HARDWARE_FAULT:
            snprintf(_data.statusStr, sizeof(_data.statusStr), "FAULT!");
            snprintf(_data.lastActionStr, sizeof(_data.lastActionStr), "HW Fault code:%u", (unsigned int)event.payload.u32[0]);
            _hasHardwareError = true;
            _isDirty = true;
            break;

        default:
            break;
    }
}

} // namespace plugins
} // namespace muon
