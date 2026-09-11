/**
 * @file Crc16Ccitt.h
 * @brief Table-driven CRC-16 CCITT-FALSE calculation.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_CRC16_CCITT_H
#define MUON_CRC16_CCITT_H

#include <stdint.h>
#include <stddef.h>

namespace muon {
namespace uartcobs {

/**
 * @brief Fast CRC-16 CCITT-FALSE calculator.
 * Polynomial: 0x1021
 * Initial: 0xFFFF
 * RefIn: false, RefOut: false
 * XorOut: 0x0000
 * Test Vector: "123456789" -> 0x29B1
 */
class Crc16Ccitt {
public:
    static constexpr uint16_t INITIAL_VALUE = 0xFFFF;
    static constexpr uint16_t POLYNOMIAL = 0x1021;

    static uint16_t update(uint16_t currentCrc, uint8_t byte);
    static uint16_t update(uint16_t currentCrc, const uint8_t* buffer, size_t length);
    static uint16_t calculate(const uint8_t* buffer, size_t length, uint16_t initial = INITIAL_VALUE);
};

} // namespace uartcobs
} // namespace muon

#endif // MUON_CRC16_CCITT_H
