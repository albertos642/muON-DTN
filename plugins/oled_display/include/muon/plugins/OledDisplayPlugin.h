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
#include <muon/bpa/ITimeProvider.h>
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
namespace bpa {
class BundleAgent;
}
}

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
    char     lastMessage[96];
    uint32_t lastMessageTime;
    bool     hasNewMessage;
    // Enhanced Diagnostics
    uint32_t uptimeSec;
    char     heartbeatChar;
    char     lastActionStr[32];
    bool     isHostConnected;
    // Real-Time Clock & Marquee
    char     rtcStr[12];
    bool     isRtcAuthoritative;
    uint16_t scrollOffset;
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
    char              _lines[4][96];
    OledDashboardData _lastData;
};

#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)

#if defined(CONFIG_MUON_OLED_HAS_RESET_PIN) && defined(CONFIG_MUON_OLED_PIN_RESET) && (CONFIG_MUON_OLED_PIN_RESET >= 0)
#define MUON_OLED_DEFAULT_RESET_PIN CONFIG_MUON_OLED_PIN_RESET
#else
#define MUON_OLED_DEFAULT_RESET_PIN -1
#endif

/**
 * @brief 8x8 Monochrome XBM Icons for high-contrast OLED Status Bars.
 * In XBM format, LSB (bit 0) is the leftmost pixel.
 */
static const uint8_t icon_mcu[8] = {
    0x42, // . * . . . . * .
    0x7E, // . * * * * * * .
    0x5A, // . * . * * . * .
    0x5A, // . * . * * . * .
    0x5A, // . * . * * . * .
    0x7E, // . * * * * * * .
    0x42, // . * . . . . * .
    0x00
};

static const uint8_t icon_clock[8] = {
    0x3C, // . . * * * * . .
    0x42, // . * . . . . * .
    0x99, // * . . * * . . *
    0x89, // * . . . * . . *
    0x81, // * . . . . . . *
    0x42, // . * . . . . * .
    0x3C, // . . * * * * . .
    0x00
};

static const uint8_t icon_link[8] = {
    0x24, // . . * . . * . .
    0x24, // . . * . . * . .
    0x7E, // . * * * * * * .
    0x7E, // . * * * * * * .
    0x3C, // . . * * * * . .
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0x00
};

static const uint8_t icon_disk[8] = {
    0x3C, // . . * * * * . .
    0x42, // . * . . . . * .
    0x7E, // . * * * * * * .
    0x42, // . * . . . . * .
    0x7E, // . * * * * * * .
    0x42, // . * . . . . * .
    0x3C, // . . * * * * . .
    0x00
};

static const uint8_t icon_tx[8] = {
    0x18, // . . . * * . . .
    0x3C, // . . * * * * . .
    0x7E, // . * * * * * * .
    0xDB, // * * . * * . * *
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0x00
};

static const uint8_t icon_rx[8] = {
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0xDB, // * * . * * . * *
    0x7E, // . * * * * * * .
    0x3C, // . . * * * * . .
    0x18, // . . . * * . . .
    0x00
};

static const uint8_t icon_warn[8] = {
    0x18, // . . . * * . . .
    0x24, // . . * . . * . .
    0x5A, // . * . * * . * .
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0x00, // . . . . . . . .
    0x18, // . . . * * . . .
    0x00
};

static const uint8_t icon_antenna[8] = {
    0xA5, // * . * . . * . *
    0x42, // . * . . . . * .
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0x18, // . . . * * . . .
    0x3C, // . . * * * * . .
    0x00
};

/**
 * @brief Production U8g2 renderer with Page/Full Buffer support for SSD1306/SSD1315.
 */
class U8g2OledRenderer : public IOledRenderer {
public:
    U8g2OledRenderer(uint8_t i2cAddress = CONFIG_MUON_OLED_I2C_ADDRESS, int8_t resetPin = MUON_OLED_DEFAULT_RESET_PIN)
        : _i2cAddress(i2cAddress), _resetPin(resetPin), _u8g2(nullptr) {}

    virtual ~U8g2OledRenderer() override = default;

