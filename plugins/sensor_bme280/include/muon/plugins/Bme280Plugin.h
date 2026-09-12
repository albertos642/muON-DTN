/**
 * @file Bme280Plugin.h
 * @brief BME280 Environmental Sensor plugin producing DTN telemetry bundles upon trigger.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_PLUGINS_BME280_PLUGIN_H
#define MUON_PLUGINS_BME280_PLUGIN_H

#include <ggg/system/SystemBus.h>
#include <muon/bpa/BundleAgent.h>
#include <stdint.h>
#include <stddef.h>

#if __has_include("autoconf.h")
#include "autoconf.h"
#endif

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#endif

// Compile-time fallbacks if not configured via Kconfig
#ifndef CONFIG_MUON_BME280_I2C_ADDRESS
#define CONFIG_MUON_BME280_I2C_ADDRESS 0x76
#endif

#ifndef CONFIG_MUON_BME280_TRIGGER_EVENT_ID
#define CONFIG_MUON_BME280_TRIGGER_EVENT_ID 0x0100
#endif

#ifndef CONFIG_MUON_BME280_TRIGGER_CODE
#define CONFIG_MUON_BME280_TRIGGER_CODE 1
#endif

#ifndef CONFIG_MUON_BME280_DEST_NODE
#define CONFIG_MUON_BME280_DEST_NODE 2
#endif

#ifndef CONFIG_MUON_BME280_DEST_SERVICE
#define CONFIG_MUON_BME280_DEST_SERVICE 1
#endif

#ifndef CONFIG_MUON_BME280_BUNDLE_PRIORITY
#define CONFIG_MUON_BME280_BUNDLE_PRIORITY 1
#endif

#ifndef CONFIG_MUON_BME280_BUNDLE_LIFETIME_SEC
#define CONFIG_MUON_BME280_BUNDLE_LIFETIME_SEC 3600
#endif

#ifndef CONFIG_MUON_BME280_PAYLOAD_TEMPLATE
#define CONFIG_MUON_BME280_PAYLOAD_TEMPLATE "{\"T\":{T},\"H\":{H},\"P\":{P}}"
#endif

namespace muon {
namespace plugins {

/**
 * @brief Hardware abstraction driver interface for BME280 sensor.
 */
class IBme280Driver {
public:
    virtual ~IBme280Driver() = default;
    virtual bool begin(uint8_t i2cAddress) = 0;
    virtual bool readTelemetry(float& tempC, float& humidityPercent, float& pressureHpa) = 0;
};

/**
 * @brief In-memory mock BME280 driver for host unit testing and simulation.
 */
class MockBme280Driver : public IBme280Driver {
public:
    MockBme280Driver(float tempC = 23.5f, float humidity = 48.2f, float pressure = 1013.25f)
        : _temp(tempC), _hum(humidity), _press(pressure), _readCount(0), _isInitialized(false) {}

    bool begin(uint8_t i2cAddress) override {
        (void)i2cAddress;
        _isInitialized = true;
        return true;
    }

    bool readTelemetry(float& tempC, float& humidityPercent, float& pressureHpa) override {
        if (!_isInitialized) return false;
        _readCount++;
        tempC = _temp;
        humidityPercent = _hum;
        pressureHpa = _press;
        return true;
    }

    void setMockValues(float tempC, float humidityPercent, float pressureHpa) {
        _temp = tempC;
        _hum = humidityPercent;
        _press = pressureHpa;
    }

    size_t getReadCount() const { return _readCount; }
    bool isInitialized() const { return _isInitialized; }

private:
    float  _temp;
    float  _hum;
    float  _press;
    size_t _readCount;
    bool   _isInitialized;
};

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)

/**
 * @brief Hardware implementation using Adafruit_BME280 library in low-power Forced Mode.
 */
class AdafruitBme280Driver : public IBme280Driver {
public:
    AdafruitBme280Driver() : _isInitialized(false) {}

