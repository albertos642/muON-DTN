/**
 * @file RadioLibLoRaModem.h
 * @brief Zero-Malloc driver interfacing RadioLib physical modems.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_RADIOLIB_LORA_MODEM_H
#define MUON_RADIOLIB_LORA_MODEM_H

#include "muon/lora/ILoRaModem.h"

#if defined(ARDUINO) || !defined(TARGET_NATIVE)
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

namespace muon {
namespace lora {

/**
 * @brief Zero-Malloc Hardware Driver using RadioLib for Semtech modems.
 * Instantiates Module and Radio directly as object members without heap allocation.
 */
class RadioLibLoRaModem : public ILoRaModem {
private:
    enum class Action {
        IDLE,
        TX_IN_PROGRESS,
        RX_IN_PROGRESS
    };

    Module _module;

#if defined(CONFIG_MUON_LORA_MODEM_SX1278)
    mutable SX1278 _radio;
#elif defined(CONFIG_MUON_LORA_MODEM_SX1272)
    mutable SX1272 _radio;
#elif defined(CONFIG_MUON_LORA_MODEM_SX1262)
    mutable SX1262 _radio;
#else // Default: SX1276 (Adafruit Feather M0 RFM95W)
    mutable SX1276 _radio;
#endif

    IModemCallback* _callback;
    volatile Action _currentAction;
    LoRaConfig _lastConfig;
    float _cachedRssi;
    float _cachedSnr;

public:
    RadioLibLoRaModem(uint32_t csPin, uint32_t dio0Pin, uint32_t resetPin, uint32_t dio1Pin = RADIOLIB_NC);

    bool begin(const LoRaConfig& config, IModemCallback* callback) override;
    bool transmitAsync(const uint8_t* buffer, size_t length) override;
    size_t receive(uint8_t* buffer, size_t maxLength) override;
    void startReceive() override;
    void forceStandby() override;
    size_t getMTU() const override { return 255; }
    void handleInterrupt() override;
    uint32_t getTimeOnAirMs(size_t packetLength) const override;
    float getRSSI() const override { return _cachedRssi; }
    float getSNR() const override { return _cachedSnr; }
};

} // namespace lora
} // namespace muon

#endif // ARDUINO || !TARGET_NATIVE

#endif // MUON_RADIOLIB_LORA_MODEM_H
