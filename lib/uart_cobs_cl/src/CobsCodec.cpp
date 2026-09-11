/**
 * @file CobsCodec.cpp
 * @brief Implementation of RFC-standard COBS framing and deframing algorithms.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/uartcobs/CobsCodec.h"

namespace muon {
namespace uartcobs {

size_t CobsCodec::encode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstMax) {
    if (dst == nullptr || (src == nullptr && srcLen > 0)) {
        return 0;
    }

    size_t worstCase = srcLen + (srcLen / 254) + 2;
    if (dstMax < worstCase) {
        return 0;
    }

    size_t readIndex = 0;
    size_t writeIndex = 1;
    size_t codeIndex = 0;
    uint8_t code = 1;

    while (readIndex < srcLen) {
        if (src[readIndex] == 0) {
            dst[codeIndex] = code;
            code = 1;
            codeIndex = writeIndex++;
            readIndex++;
        } else {
            dst[writeIndex++] = src[readIndex++];
            code++;
            if (code == 0xFF) {
                dst[codeIndex] = code;
                code = 1;
                codeIndex = writeIndex++;
            }
        }
    }

    dst[codeIndex] = code;
    return writeIndex;
}

size_t CobsCodec::decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstMax) {
    if (src == nullptr || dst == nullptr || srcLen == 0) {
        return 0;
    }

    size_t readIndex = 0;
    size_t writeIndex = 0;

    while (readIndex < srcLen) {
        uint8_t code = src[readIndex++];
        if (code == 0) {
            // 0 is illegal inside a COBS packet
            return 0;
        }

        for (uint8_t i = 1; i < code; i++) {
            if (readIndex >= srcLen || writeIndex >= dstMax) {
                return 0;
            }
            dst[writeIndex++] = src[readIndex++];
        }

        if (code < 0xFF && readIndex < srcLen) {
            if (writeIndex >= dstMax) {
                return 0;
            }
            dst[writeIndex++] = 0;
        }
    }

    return writeIndex;
}

CobsStreamDecoder::Result CobsStreamDecoder::feedByte(uint8_t wireByte, uint8_t& outByte) {
    if (wireByte == 0x00) {
        if (_inFrame) {
            reset();
            return Result::FRAME_END;
        } else {
            reset();
            _inFrame = true;
            return Result::FRAME_START;
        }
    }

    if (!_inFrame) {
        return Result::NEED_MORE;
    }

    if (_code == 0) {
        uint8_t codeVal = wireByte;
        if (codeVal == 0) {
            reset();
            return Result::ERROR;
        }

        bool emitZero = (!_firstCode && _addZeroPending);

        _code = codeVal - 1;
        _addZeroPending = (codeVal < 0xFF);
        _firstCode = false;

        if (emitZero) {
            outByte = 0x00;
            return Result::DECODED_BYTE;
        }

        return Result::NEED_MORE;
    }

    _code--;
    outByte = wireByte;
    return Result::DECODED_BYTE;
}

} // namespace uartcobs
} // namespace muon
