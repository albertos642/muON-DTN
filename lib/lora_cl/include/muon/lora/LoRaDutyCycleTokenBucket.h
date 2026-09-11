/**
 * @file LoRaDutyCycleTokenBucket.h
 * @brief ETSI EN 300 220 1% EU868 duty cycle token bucket and ToA calculation.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_LORA_DUTY_CYCLE_TOKEN_BUCKET_H
#define MUON_LORA_DUTY_CYCLE_TOKEN_BUCKET_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "ILoRaModem.h"

namespace muon {
namespace lora {

/**
 * @brief Token Bucket regulator for ETSI EU868 1% Duty Cycle.
 * Max Capacity: 36,000 ms (1% of 1 hour).
 * Replenishment Rate: 1 ms every 100 ms of real time.
 */
class LoRaDutyCycleTokenBucket {
public:
    static constexpr uint32_t MAX_CAPACITY_MS = 36000;
    static constexpr uint32_t REPLENISH_INTERVAL_MS = 100; // 1 ms added per 100 ms

private:
    uint32_t _currentBudgetMs;
    uint32_t _accumulatedFractionMs;
    uint32_t _lastTickMs;
    bool _initialized;

public:
    explicit LoRaDutyCycleTokenBucket(uint32_t initialBudgetMs = MAX_CAPACITY_MS)
        : _currentBudgetMs(initialBudgetMs > MAX_CAPACITY_MS ? MAX_CAPACITY_MS : initialBudgetMs),
          _accumulatedFractionMs(0),
          _lastTickMs(0),
          _initialized(false) {}

    void reset(uint32_t initialBudgetMs = MAX_CAPACITY_MS) {
        _currentBudgetMs = (initialBudgetMs > MAX_CAPACITY_MS) ? MAX_CAPACITY_MS : initialBudgetMs;
        _accumulatedFractionMs = 0;
        _lastTickMs = 0;
        _initialized = false;
    }

    /**
     * @brief Updates the token bucket state given the current system timestamp in ms.
     */
    void update(uint32_t currentTickMs) {
        if (!_initialized) {
            _lastTickMs = currentTickMs;
            _initialized = true;
            return;
        }

        uint32_t elapsedMs = currentTickMs - _lastTickMs;
        _lastTickMs = currentTickMs;

        _accumulatedFractionMs += elapsedMs;
        uint32_t tokensToAdd = _accumulatedFractionMs / REPLENISH_INTERVAL_MS;
        _accumulatedFractionMs %= REPLENISH_INTERVAL_MS;

        _currentBudgetMs += tokensToAdd;
        if (_currentBudgetMs > MAX_CAPACITY_MS) {
            _currentBudgetMs = MAX_CAPACITY_MS;
        }
    }

    /**
     * @brief Checks if the budget allows transmitting a packet of expected ToA.
     */
    bool canTransmit(uint32_t toaMs) const {
        return _currentBudgetMs >= toaMs;
    }

    /**
     * @brief Deducts Time-on-Air from the available budget.
     */
    bool consume(uint32_t toaMs) {
        if (!canTransmit(toaMs)) {
            return false;
        }
        _currentBudgetMs -= toaMs;
        return true;
    }

    uint32_t getAvailableBudgetMs() const {
        return _currentBudgetMs;
    }

    static uint32_t calculateAnalyticalToAMs(const LoRaConfig& config, size_t payloadBytes) {
        // Symbol duration in milliseconds: Tsym = (2^SF) / BW_kHz
        double tSymMs = (double)(1UL << config.spreadingFactor) / config.bandwidthKHz;
        
        // Preamble duration
        double tPreambleMs = ((double)config.preambleLength + 4.25) * tSymMs;

        // Low Data Rate Optimization: enabled if symbol duration > 16 ms
        int de = (tSymMs > 16.0) ? 1 : 0;
        
        // Coding rate multiplier (config.codingRate is typically 5..8 for 4/5..4/8)
        int crVal = (config.codingRate >= 5) ? (config.codingRate - 4) : config.codingRate;
        if (crVal < 1) crVal = 1;
        if (crVal > 4) crVal = 4;

        int sf = config.spreadingFactor;
        int pl = (int)payloadBytes;
        
        // Explicit header (H = 0), CRC enabled (CRC = 1)
        int term = 8 * pl - 4 * sf + 28 + 16;
        if (term < 0) term = 0;
        
        int denom = 4 * (sf - 2 * de);
        if (denom <= 0) denom = 1;

        double nPayloadSymb = 8.0 + ceil((double)term / (double)denom) * (crVal + 4);
        if (nPayloadSymb < 8.0) nPayloadSymb = 8.0;

        double tPayloadMs = nPayloadSymb * tSymMs;
        double totalToAMs = tPreambleMs + tPayloadMs;

        return (uint32_t)ceil(totalToAMs);
    }
};

} // namespace lora
} // namespace muon

#endif // MUON_LORA_DUTY_CYCLE_TOKEN_BUCKET_H
