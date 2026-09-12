/**
 * @file LoRaConvergenceLayer.h
 * @brief LoRa Convergence Layer implementing segmentation, Block-ACK, and reassembly.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_LORA_CONVERGENCE_LAYER_H
#define MUON_LORA_CONVERGENCE_LAYER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <muon/clm/IConvergenceLayer.h>
#include <muon/bpa/MuonEvents.h>
#include <ggg/system/SystemBus.h>
#include <ggg/hal/IStorage.h>

#include "LoRaFraming.h"
#include "ILoRaModem.h"
#include "LoRaDutyCycleTokenBucket.h"

namespace muon {
namespace lora {

typedef uint32_t (*LoRaTimeProviderFn)();

class LoRaConvergenceLayer : public clm::IConvergenceLayer, public IModemCallback {
public:
    enum class TxState {
        IDLE,
        SENDING_SEGMENT,
        WAIT_ACK
    };

    enum class RxState {
        IDLE,
        RECEIVING
    };

private:
    uint8_t _linkId;
    ILoRaModem* _modem;
    ggg::hal::IStorage* _storage;
    LoRaConfig _config;
    LoRaDutyCycleTokenBucket _tokenBucket;
    bool _dutyCycleEnabled;

    uint32_t _reassemblyTimeoutMs;
    uint32_t _ackTimeoutMs;

    LoRaTimeProviderFn _timeProvider;
    uint32_t _internalTickMs;

    // Buffer space (Modem MTU up to 255)
    uint8_t _txBuffer[255];
    uint8_t _rxBuffer[255];

    // Session generator
    uint8_t _sessionSeq;

    // TX state machine
    TxState _txState;
    ggg::hal::StorageHandle_t _txBundleHandle;
    size_t _txTotalBytes;
    uint16_t _txTotalSegments;
    uint16_t _txCurrentSegment;
    uint8_t _txQos;
    uint8_t _txSessionId;
    uint32_t _txAckStartTimeMs;
    uint32_t _txSegmentStartTimeMs;

    // RX state machine
    RxState _rxState;
    ggg::hal::StorageHandle_t _rxBundleHandle;
    uint8_t _rxSessionId;
    uint8_t _rxServiceClass;
    uint16_t _rxTotalSegments;
    uint16_t _rxSegmentsReceivedCount;
    uint32_t _rxStartTimeMs;
    uint32_t _rxSegmentBitmask[8]; // 256 bits for segment tracking

    // Pending control frame flag (e.g. ACK/REFUSE in transmission)
    bool     _sendingControlFrame;
    uint32_t _controlFrameStartTimeMs;
    uint8_t  _ctrlBuffer[4];

    uint32_t getNowMs();
    void sendNextSegment();
    void sendAck(uint8_t sessionId);
    void sendRefuse(uint8_t sessionId, uint8_t reasonCode, uint8_t sc);
    void sendReject(uint8_t sessionId, uint8_t reasonCode);

public:
    LoRaConvergenceLayer(uint8_t linkId,
                         ILoRaModem* modem,
                         ggg::hal::IStorage* storage,
                         const LoRaConfig& config,
                         bool dutyCycleEnabled = true,
                         uint32_t reassemblyTimeoutMs = 5000,
                         uint32_t ackTimeoutMs = 3000);

    bool begin();

    void setTimeProvider(LoRaTimeProviderFn fn) {
        _timeProvider = fn;
    }

    void setDutyCycleEnabled(bool enabled) {
        _dutyCycleEnabled = enabled;
    }

    LoRaDutyCycleTokenBucket& getTokenBucket() {
        return _tokenBucket;
    }

    TxState getTxState() const { return _txState; }
    RxState getRxState() const { return _rxState; }

    // IConvergenceLayer interface
    uint8_t getLinkId() const override;
    bool transmitBundle(ggg::hal::StorageHandle_t bundleHandle, uint8_t qos) override;
    void tick() override;

    // IModemCallback interface
    void onTxDone() override;
    void onRxDone(size_t length) override;
};

} // namespace lora
} // namespace muon

#endif // MUON_LORA_CONVERGENCE_LAYER_H
