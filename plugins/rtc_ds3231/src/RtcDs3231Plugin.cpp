/**
 * @file RtcDs3231Plugin.cpp
 * @brief Implementation of DS3231 RTC plugin and date/time conversions for muON.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/plugins/RtcDs3231Plugin.h"
#include <muon/bpa/MuonEvents.h>

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
#include <Arduino.h>
#define GET_MILLIS() millis()
#else
// Host test environment time base
static uint32_t s_mockMillis = 1000;
#define GET_MILLIS() (s_mockMillis += 10)
#endif

namespace muon {
namespace plugins {

static bool isLeapYear(uint16_t year) {
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

static const uint8_t daysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

uint32_t RtcDs3231Plugin::dateTimeToEpoch(uint16_t year, uint8_t month, uint8_t day,
                                         uint8_t hour, uint8_t minute, uint8_t second) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }

    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; ++y) {
        days += isLeapYear(y) ? 366 : 365;
    }

    for (uint8_t m = 1; m < month; ++m) {
        days += daysInMonth[m - 1];
        if (m == 2 && isLeapYear(year)) {
            days += 1;
        }
    }

    days += (day - 1);

    uint32_t epoch = days * 86400UL;
    epoch += hour * 3600UL;
    epoch += minute * 60UL;
    epoch += second;

    return epoch;
}

void RtcDs3231Plugin::epochToDateTime(uint32_t epoch, uint16_t& year, uint8_t& month, uint8_t& day,
                                      uint8_t& hour, uint8_t& minute, uint8_t& second) {
    second = epoch % 60;
    epoch /= 60;
    minute = epoch % 60;
    epoch /= 60;
    hour = epoch % 24;
    uint32_t days = epoch / 24;

    year = 1970;
    while (true) {
        uint16_t dInY = isLeapYear(year) ? 366 : 365;
        if (days >= dInY) {
            days -= dInY;
            year++;
        } else {
            break;
        }
    }

    month = 1;
    while (month <= 12) {
        uint8_t dInM = daysInMonth[month - 1];
        if (month == 2 && isLeapYear(year)) {
            dInM = 29;
        }

        if (days >= dInM) {
            days -= dInM;
            month++;
        } else {
            break;
        }
    }

    day = static_cast<uint8_t>(days + 1);
}

RtcDs3231Plugin::RtcDs3231Plugin(IRtcHardwareHal* hal,
                                 uint8_t i2cAddress,
                                 bool autoSyncUartcl,
                                 bool autoSyncBundle,
                                 uint32_t cacheSeconds)
    : _hal(hal),
      _i2cAddress(i2cAddress),
      _autoSyncUartcl(autoSyncUartcl),
      _autoSyncBundle(autoSyncBundle),
      _cacheIntervalMs((cacheSeconds > 0) ? (cacheSeconds * 1000) : 1000),
      _cachedEpoch(0),
      _lastHardwareReadMs(0),
      _isAuthoritative(false),
      _isInitialized(false),
      _syncCount(0)
{
}

bool RtcDs3231Plugin::begin() {
    if (_hal == nullptr) {
        return false;
    }

    if (!_hal->begin(_i2cAddress)) {
        return false;
    }

    _isInitialized = true;

    // Read initial time and check if oscillator is valid
    uint16_t y = 0;
    uint8_t m = 0, d = 0, hh = 0, mm = 0, ss = 0;
    bool oscStopped = true;

    if (_hal->readDateTime(y, m, d, hh, mm, ss, oscStopped)) {
        _cachedEpoch = dateTimeToEpoch(y, m, d, hh, mm, ss);
        _lastHardwareReadMs = GET_MILLIS();
        _isAuthoritative = (!oscStopped && y >= 2026);
    }

    return ggg::system::SystemBus::getInstance().subscribe(this);
}

uint32_t RtcDs3231Plugin::readHardwareEpoch() const {
    if (_hal == nullptr || !_isInitialized) {
        return _cachedEpoch;
    }

    uint16_t y = 0;
    uint8_t m = 0, d = 0, hh = 0, mm = 0, ss = 0;
    bool oscStopped = true;

    if (_hal->readDateTime(y, m, d, hh, mm, ss, oscStopped)) {
        _cachedEpoch = dateTimeToEpoch(y, m, d, hh, mm, ss);
        _lastHardwareReadMs = GET_MILLIS();
        if (!oscStopped && y >= 2026) {
            const_cast<RtcDs3231Plugin*>(this)->_isAuthoritative = true;
        }
    }

    return _cachedEpoch;
}

uint32_t RtcDs3231Plugin::getDtnTimestamp() const {
    if (!_isInitialized) {
        return static_cast<uint32_t>(GET_MILLIS() / 1000);
    }

    uint32_t nowMs = GET_MILLIS();
    uint32_t elapsedMs = nowMs - _lastHardwareReadMs;

    if (elapsedMs < _cacheIntervalMs) {
        // High efficiency: interpolate seconds from millis()
        return _cachedEpoch + (elapsedMs / 1000);
    }

    // Refresh from hardware I2C bus
    return readHardwareEpoch();
}

bool RtcDs3231Plugin::setDtnTimestamp(uint32_t timestamp) {
    if (_hal == nullptr || !_isInitialized || timestamp == 0) {
        return false;
    }

    uint16_t y = 0;
    uint8_t m = 0, d = 0, hh = 0, mm = 0, ss = 0;
    epochToDateTime(timestamp, y, m, d, hh, mm, ss);

    if (!_hal->writeDateTime(y, m, d, hh, mm, ss)) {
        return false;
    }

    _cachedEpoch = timestamp;
    _lastHardwareReadMs = GET_MILLIS();
    _isAuthoritative = true;
    _syncCount++;

    return true;
}

void RtcDs3231Plugin::onEvent(const ggg::system::SystemEvent& event) {
    if (!_isInitialized) {
        return;
    }

    // Source 1: Authoritative UARTCL time sync frame
    if (_autoSyncUartcl && event.type == muon::events::MUON_EVT_TIME_SYNC) {
        uint32_t dtnTime = event.payload.u32[0];
        if (dtnTime > 0) {
            setDtnTimestamp(dtnTime);
        }
        return;
    }

    // Source 2: Ingress bundle timestamp adoption if uncalibrated
    if (_autoSyncBundle && !_isAuthoritative && event.type == muon::events::MUON_EVT_RX_READY) {
        // If event carries candidate timestamp in payload.u32[1]
        uint32_t candidateTime = event.payload.u32[1];
        if (candidateTime > getDtnTimestamp()) {
            setDtnTimestamp(candidateTime);
        }
    }
}

} // namespace plugins
} // namespace muon
