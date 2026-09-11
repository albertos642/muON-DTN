/**
 * @file LoRaFraming.h
 * @brief Polymorphic physical header definitions, bitmasks, and reason codes for LoRa.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_LORA_FRAMING_H
#define MUON_LORA_FRAMING_H

#include <stdint.h>
#include <stddef.h>

namespace muon {
namespace lora {

// Byte 0: Control Word Masks
#define LORA_TYPE_MASK        0xC0 // Bits 7-6
#define LORA_TYPE_SEGMENT     0x00 // MT 00: XFER_SEGMENT
#define LORA_TYPE_ACK         0x40 // MT 01: BDL_XFER_ACK
#define LORA_TYPE_REFUSE      0x80 // MT 10: XFER_REFUSE
#define LORA_TYPE_MSG_REJECT  0xC0 // MT 11: MSJ_REJECT

#define LORA_SC_MASK          0x20 // Bit 5 (0=Unreliable, 1=Notified)
#define LORA_SC_UNRELIABLE    0x00
#define LORA_SC_NOTIFIED      0x20

#define LORA_SESSION_MASK     0x1F // Bits 4-0 (Session ID 0-31)

// Reason Codes for XFER_REFUSE
#define LORA_REFUSE_INSUFFICIENT_SPACE      0x01
#define LORA_REFUSE_DUTY_CYCLE_EXHAUSTED    0x02
#define LORA_REFUSE_ADMIN_DISCARD           0x03
#define LORA_REFUSE_NACK_NOTIFIED           0x04

// Reason Codes for MSG_REJECT
#define LORA_REJECT_MISSING_SEGMENT         0x10
#define LORA_REJECT_UNKNOWN_TRANSFER        0x11

// Packed Polymorphic Header (Memory-Safe)
struct __attribute__((packed)) LoRaCL_Header {
    uint8_t control_session; // Byte 0 (Present in all messages)

    union {
        // XFER_SEGMENT format (Total 3 bytes header)
        struct {
            uint8_t total_segments; // Byte 1 (Total count, 1..256 represented as 1..255 or 0 for 256)
            uint8_t segment_index;  // Byte 2 (0-indexed segment sequence number)
        } data;

        // Error signaling format (Total 2 bytes header, multiplexed on Byte 1)
        uint8_t reason_code;        // Byte 1
    };
};

// Helper utilities for header manipulation
inline uint8_t makeControlWord(uint8_t type, uint8_t sc, uint8_t sessionId) {
    return (type & LORA_TYPE_MASK) | (sc & LORA_SC_MASK) | (sessionId & LORA_SESSION_MASK);
}

inline uint8_t getMessageType(uint8_t controlWord) {
    return controlWord & LORA_TYPE_MASK;
}

inline uint8_t getServiceClass(uint8_t controlWord) {
    return controlWord & LORA_SC_MASK;
}

inline uint8_t getSessionId(uint8_t controlWord) {
    return controlWord & LORA_SESSION_MASK;
}

inline size_t getHeaderLength(uint8_t messageType) {
    switch (messageType) {
        case LORA_TYPE_ACK:
            return 1;
        case LORA_TYPE_REFUSE:
        case LORA_TYPE_MSG_REJECT:
            return 2;
        case LORA_TYPE_SEGMENT:
        default:
            return 3;
    }
}

} // namespace lora
} // namespace muon

#endif // MUON_LORA_FRAMING_H
