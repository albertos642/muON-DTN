/**
 * @file ILoRaModem.h
 * @brief Hardware abstraction layer for Semtech LoRa PHY transceiver modems.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_I_LORA_MODEM_H
#define MUON_I_LORA_MODEM_H

#include <stdint.h>
#include <stddef.h>

namespace muon {
namespace lora {

/**
 * @brief RF physical parameters for LoRa modulation.
 */
struct LoRaConfig {
    float frequencyMHz;       // e.g. 868.0 or 434.0
    uint8_t spreadingFactor;  // 6 - 12 (default 9)
    float bandwidthKHz;       // e.g. 125.0, 250.0, 500.0
    uint8_t codingRate;       // 5 - 8 (4/5 to 4/8, default 5)
    int8_t txPowerDbm;        // e.g. 14, 20
    uint8_t syncWord;         // e.g. 0x12 (private networks)
    uint16_t preambleLength;  // default 8 symbols
};

/**
 * @brief Asynchronous callback interface invoked by modem driver.
 */
class IModemCallback {
public:
    virtual ~IModemCallback() = default;
    virtual void onTxDone() = 0;
    virtual void onRxDone(size_t length) = 0;
};

/**
 * @brief L1 (PHY) Hardware Abstraction Layer for LoRa modems.
 */
class ILoRaModem {
public:
    virtual ~ILoRaModem() = default;

    /**
     * @brief Initialises the LoRa modem with RF configuration and event callback.
     */
    virtual bool begin(const LoRaConfig& config, IModemCallback* callback) = 0;

    /**
     * @brief Asynchronously transmits a packet buffer. Returns immediately.
     */
    virtual bool transmitAsync(const uint8_t* buffer, size_t length) = 0;

    /**
     * @brief Reads a received packet from the modem hardware buffer.
     */
    virtual size_t receive(uint8_t* buffer, size_t maxLength) = 0;

    /**
     * @brief Enters continuous receive mode.
     */
    virtual void startReceive() = 0;

    /**
     * @brief Returns the maximum transmission unit supported by the modem.
     */
    virtual size_t getMTU() const = 0;

    /**
     * @brief Service routine invoked inside the DIO interrupt.
     */
    virtual void handleInterrupt() = 0;

    /**
     * @brief Calculates or queries Time on Air (ToA) in milliseconds for a given packet length.
     */
    virtual uint32_t getTimeOnAirMs(size_t packetLength) const = 0;

    /**
     * @brief Returns the RSSI of the last received packet in dBm.
     */
    virtual float getRSSI() const { return 0.0f; }

    /**
     * @brief Returns the SNR of the last received packet in dB.
     */
    virtual float getSNR() const { return 0.0f; }
};

} // namespace lora
} // namespace muon

#endif // MUON_I_LORA_MODEM_H
