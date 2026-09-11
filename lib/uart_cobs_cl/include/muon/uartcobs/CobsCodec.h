/**
 * @file CobsCodec.h
 * @brief Consistent Overhead Byte Stuffing (COBS) stream encoder and decoder.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_COBS_CODEC_H
#define MUON_COBS_CODEC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

namespace muon {
namespace uartcobs {

/**
 * @brief Consistent Overhead Byte Stuffing (COBS) encoder and in-flight decoder.
 */
class CobsCodec {
public:
    /**
     * @brief Encodes a memory buffer into COBS format (without bounding 0x00 delimiters).
     * @return Number of bytes written to dst, or 0 if dstMax is insufficient.
     */
    static size_t encode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstMax);

    /**
     * @brief Decodes a COBS formatted buffer (without bounding 0x00 delimiters).
     * @return Number of decoded bytes written to dst, or 0 on decode error.
     */
    static size_t decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstMax);
};

/**
 * @brief Stream-based in-flight COBS decoder state machine.
 * Consumes 1 byte at a time with O(1) RAM.
 */
class CobsStreamDecoder {
public:
    enum class Result {
        NEED_MORE,      // Consumed byte, no decoded output available yet
        DECODED_BYTE,   // Successfully decoded 1 byte (returned in outByte)
        FRAME_START,    // Encountered delimiter 0x00, starting new frame
        FRAME_END,      // Encountered delimiter 0x00, ended active frame
        ERROR           // Framing / protocol violation
    };

private:
    bool _inFrame;
    uint8_t _code;
    bool _addZeroPending;
    bool _firstCode;
    uint8_t _pendingByte;
    bool _hasPendingByte;

public:
    CobsStreamDecoder() {
        reset();
    }

    void reset() {
        _inFrame = false;
        _code = 0;
        _addZeroPending = false;
        _firstCode = true;
        _pendingByte = 0;
        _hasPendingByte = false;
    }

    /**
     * @brief Feeds a raw wire byte into the decoder.
     * If DECODED_BYTE is returned, outByte contains the decoded payload byte.
     * Note: on certain boundaries (e.g. pending 0x00), multiple calls or hasPending() may apply.
     */
    Result feedByte(uint8_t wireByte, uint8_t& outByte);

    bool hasPendingByte() const {
        return _hasPendingByte;
    }

    uint8_t getPendingByte() {
        _hasPendingByte = false;
        return _pendingByte;
    }
};

} // namespace uartcobs
} // namespace muon

#endif // MUON_COBS_CODEC_H
