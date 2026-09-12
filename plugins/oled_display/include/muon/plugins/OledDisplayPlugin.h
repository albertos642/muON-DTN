/**
 * @file OledDisplayPlugin.h
 * @brief OLED Display status dashboard and DTN message receiver plugin for muON.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_PLUGINS_OLED_DISPLAY_PLUGIN_H
#define MUON_PLUGINS_OLED_DISPLAY_PLUGIN_H

#include <ggg/system/SystemBus.h>
#include <ggg/hal/IStorage.h>
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cstdio>

#if __has_include("autoconf.h")
#include "autoconf.h"
#endif

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#endif

// Fallbacks for compile-time constants if not set via Kconfig
#ifndef CONFIG_MUON_OLED_I2C_ADDRESS
#define CONFIG_MUON_OLED_I2C_ADDRESS 0x3C
#endif

#ifndef CONFIG_MUON_OLED_APP_SERVICE_ID
#define CONFIG_MUON_OLED_APP_SERVICE_ID 10
#endif

#ifndef CONFIG_MUON_OLED_REFRESH_RATE_HZ
#define CONFIG_MUON_OLED_REFRESH_RATE_HZ 2
#endif

namespace muon {
namespace plugins {

/**
 * @brief Telemetry and operational state snapshot presented on the OLED.
 */
struct OledDashboardData {
    char     roleStr[16];
    char     statusStr[12];
    uint16_t storageCount;
    uint16_t txSuccessCount;
    uint16_t txFailureCount;
    uint16_t rxReadyCount;
    int16_t  lastRssi;
    int8_t   lastSnr;
    char     lastMessage[48];
    uint32_t lastMessageTime;
    bool     hasNewMessage;
    // Enhanced Diagnostics
    uint32_t uptimeSec;
    char     heartbeatChar;
    char     lastActionStr[32];
    bool     isHostConnected;
};

/**
 * @brief Pure virtual renderer interface decoupling graphic drawing from hardware driver.
 */
class IOledRenderer {
public:
    virtual ~IOledRenderer() = default;
    virtual bool begin() = 0;
    virtual void draw(const OledDashboardData& data) = 0;
};

/**
 * @brief In-memory mock renderer for host execution and unit tests.
 */
class MockOledRenderer : public IOledRenderer {
public:
    MockOledRenderer() : _drawCount(0), _isInitialized(false) {
        memset(_lines, 0, sizeof(_lines));
    }

    bool begin() override {
        _isInitialized = true;
        return true;
    }

    void draw(const OledDashboardData& data) override {
        _drawCount++;
        _lastData = data;

        // Line 0: Header (Role & Status)
        snprintf(_lines[0], sizeof(_lines[0]), "[%s] %s", data.roleStr, data.statusStr);
        // Line 1: Metrics (Storage & TX/RX)
        snprintf(_lines[1], sizeof(_lines[1]), "Bdl:%u TX:%u RX:%u", 
                 data.storageCount, data.txSuccessCount, data.rxReadyCount);
        // Line 2: Signal & Errors
        snprintf(_lines[2], sizeof(_lines[2]), "RSSI:%d SNR:%d E:%u", 
                 data.lastRssi, data.lastSnr, data.txFailureCount);
        // Line 3: Last bundle payload
        snprintf(_lines[3], sizeof(_lines[3]), "Msg:%s", data.lastMessage);
    }

    size_t getDrawCount() const { return _drawCount; }
    bool isInitialized() const { return _isInitialized; }
    const char* getLine(size_t idx) const {
        return (idx < 4) ? _lines[idx] : "";
    }
    const OledDashboardData& getLastData() const { return _lastData; }

private:
    size_t            _drawCount;
    bool              _isInitialized;
    char              _lines[4][48];
    OledDashboardData _lastData;
};

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)

#if defined(CONFIG_MUON_OLED_HAS_RESET_PIN) && defined(CONFIG_MUON_OLED_PIN_RESET) && (CONFIG_MUON_OLED_PIN_RESET >= 0)
#define MUON_OLED_DEFAULT_RESET_PIN CONFIG_MUON_OLED_PIN_RESET
#else
#define MUON_OLED_DEFAULT_RESET_PIN -1
#endif

/**
 * @brief Production U8g2 renderer with Page/Full Buffer support for SSD1306/SSD1315.
 */
class U8g2OledRenderer : public IOledRenderer {
public:
    U8g2OledRenderer(uint8_t i2cAddress = CONFIG_MUON_OLED_I2C_ADDRESS, int8_t resetPin = MUON_OLED_DEFAULT_RESET_PIN)
        : _i2cAddress(i2cAddress), _resetPin(resetPin), _u8g2(nullptr) {}

    virtual ~U8g2OledRenderer() override = default;

    bool begin() override {
        // Many OLED modules do not expose the RST pin (4-pin I2C modules: VCC, GND, SCL, SDA).
        // If no reset pin is configured or resetPin < 0, pass U8X8_PIN_NONE to avoid wasting GPIOs.
        uint8_t rst = (_resetPin >= 0) ? (uint8_t)_resetPin : U8X8_PIN_NONE;

#if defined(CONFIG_MUON_OLED_RES_128X32)
    #if defined(CONFIG_MUON_OLED_BUFFER_FULL)
        static U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2Instance(U8G2_R0, rst);
    #else
        static U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C u8g2Instance(U8G2_R0, rst);
    #endif
#else
    #if defined(CONFIG_MUON_OLED_BUFFER_FULL)
        // Full Framebuffer: 1024 bytes RAM
        static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2Instance(U8G2_R0, rst);
    #else
        // Page Buffer: 128 bytes RAM (ideal for memory-constrained MCUs like SAMD21)
        static U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2Instance(U8G2_R0, rst);
    #endif
#endif
        _u8g2 = &u8g2Instance;
        _u8g2->setI2CAddress(_i2cAddress << 1);
        _u8g2->begin();
        return true;
    }

