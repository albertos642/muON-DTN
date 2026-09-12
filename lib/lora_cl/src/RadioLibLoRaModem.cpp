/**
 * @file RadioLibLoRaModem.cpp
 * @brief Hardware driver implementation wrapping RadioLib transceiver routines.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/lora/RadioLibLoRaModem.h"

#if defined(ARDUINO) || !defined(TARGET_NATIVE)
#include "muon/lora/LoRaDutyCycleTokenBucket.h"
#include "muon/common/Logger.h"

namespace muon {
namespace lora {

RadioLibLoRaModem::RadioLibLoRaModem(uint32_t csPin, uint32_t dio0Pin, uint32_t resetPin, uint32_t dio1Pin)
    : _module(csPin, dio0Pin, resetPin, dio1Pin),
      _radio(&_module),
      _callback(nullptr),
      _currentAction(Action::IDLE),
      _lastConfig{},
      _cachedRssi(-120.0f),
      _cachedSnr(0.0f) {}

bool RadioLibLoRaModem::begin(const LoRaConfig& config, IModemCallback* callback) {
    _callback = callback;
    _lastConfig = config;

    int state = _radio.begin(
        config.frequencyMHz,
        config.bandwidthKHz,
        config.spreadingFactor,
        config.codingRate,
        config.syncWord > 0 ? config.syncWord : 0x12,
        config.txPowerDbm,
        config.preambleLength > 0 ? config.preambleLength : 8,
        0
    );

    if (state != RADIOLIB_ERR_NONE) {
        return false;
    }

    _radio.setCrcFiltering(true);
    return true;
}

bool RadioLibLoRaModem::transmitAsync(const uint8_t* buffer, size_t length) {
    if (length > getMTU()) {
        return false;
    }

    MUON_LOG_STR("[Radio] SX1276 startTransmit: len=");
    MUON_LOG_U32(length);
    MUON_LOG_STR(" B, freq=");
    MUON_LOG_FLOAT(_lastConfig.frequencyMHz, 1);
    MUON_LOG_STR(" MHz, SF=");
    MUON_LOG_U32(_lastConfig.spreadingFactor);
    MUON_LOG_LN("");

    _currentAction = Action::TX_IN_PROGRESS;
    int state = _radio.startTransmit(const_cast<uint8_t*>(buffer), length);
    if (state != RADIOLIB_ERR_NONE) {
        MUON_LOG_STR("[Radio] ERROR: startTransmit failed, state=");
        MUON_LOG_I32(state);
        MUON_LOG_LN("");
        _currentAction = Action::IDLE;
        return false;
    }

    return true;
}

void RadioLibLoRaModem::startReceive() {
    _currentAction = Action::RX_IN_PROGRESS;
    _radio.startReceive();
}

void RadioLibLoRaModem::forceStandby() {
    _currentAction = Action::IDLE;
    _radio.standby();
    _radio.finishTransmit();
}

size_t RadioLibLoRaModem::receive(uint8_t* buffer, size_t maxLength) {
    size_t length = _radio.getPacketLength();
    if (length == 0 || length > maxLength) {
        return 0;
    }

    // Cache packet signal metrics immediately while modem is in RX/Standby
    _cachedRssi = _radio.getRSSI();
    _cachedSnr = _radio.getSNR();

    int state = _radio.readData(buffer, length);
    if (state == RADIOLIB_ERR_NONE) {
        return length;
    }
    return 0;
}

void RadioLibLoRaModem::handleInterrupt() {
    if (_callback == nullptr) {
        return;
    }

    if (_currentAction == Action::TX_IN_PROGRESS) {
        _currentAction = Action::IDLE;
        _radio.finishTransmit();
        MUON_LOG_LN("[Radio] SX1276 DIO0 IRQ: TxDone (finishTransmit completed)");
        _callback->onTxDone();
    } else if (_currentAction == Action::RX_IN_PROGRESS) {
        size_t len = _radio.getPacketLength();
        MUON_LOG_STR("[Radio] SX1276 DIO0 IRQ: RxDone, packet len=");
        MUON_LOG_U32(len);
        MUON_LOG_LN(" B");
        _callback->onRxDone(len);
    }
}

uint32_t RadioLibLoRaModem::getTimeOnAirMs(size_t packetLength) const {
    size_t toaUs = _radio.getTimeOnAir(packetLength);
    if (toaUs > 0) {
        return (uint32_t)((toaUs + 999) / 1000);
    }
    return LoRaDutyCycleTokenBucket::calculateAnalyticalToAMs(_lastConfig, packetLength);
}

} // namespace lora
} // namespace muon

#endif // ARDUINO || !TARGET_NATIVE
