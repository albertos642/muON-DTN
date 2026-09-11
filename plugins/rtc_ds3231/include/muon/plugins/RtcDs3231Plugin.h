/**
 * @file RtcDs3231Plugin.h
 * @brief DS3231 High-Precision Real-Time Clock ITimeProvider plugin for muON-DTN.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_PLUGINS_RTC_DS3231_PLUGIN_H
#define MUON_PLUGINS_RTC_DS3231_PLUGIN_H

#include <ggg/system/SystemBus.h>
#include <muon/bpa/ITimeProvider.h>
#include <muon/bpa/BundleAgent.h>
#include <stdint.h>
#include <stddef.h>

#if __has_include("autoconf.h")
#include "autoconf.h"
#endif

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
#include <Arduino.h>
#include <Wire.h>
#endif

// Compile-time fallbacks if not configured via Kconfig
#ifndef CONFIG_MUON_RTC_DS3231_I2C_ADDRESS
#define CONFIG_MUON_RTC_DS3231_I2C_ADDRESS 0x68
#endif

#ifndef CONFIG_MUON_RTC_CACHE_SECONDS
#define CONFIG_MUON_RTC_CACHE_SECONDS 1
#endif

namespace muon {
namespace plugins {

/**
 * @brief Low-level Hardware Abstraction Interface for DS3231 I2C access.
 */
class IRtcHardwareHal {
public:
    virtual ~IRtcHardwareHal() = default;
    virtual bool begin(uint8_t i2cAddress) = 0;
    virtual bool readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                              uint8_t& hour, uint8_t& minute, uint8_t& second,
                              bool& oscillatorStopped) = 0;
    virtual bool writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t minute, uint8_t second) = 0;
};

/**
 * @brief In-memory mock RTC hardware for host desktop unit testing.
 */
class MockRtcHardwareHal : public IRtcHardwareHal {
public:
    MockRtcHardwareHal(uint16_t year = 2026, uint8_t month = 1, uint8_t day = 1,
                       uint8_t hour = 0, uint8_t minute = 0, uint8_t second = 0)
        : _year(year), _month(month), _day(day),
          _hour(hour), _minute(minute), _second(second),
          _oscStopped(false), _isInitialized(false), _writeCount(0), _readCount(0) {}

    bool begin(uint8_t i2cAddress) override {
        (void)i2cAddress;
        _isInitialized = true;
        return true;
    }

    bool readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                      uint8_t& hour, uint8_t& minute, uint8_t& second,
                      bool& oscillatorStopped) override {
        if (!_isInitialized) return false;
        _readCount++;
        year = _year;
        month = _month;
        day = _day;
        hour = _hour;
        minute = _minute;
        second = _second;
        oscillatorStopped = _oscStopped;
        return true;
    }

    bool writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                       uint8_t hour, uint8_t minute, uint8_t second) override {
        if (!_isInitialized) return false;
        _writeCount++;
        _year = year;
        _month = month;
        _day = day;
        _hour = hour;
        _minute = minute;
        _second = second;
        _oscStopped = false; // writing new time resets oscillator stop flag
        return true;
    }

    void setOscillatorStopped(bool stopped) { _oscStopped = stopped; }
    size_t getWriteCount() const { return _writeCount; }
    size_t getReadCount() const { return _readCount; }
    bool isInitialized() const { return _isInitialized; }

private:
    uint16_t _year;
    uint8_t  _month;
    uint8_t  _day;
    uint8_t  _hour;
    uint8_t  _minute;
    uint8_t  _second;
    bool     _oscStopped;
    bool     _isInitialized;
    size_t   _writeCount;
    size_t   _readCount;
};

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)

/**
 * @brief Zero-Malloc hardware I2C driver for DS3231 registers via standard Wire.
 */
class I2cRtcHardwareHal : public IRtcHardwareHal {
public:
    I2cRtcHardwareHal() : _i2cAddress(CONFIG_MUON_RTC_DS3231_I2C_ADDRESS), _isInitialized(false) {}

    bool begin(uint8_t i2cAddress) override {
        _i2cAddress = i2cAddress;
        Wire.begin();
        _isInitialized = true;
        return true;
    }