    bool begin(uint8_t i2cAddress) override {
        if (!_bme.begin(i2cAddress)) {
            return false;
        }

        // Configure weather monitoring forced mode (ultra-low power: sleeps until triggered)
        _bme.setSampling(Adafruit_BME280::MODE_FORCED,
                         Adafruit_BME280::SAMPLING_X1, // Temperature
                         Adafruit_BME280::SAMPLING_X1, // Pressure
                         Adafruit_BME280::SAMPLING_X1, // Humidity
                         Adafruit_BME280::FILTER_OFF);

        _isInitialized = true;
        return true;
    }

    bool readTelemetry(float& tempC, float& humidityPercent, float& pressureHpa) override {
        if (!_isInitialized) return false;

        _bme.takeForcedMeasurement();
        tempC = _bme.readTemperature();
        humidityPercent = _bme.readHumidity();
        pressureHpa = _bme.readPressure() / 100.0f; // Convert Pa to hPa
        return true;
    }

private:
    Adafruit_BME280 _bme;
    bool            _isInitialized;
};
#endif

/**
 * @brief BME280 Environmental Sensor Plugin for muON.
 * Reacts to SystemBus trigger events, samples environmental data,
 * formats a compact string on the stack using a configurable template (Zero-Malloc),
 * and sends a DTN bundle.
 */
class Bme280Plugin : public ggg::system::IEventListener {
public:
    Bme280Plugin(IBme280Driver* driver,
                 muon::bpa::BundleAgent* bpa,
                 uint8_t i2cAddress = CONFIG_MUON_BME280_I2C_ADDRESS,
                 uint16_t triggerEventId = CONFIG_MUON_BME280_TRIGGER_EVENT_ID,
                 uint32_t triggerCode = CONFIG_MUON_BME280_TRIGGER_CODE,
                 uint32_t destNode = CONFIG_MUON_BME280_DEST_NODE,
                 uint32_t destService = CONFIG_MUON_BME280_DEST_SERVICE,
                 uint8_t priority = CONFIG_MUON_BME280_BUNDLE_PRIORITY,
                 uint32_t lifetimeSec = CONFIG_MUON_BME280_BUNDLE_LIFETIME_SEC,
                 const char* payloadTemplate = CONFIG_MUON_BME280_PAYLOAD_TEMPLATE);

    virtual ~Bme280Plugin() override = default;

    /**
     * @brief Initialises sensor hardware and subscribes to the SystemBus.
     */
    bool begin();

    /**
     * @brief SystemBus listener callback that reacts to the configured trigger event.
     */
    void onEvent(const ggg::system::SystemEvent& event) override;

    /**
     * @brief Manually triggers a telemetry read and DTN bundle transmission.
     */
    bool readAndSend();

    /**
     * @brief Sets the format template string for the generated payload.
     * Supported placeholders: {T} (Temp), {H} (Humidity), {P} (Pressure).
     */
    void setPayloadTemplate(const char* tpl);
    const char* getPayloadTemplate() const { return _payloadTemplate; }

    /**
     * @brief Static helper to format telemetry according to template into a stack buffer.
     */
    static size_t formatPayloadWithTemplate(const char* tpl, 
                                            float tempC, 
                                            float humidityPercent, 
                                            float pressureHpa, 
                                            char* outBuf, 
                                            size_t outSize);

    size_t getTransmittedCount() const { return _transmittedCount; }
    bool isInitialized() const { return _isInitialized; }
    bool isSensorDetected() const { return _sensorDetected; }

private:
    IBme280Driver*          _driver;
    muon::bpa::BundleAgent* _bpa;
    uint8_t                 _i2cAddress;
    uint16_t                _triggerEventId;
    uint32_t                _triggerCode;
    uint32_t                _destNode;
    uint32_t                _destService;
    uint8_t                 _priority;
    uint32_t                _lifetimeSec;
    char                    _payloadTemplate[96];
    size_t                  _transmittedCount;
    bool                    _isInitialized;
    bool                    _sensorDetected;
};


} // namespace plugins
} // namespace muon

#endif // MUON_PLUGINS_BME280_PLUGIN_H
