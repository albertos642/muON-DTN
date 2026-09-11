/**
 * @file UartCobsConvergenceLayer.cpp
 * @brief Serial transmission and stream-to-storage reception state machine.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/uartcobs/UartCobsConvergenceLayer.h"

namespace muon {
namespace uartcobs {

UartCobsConvergenceLayer::UartCobsConvergenceLayer(uint8_t linkId,
                                                   ggg::hal::IInputStream* inStream,
                                                   ggg::hal::IOutputStream* outStream,
                                                   ggg::hal::IStorage* storage,
                                                   bool timeSyncEnabled)
    : _linkId(linkId),
      _inStream(inStream),
      _outStream(outStream),
      _storage(storage),
      _timeSyncCallback(nullptr),
      _timeSyncEnabled(timeSyncEnabled),
      _currentRxHandle(GGG_INVALID_HANDLE),
      _rxByteIndex(0),
      _runningRxCrc(Crc16Ccitt::INITIAL_VALUE),
      _isHeaderParsed(false),
      _isSyncFrame(false),
      _crcWindowCount(0),
      _storageWritePos(0),
      _syncBufferLen(0),
      _txBlockLen(0) {
    memset(_crcWindow, 0, sizeof(_crcWindow));
    memset(_storageWriteBuf, 0, sizeof(_storageWriteBuf));
    memset(_syncBuffer, 0, sizeof(_syncBuffer));
    memset(_txBlock, 0, sizeof(_txBlock));
}

uint8_t UartCobsConvergenceLayer::getLinkId() const {
    return _linkId;
}

void UartCobsConvergenceLayer::resetRxState() {
    if (_currentRxHandle != GGG_INVALID_HANDLE && _storage != nullptr) {
        _storage->abortWrite(_currentRxHandle);
        _currentRxHandle = GGG_INVALID_HANDLE;
    }
    _rxByteIndex = 0;
    _runningRxCrc = Crc16Ccitt::INITIAL_VALUE;
    _isHeaderParsed = false;
    _isSyncFrame = false;
    _crcWindowCount = 0;
    _storageWritePos = 0;
    _syncBufferLen = 0;
}

void UartCobsConvergenceLayer::flushRxStorageBuffer() {
    if (_storageWritePos > 0 && _currentRxHandle != GGG_INVALID_HANDLE && _storage != nullptr) {
        _storage->writeData(_currentRxHandle, _storageWriteBuf, _storageWritePos);
        _storageWritePos = 0;
    }
}

void UartCobsConvergenceLayer::feedWireByte(uint8_t wireByte) {
    uint8_t decodedByte = 0;
    CobsStreamDecoder::Result res = _cobsDecoder.feedByte(wireByte, decodedByte);

    if (res == CobsStreamDecoder::Result::FRAME_START) {
        resetRxState();
        return;
    }

    if (res == CobsStreamDecoder::Result::FRAME_END) {
        if (!_isHeaderParsed) {
            resetRxState();
            return;
        }

        if (_isSyncFrame) {
            // Expected sync frame: 4 bytes DTN epoch + 2 bytes CRC
            if (_syncBufferLen == 6) {
                uint16_t computedCrc = Crc16Ccitt::update(_runningRxCrc, _syncBuffer, 4);
                uint16_t receivedCrc = ((uint16_t)_syncBuffer[4] << 8) | _syncBuffer[5];
                if (computedCrc == receivedCrc) {
                    uint32_t dtnTime = ((uint32_t)_syncBuffer[0] << 24) |
                                       ((uint32_t)_syncBuffer[1] << 16) |
                                       ((uint32_t)_syncBuffer[2] << 8) |
                                       (uint32_t)_syncBuffer[3];
                    if (_timeSyncCallback != nullptr) {
                        _timeSyncCallback(dtnTime);
                    }

                    // Emit MUON_EVT_TIME_SYNC on SystemBus for RTC and time providers
                    ggg::system::SystemEvent ev = {};
                    ev.type = muon::events::MUON_EVT_TIME_SYNC;
                    ev.source = _linkId;
                    ev.priority = 200;
                    ev.payload.u32[0] = dtnTime;
                    ggg::system::SystemBus::getInstance().publish(ev);
                }
            }
            resetRxState();
            return;
        }

        // DATA Frame: verify CRC and commit/rollback
        if (_crcWindowCount < 2 || _storage == nullptr) {
            resetRxState();
            return;
        }

        flushRxStorageBuffer();

        uint16_t receivedCrc = ((uint16_t)_crcWindow[0] << 8) | _crcWindow[1];
        if (receivedCrc == _runningRxCrc) {
            // Integrity verified: commit to persistent storage
            _storage->commitWrite(_currentRxHandle);
            ggg::hal::StorageHandle_t committedHandle = _currentRxHandle;
            _currentRxHandle = GGG_INVALID_HANDLE;
            resetRxState();

            // Emit MUON_EVT_RX_READY to BPA
            ggg::system::SystemEvent ev = {};
            ev.type = muon::events::MUON_EVT_RX_READY;
            ev.source = _linkId;
            ev.priority = 100;
            ev.payload.u32[0] = committedHandle;
            ggg::system::SystemBus::getInstance().publish(ev);
        } else {
            // CRC failure: ROLLBACK storage
            resetRxState();
        }
        return;
    }

    if (res == CobsStreamDecoder::Result::DECODED_BYTE) {
        _rxByteIndex++;
        if (_rxByteIndex == 1) {
            // Control Header
            if (!isValidVersion(decodedByte)) {
                resetRxState();
                return;
            }
            _isHeaderParsed = true;
            _isSyncFrame = isSyncFrame(decodedByte);
            _runningRxCrc = Crc16Ccitt::update(Crc16Ccitt::INITIAL_VALUE, decodedByte);

            if (!_isSyncFrame && _storage != nullptr) {
                _currentRxHandle = _storage->beginWrite();
                if (_currentRxHandle == GGG_INVALID_HANDLE) {
                    resetRxState();
                    return;
                }
            }
        } else {
            // Subsequent bytes
            if (_isSyncFrame) {
                if (_syncBufferLen < sizeof(_syncBuffer)) {
                    _syncBuffer[_syncBufferLen++] = decodedByte;
                }
            } else {
                if (_crcWindowCount < 2) {
                    _crcWindow[_crcWindowCount++] = decodedByte;
                } else {
                    uint8_t payloadByte = _crcWindow[0];
                    _crcWindow[0] = _crcWindow[1];
                    _crcWindow[1] = decodedByte;

                    _runningRxCrc = Crc16Ccitt::update(_runningRxCrc, payloadByte);
                    _storageWriteBuf[_storageWritePos++] = payloadByte;
                    if (_storageWritePos >= sizeof(_storageWriteBuf)) {
                        flushRxStorageBuffer();
                    }
                }
            }
        }
        return;
    }

    if (res == CobsStreamDecoder::Result::ERROR) {
        resetRxState();
        return;
    }
}

void UartCobsConvergenceLayer::tick() {
    if (_inStream == nullptr) {
        return;
    }

    while (_inStream->available() > 0) {
        int b = _inStream->read();
        if (b >= 0) {
            feedWireByte((uint8_t)b);
        }
    }
}

void UartCobsConvergenceLayer::txBlockAppend(uint8_t byte) {
    if (byte == 0x00) {
        txBlockFlush();
    } else {
        _txBlock[_txBlockLen++] = byte;
        if (_txBlockLen == 254) {
            txBlockFlush();
        }
    }
}

void UartCobsConvergenceLayer::txBlockFlush() {
    if (_outStream == nullptr) {
        return;
    }
    _outStream->write((uint8_t)(_txBlockLen + 1));
    if (_txBlockLen > 0) {
        _outStream->write(_txBlock, _txBlockLen);
        _txBlockLen = 0;
    }
}

bool UartCobsConvergenceLayer::transmitBundle(ggg::hal::StorageHandle_t bundleHandle, uint8_t qos) {
    (void)qos; // Best effort on serial as per paper
    if (_outStream == nullptr || _storage == nullptr) {
        return false;
    }

    size_t totalBytes = _storage->getSize(bundleHandle);
    if (totalBytes == 0) {
        return false;
    }

    // Step 1: Pre-calculate CRC-16 across Control Header and Bundle Payload
    uint8_t control = makeControlHeader(UARTCL_FLAG_DATA);
    uint16_t crc = Crc16Ccitt::update(Crc16Ccitt::INITIAL_VALUE, control);

    uint8_t chunk[64];
    for (size_t offset = 0; offset < totalBytes; offset += sizeof(chunk)) {
        size_t len = _storage->readData(bundleHandle, offset, chunk, sizeof(chunk));
        crc = Crc16Ccitt::update(crc, chunk, len);
    }

    uint8_t crcBytes[2] = {
        (uint8_t)((crc >> 8) & 0xFF),
        (uint8_t)(crc & 0xFF)
    };

    // Step 2: Stream-encode frame via COBS directly to output stream
    _outStream->write(UARTCL_DELIMITER);
    _txBlockLen = 0;

    txBlockAppend(control);

    for (size_t offset = 0; offset < totalBytes; offset += sizeof(chunk)) {
        size_t len = _storage->readData(bundleHandle, offset, chunk, sizeof(chunk));
        for (size_t i = 0; i < len; i++) {
            txBlockAppend(chunk[i]);
        }
    }

    txBlockAppend(crcBytes[0]);
    txBlockAppend(crcBytes[1]);

    txBlockFlush();
    _outStream->write(UARTCL_DELIMITER);
    _outStream->flush();

    // Signal TX Success on SystemBus
    ggg::system::SystemEvent ev = {};
    ev.type = muon::events::MUON_EVT_TX_SUCCESS;
    ev.source = _linkId;
    ev.priority = 100;
    ev.payload.u32[0] = bundleHandle;
    ggg::system::SystemBus::getInstance().publish(ev);

    return true;
}

void UartCobsConvergenceLayer::sendSyncRequest() {
    if (_outStream == nullptr) {
        return;
    }

    uint8_t control = makeControlHeader(UARTCL_FLAG_SYNC);
    uint16_t crc = Crc16Ccitt::update(Crc16Ccitt::INITIAL_VALUE, control);
    uint8_t raw[3] = {
        control,
        (uint8_t)((crc >> 8) & 0xFF),
        (uint8_t)(crc & 0xFF)
    };

    uint8_t encoded[8];
    size_t encLen = CobsCodec::encode(raw, sizeof(raw), encoded, sizeof(encoded));
    if (encLen == 0) {
        return;
    }

    _outStream->write(UARTCL_DELIMITER);
    _outStream->write(encoded, encLen);
    _outStream->write(UARTCL_DELIMITER);
    _outStream->flush();
}

} // namespace uartcobs
} // namespace muon
