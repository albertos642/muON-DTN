/**
 * @file UartCobsConvergenceLayer.h
 * @brief UART Convergence Layer with atomic stream-to-storage reception and rollback.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_UART_COBS_CONVERGENCE_LAYER_H
#define MUON_UART_COBS_CONVERGENCE_LAYER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <muon/clm/IConvergenceLayer.h>
#include <muon/bpa/MuonEvents.h>
#include <ggg/system/SystemBus.h>
#include <ggg/hal/IStorage.h>
#include <ggg/hal/IStream.h>

#include "UartCobsFraming.h"
#include "Crc16Ccitt.h"
#include "CobsCodec.h"

namespace muon {
namespace uartcobs {

typedef void (*DtnTimeSyncCallback)(uint32_t dtnEpochSeconds);

class UartCobsConvergenceLayer : public clm::IConvergenceLayer {
private:
    uint8_t _linkId;
    ggg::hal::IInputStream* _inStream;
    ggg::hal::IOutputStream* _outStream;
    ggg::hal::IStorage* _storage;

    DtnTimeSyncCallback _timeSyncCallback;
    bool _timeSyncEnabled;

    // Stream-to-Storage RX State
    CobsStreamDecoder _cobsDecoder;
    ggg::hal::StorageHandle_t _currentRxHandle;
    size_t _rxByteIndex;
    uint16_t _runningRxCrc;
    bool _isHeaderParsed;
    bool _isSyncFrame;

    // 2-byte sliding window to buffer trailing CRC16 bytes and avoid storing them
    uint8_t _crcWindow[2];
    uint8_t _crcWindowCount;

    // Small storage write buffer (O(1) RAM)
    uint8_t _storageWriteBuf[32];
    size_t _storageWritePos;

    // Sync frame payload buffer (Header + 4 bytes time + 2 bytes CRC)
    uint8_t _syncBuffer[8];
    size_t _syncBufferLen;

    // TX state buffer
    uint8_t _txBlock[254];
    size_t _txBlockLen;

    void flushRxStorageBuffer();
    void resetRxState();
    void feedWireByte(uint8_t wireByte);

    void txBlockAppend(uint8_t byte);
    void txBlockFlush();

public:
    UartCobsConvergenceLayer(uint8_t linkId,
                             ggg::hal::IInputStream* inStream,
                             ggg::hal::IOutputStream* outStream,
                             ggg::hal::IStorage* storage,
                             bool timeSyncEnabled = true);

    void setTimeSyncCallback(DtnTimeSyncCallback cb) {
        _timeSyncCallback = cb;
    }

    void sendSyncRequest();

    // IConvergenceLayer interface
    uint8_t getLinkId() const override;
    bool transmitBundle(ggg::hal::StorageHandle_t bundleHandle, uint8_t qos) override;
    void tick() override;
};

} // namespace uartcobs
} // namespace muon

#endif // MUON_UART_COBS_CONVERGENCE_LAYER_H