    bool readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                      uint8_t& hour, uint8_t& minute, uint8_t& second,
                      bool& oscillatorStopped) override {
        if (!_isInitialized) return false;

        // Start reading from register 0x00 (Seconds)
        Wire.beginTransmission(_i2cAddress);
        Wire.write(0x00);
        if (Wire.endTransmission() != 0) {
            return false;
        }

        if (Wire.requestFrom(_i2cAddress, (uint8_t)7) != 7) {
            return false;
        }

        second = bcd2bin(Wire.read() & 0x7F);
        minute = bcd2bin(Wire.read() & 0x7F);
        hour   = bcd2bin(Wire.read() & 0x3F); // 24hr format
        (void)Wire.read(); // skip day-of-week
        day    = bcd2bin(Wire.read() & 0x3F);
        uint8_t rawMonth = Wire.read();
        month  = bcd2bin(rawMonth & 0x1F);
        year   = 2000 + bcd2bin(Wire.read());

        // Check OSF (Oscillator Stop Flag) in Status Register (0x0F)
        Wire.beginTransmission(_i2cAddress);
        Wire.write(0x0F);
        Wire.endTransmission();
        if (Wire.requestFrom(_i2cAddress, (uint8_t)1) == 1) {
            uint8_t status = Wire.read();
            oscillatorStopped = (status & 0x80) != 0;
        } else {
            oscillatorStopped = false;
        }

        return true;
    }

    bool writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                       uint8_t hour, uint8_t minute, uint8_t second) override {
        if (!_isInitialized) return false;

        Wire.beginTransmission(_i2cAddress);
        Wire.write(0x00); // Start at register 0x00
        Wire.write(bin2bcd(second));
        Wire.write(bin2bcd(minute));
        Wire.write(bin2bcd(hour));
        Wire.write(1); // Day of week (arbitrary 1)
        Wire.write(bin2bcd(day));
        Wire.write(bin2bcd(month));
        Wire.write(bin2bcd(static_cast<uint8_t>(year >= 2000 ? year - 2000 : 0)));
        if (Wire.endTransmission() != 0) {
            return false;
        }

        // Clear OSF in Status Register (0x0F)
        Wire.beginTransmission(_i2cAddress);
        Wire.write(0x0F);
        Wire.write(0x00);
        return (Wire.endTransmission() == 0);
    }

private:
    uint8_t _i2cAddress;
    bool    _isInitialized;

    static inline uint8_t bcd2bin(uint8_t val) { return val - 6 * (val >> 4); }
    static inline uint8_t bin2bcd(uint8_t val) { return val + 6 * (val / 10); }
};
#endif

/**
 * @brief DS3231 RTC Plugin implementing muon::bpa::ITimeProvider.
 * Maintains accurate DTN time, listens to timesync frames, and provides millisecond interpolation.
 */
class RtcDs3231Plugin : public muon::bpa::ITimeProvider, public ggg::system::IEventListener {
public:
    RtcDs3231Plugin(IRtcHardwareHal* hal,
                    uint8_t i2cAddress = CONFIG_MUON_RTC_DS3231_I2C_ADDRESS,
                    bool autoSyncUartcl = true,
                    bool autoSyncBundle = true,
                    uint32_t cacheSeconds = CONFIG_MUON_RTC_CACHE_SECONDS);

    virtual ~RtcDs3231Plugin() override = default;

    /**
     * @brief Initialises the RTC hardware and subscribes to the SystemBus.
     */
    bool begin();

    /**
     * @brief ITimeProvider contract returning current DTN timestamp (seconds).
     */
    uint32_t getDtnTimestamp() const override;

    /**
     * @brief Sets the authoritative DTN time into the DS3231 hardware.
     */
    bool setDtnTimestamp(uint32_t timestamp);

    /**
     * @brief SystemBus listener callback processing timesync and ingress bundle events.
     */
    void onEvent(const ggg::system::SystemEvent& event) override;

    bool isAuthoritative() const { return _isAuthoritative; }
    size_t getSyncCount() const { return _syncCount; }
    bool isInitialized() const { return _isInitialized; }

    // --- Epoch <-> Calendar Conversions (Zero-Malloc) ---
    static uint32_t dateTimeToEpoch(uint16_t year, uint8_t month, uint8_t day,
                                    uint8_t hour, uint8_t minute, uint8_t second);

    static void epochToDateTime(uint32_t epoch, uint16_t& year, uint8_t& month, uint8_t& day,
                                uint8_t& hour, uint8_t& minute, uint8_t& second);

private:
    IRtcHardwareHal* _hal;
    uint8_t          _i2cAddress;
    bool             _autoSyncUartcl;
    bool             _autoSyncBundle;
    uint32_t         _cacheIntervalMs;

    mutable uint32_t _cachedEpoch;
    mutable uint32_t _lastHardwareReadMs;
    bool             _isAuthoritative;
    bool             _isInitialized;
    size_t           _syncCount;

    uint32_t readHardwareEpoch() const;
};

} // namespace plugins
} // namespace muon

#endif // MUON_PLUGINS_RTC_DS3231_PLUGIN_H