    bool begin() override {
        uint8_t rst = (_resetPin >= 0) ? (uint8_t)_resetPin : U8X8_PIN_NONE;

#if defined(CONFIG_MUON_OLED_RES_128X32)
    #if defined(CONFIG_MUON_OLED_BUFFER_FULL)
        static U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2Instance(U8G2_R0, rst);
    #else
        static U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C u8g2Instance(U8G2_R0, rst);
    #endif
#else
    #if defined(CONFIG_MUON_OLED_BUFFER_FULL)
        static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2Instance(U8G2_R0, rst);
    #else
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
            // ================================================================
            // 1. TOP STATUS BAR (y=0..11, Divider at y=11)
            // ================================================================
            _u8g2->drawHLine(0, 11, 128);

            // Node Symbol & Identity (x=1..36)
            _u8g2->drawXBM(1, 1, 8, 8, icon_mcu);
            _u8g2->setFont(u8g2_font_5x7_tf);
            if (data.roleStr[5] == 'B') {
                _u8g2->drawStr(11, 8, "B:2.1");
            } else if (data.roleStr[5] == 'A') {
                _u8g2->drawStr(11, 8, "A:1.1");
            } else {
                _u8g2->drawStr(11, 8, data.roleStr);
            }

            // Real-Time Clock (x=42..88)
            _u8g2->drawXBM(42, 1, 8, 8, icon_clock);
            if (data.isRtcAuthoritative && data.rtcStr[0] != '\0') {
                _u8g2->drawStr(52, 8, data.rtcStr);
            } else {
                _u8g2->drawStr(52, 8, "RTC:--");
            }

            // Host Link Status (x=96..118)
            _u8g2->drawXBM(96, 1, 8, 8, icon_link);
            _u8g2->drawStr(106, 8, data.isHostConnected ? "OK" : "--");

            // Heartbeat spinner (x=122)
            char spin[2] = { data.heartbeatChar ? data.heartbeatChar : '*', '\0' };
            _u8g2->drawStr(122, 8, spin);

            // ================================================================
            // 2. CENTER DYNAMIC AREA (y=13..51)
            // ================================================================
            // Row 1 (y=24): State badge + Last Action Detail
            _u8g2->setFont(u8g2_font_6x10_tf);
            char stateBuf[64];
            snprintf(stateBuf, sizeof(stateBuf), "[%s] %s", data.statusStr, data.lastActionStr);
            _u8g2->drawStr(2, 24, stateBuf);

            // Row 2 (y=42): Payload Message with Horizontal Marquee
            const char* msgPtr = data.hasNewMessage && data.lastMessage[0] != '\0' 
                                 ? data.lastMessage 
                                 : "Waiting traffic...";
            int strPxWidth = _u8g2->getStrWidth(msgPtr);
            if (strPxWidth > 124) {
                // Marquee scrolling smoothly from right to left
                int xPos = 2 - static_cast<int>(data.scrollOffset);
                _u8g2->drawStr(xPos, 42, msgPtr);
            } else {
                _u8g2->drawStr(2, 42, msgPtr);
            }

            // ================================================================
            // 3. BOTTOM STATUS BAR (y=53..63, Divider at y=52)
            // ================================================================
            _u8g2->drawHLine(0, 52, 128);
            _u8g2->setFont(u8g2_font_5x7_tf);
            char numBuf[8];

            // Metric 1: Storage Count (x=1..22)
            _u8g2->drawXBM(1, 54, 8, 8, icon_disk);
            snprintf(numBuf, sizeof(numBuf), "%u", data.storageCount);
            _u8g2->drawStr(10, 61, numBuf);

            // Metric 2: TX Success Count (x=26..47)
            _u8g2->drawXBM(26, 54, 8, 8, icon_tx);
            snprintf(numBuf, sizeof(numBuf), "%u", data.txSuccessCount);
            _u8g2->drawStr(35, 61, numBuf);

            // Metric 3: RX Ready Count (x=51..72)
            _u8g2->drawXBM(51, 54, 8, 8, icon_rx);
            snprintf(numBuf, sizeof(numBuf), "%u", data.rxReadyCount);
            _u8g2->drawStr(60, 61, numBuf);

            // Metric 4: Error Count (x=76..96)
            _u8g2->drawXBM(76, 54, 8, 8, icon_warn);
            snprintf(numBuf, sizeof(numBuf), "%u", data.txFailureCount);
            _u8g2->drawStr(85, 61, numBuf);

            // Metric 5: LoRa RSSI/SNR (x=99..127)
            _u8g2->drawXBM(99, 54, 8, 8, icon_antenna);
            snprintf(numBuf, sizeof(numBuf), "%d", data.lastRssi);
            _u8g2->drawStr(108, 61, numBuf);

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
                      uint8_t refreshRateHz = CONFIG_MUON_OLED_REFRESH_RATE_HZ,
                      muon::bpa::ITimeProvider* timeProvider = nullptr,
                      muon::bpa::BundleAgent* bpa = nullptr);

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

    /**
     * @brief Sets time provider to query Real-Time Clock values for the top bar.
     */
    void setTimeProvider(muon::bpa::ITimeProvider* tp) {
        _timeProvider = tp;
    }

    /**
     * @brief Sets BPA instance to consume delivered bundles upon reception.
     */
    void setBundleAgent(muon::bpa::BundleAgent* bpa) {
        _bpa = bpa;
    }

    const OledDashboardData& getData() const { return _data; }
    IOledRenderer* getRenderer() const { return _renderer; }

private:
    IOledRenderer*            _renderer;
    ggg::hal::IStorage*       _storage;
    uint8_t                   _appServiceId;
    uint32_t                  _minRefreshIntervalMs;
    uint32_t                  _lastDrawTimeMs;
    uint32_t                  _lastScrollTickMs;
    bool                      _isDirty;
    bool                      _isInitialized;
    bool                      _hasHardwareError;
    muon::bpa::ITimeProvider* _timeProvider;
    muon::bpa::BundleAgent*   _bpa;
    OledDashboardData         _data;

    void updateStorageCount();
};

} // namespace plugins
} // namespace muon

#endif // MUON_PLUGINS_OLED_DISPLAY_PLUGIN_H
