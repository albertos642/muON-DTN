/**
 * @file UartCobsFraming.h
 * @brief Serial frame format, delimiters, version bytes, and protocol flags.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_UART_COBS_FRAMING_H
#define MUON_UART_COBS_FRAMING_H

#include <stdint.h>
#include <stddef.h>

namespace muon {
namespace uartcobs {

// Delimiter byte for COBS framed packets
#define UARTCL_DELIMITER        0x00

// Control Header bitmasks (Byte 0 of decoded stream)
#define UARTCL_VERSION_MASK     0xF0 // Bits 7-4
#define UARTCL_VERSION_1        0x10

#define UARTCL_FLAG_MASK        0x08 // Bit 3
#define UARTCL_FLAG_DATA        0x00 // Frame contains CBOR Bundle + CRC
#define UARTCL_FLAG_SYNC        0x08 // Sync Request/Response (DTN epoch time)

inline uint8_t makeControlHeader(uint8_t flag) {
    return UARTCL_VERSION_1 | (flag & UARTCL_FLAG_MASK);
}

inline bool isValidVersion(uint8_t header) {
    return (header & UARTCL_VERSION_MASK) == UARTCL_VERSION_1;
}

inline bool isSyncFrame(uint8_t header) {
    return (header & UARTCL_FLAG_MASK) == UARTCL_FLAG_SYNC;
}

inline bool isDataFrame(uint8_t header) {
    return (header & UARTCL_FLAG_MASK) == UARTCL_FLAG_DATA;
}

} // namespace uartcobs
} // namespace muon

#endif // MUON_UART_COBS_FRAMING_H