    void draw(const OledDashboardData& data) override {
        if (_u8g2 == nullptr) return;

        _u8g2->firstPage();
        do {
            _u8g2->setFont(u8g2_font_6x10_tf);

            // Row 1 (y=10): Role + Animated Heartbeat Spinner + Uptime / ION status
            char lineBuf[32];
            uint32_t mm = (data.uptimeSec / 60) % 100;
            uint32_t ss = data.uptimeSec % 60;
            if (data.roleStr[5] == 'B') {
                const char* hostStr = data.isHostConnected ? "ION:OK" : "ION:--";
                snprintf(lineBuf, sizeof(lineBuf), "Node B [%c] %s", 
                         data.heartbeatChar ? data.heartbeatChar : '*', hostStr);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%s [%c] %02lu:%02lu", 
                         data.roleStr, data.heartbeatChar ? data.heartbeatChar : '*',
                         (unsigned long)mm, (unsigned long)ss);
            }
            _u8g2->drawStr(0, 10, lineBuf);

            // Row 2 (y=23): Status & Storage Count
            snprintf(lineBuf, sizeof(lineBuf), "ST:%-7s  Bdl:%u", 
                     data.statusStr, data.storageCount);
            _u8g2->drawStr(0, 23, lineBuf);

            // Row 3 (y=36): Packet Counters (TX, RX, ERR)
            snprintf(lineBuf, sizeof(lineBuf), "TX:%-3u RX:%-3u E:%-2u", 
                     data.txSuccessCount, data.rxReadyCount, data.txFailureCount);
            _u8g2->drawStr(0, 36, lineBuf);

            // Row 4 (y=49): Radio Telemetry
            snprintf(lineBuf, sizeof(lineBuf), "RSSI:%-4d SNR:%-2d", 
                     data.lastRssi, data.lastSnr);
            _u8g2->drawStr(0, 49, lineBuf);

            // Row 5 (y=62): Last Action / Payload Message
            if (data.hasNewMessage && data.lastMessage[0] != '\0') {
                _u8g2->drawStr(0, 62, data.lastMessage);
            } else if (data.lastActionStr[0] != '\0') {
                _u8g2->drawStr(0, 62, data.lastActionStr);
            } else {
                _u8g2->drawStr(0, 62, "Waiting for traffic");
            }
        } while (_u8g2->nextPage());
    }

private:
    uint8_t _i2cAddress;
    int8_t  _resetPin;
    U8G2*   _u8g2;
};
#endif

/**
 * @brief OLED Display Plugin listening to SystemBus events and driving IOledRenderer.
 */
class OledDisplayPlugin : public ggg::system::IEventListener {
public:
    OledDisplayPlugin(IOledRenderer* renderer, 
                      ggg::hal::IStorage* storage, 
                      uint8_t appServiceId = CONFIG_MUON_OLED_APP_SERVICE_ID,
                      uint8_t refreshRateHz = CONFIG_MUON_OLED_REFRESH_RATE_HZ);

    virtual ~OledDisplayPlugin() override = default;

    /**
     * @brief Initialises the display and subscribes to the SystemBus.
     */
    bool begin();

    /**
     * @brief Event listener callback invoked when events are dispatched on SystemBus.
     */
    void onEvent(const ggg::system::SystemEvent& event) override;

    /**
     * @brief Periodic refresh call to update screen non-blockingly at controlled framerate.
     */
    void tick(uint32_t nowMs);

    /**
     * @brief Configures node role string shown on dashboard (e.g. "Node A (1.1)").
     */
    void setRoleString(const char* role);

    /**
     * @brief Configures operational status string shown on row 2 (e.g. "BOOT", "ERR: FLASH").
     */
    void setStatusString(const char* status) {
        if (status != nullptr) {
            strncpy(_data.statusStr, status, sizeof(_data.statusStr) - 1);
            _data.statusStr[sizeof(_data.statusStr) - 1] = '\0';
            if (strncmp(status, "ERR", 3) == 0 || strncmp(status, "FAULT", 5) == 0) {
                _hasHardwareError = true;
            }
            _isDirty = true;
        }
    }

    void setHardwareError(const char* status, const char* actionMsg = nullptr) {
        setStatusString(status);
        _hasHardwareError = true;
        if (actionMsg != nullptr) {
            strncpy(_data.lastActionStr, actionMsg, sizeof(_data.lastActionStr) - 1);
            _data.lastActionStr[sizeof(_data.lastActionStr) - 1] = '\0';
        }
        _isDirty = true;
    }

    bool hasHardwareError() const { return _hasHardwareError; }

    /**
     * @brief Updates last recorded RF telemetry (RSSI / SNR).
     */
    void updateRfTelemetry(int16_t rssi, int8_t snr);

    /**
     * @brief Forces an immediate display repaint.
     */
    void forceRedraw();

    /**
     * @brief Updates host connection state (UARTCL-COBS with IONe).
     */
    void setHostConnected(bool connected);

    const OledDashboardData& getData() const { return _data; }
    IOledRenderer* getRenderer() const { return _renderer; }

private:
    IOledRenderer*      _renderer;
    ggg::hal::IStorage* _storage;
    uint8_t             _appServiceId;
    uint32_t            _minRefreshIntervalMs;
    uint32_t            _lastDrawTimeMs;
    bool                _isDirty;
    bool                _isInitialized;
    bool                _hasHardwareError;
    OledDashboardData   _data;

    void updateStorageCount();
};

} // namespace plugins
} // namespace muon

#endif // MUON_PLUGINS_OLED_DISPLAY_PLUGIN_H
