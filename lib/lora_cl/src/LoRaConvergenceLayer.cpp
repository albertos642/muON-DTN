/**
 * @file LoRaConvergenceLayer.cpp
 * @brief LoRa CL transmission state machine, reception reassembly, and rollback logic.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/lora/LoRaConvergenceLayer.h"
#include <string.h>

namespace muon {
namespace lora {

LoRaConvergenceLayer::LoRaConvergenceLayer(uint8_t linkId,
                                           ILoRaModem* modem,
                                           ggg::hal::IStorage* storage,
                                           const LoRaConfig& config,
                                           bool dutyCycleEnabled,
                                           uint32_t reassemblyTimeoutMs,
                                           uint32_t ackTimeoutMs)
    : _linkId(linkId),
      _modem(modem),
      _storage(storage),
      _config(config),
      _tokenBucket(LoRaDutyCycleTokenBucket::MAX_CAPACITY_MS),
      _dutyCycleEnabled(dutyCycleEnabled),
      _reassemblyTimeoutMs(reassemblyTimeoutMs),
      _ackTimeoutMs(ackTimeoutMs),
      _timeProvider(nullptr),
      _internalTickMs(0),
      _sessionSeq(0),
      _txState(TxState::IDLE),
      _txBundleHandle(GGG_INVALID_HANDLE),
      _txTotalBytes(0),
      _txTotalSegments(0),
      _txCurrentSegment(0),
      _txQos(0),
      _txSessionId(0),
      _txAckStartTimeMs(0),
      _rxState(RxState::IDLE),
      _rxBundleHandle(GGG_INVALID_HANDLE),
      _rxSessionId(0),
      _rxServiceClass(0),
      _rxTotalSegments(0),
      _rxSegmentsReceivedCount(0),
      _rxStartTimeMs(0),
      _sendingControlFrame(false) {
    memset(_rxSegmentBitmask, 0, sizeof(_rxSegmentBitmask));
    memset(_txBuffer, 0, sizeof(_txBuffer));
    memset(_rxBuffer, 0, sizeof(_rxBuffer));
}

bool LoRaConvergenceLayer::begin() {
    if (_modem == nullptr) {
        return false;
    }
    if (_modem->begin(_config, this)) {
        _modem->startReceive();
        return true;
    }
    return false;
}

uint8_t LoRaConvergenceLayer::getLinkId() const {
    return _linkId;
}

uint32_t LoRaConvergenceLayer::getNowMs() {
    if (_timeProvider != nullptr) {
        return _timeProvider();
    }
    return _internalTickMs;
}

bool LoRaConvergenceLayer::transmitBundle(ggg::hal::StorageHandle_t bundleHandle, uint8_t qos) {
    if (_txState != TxState::IDLE || _modem == nullptr || _storage == nullptr) {
        return false;
    }

    size_t totalBytes = _storage->getSize(bundleHandle);
    if (totalBytes == 0) {
        return false;
    }

    size_t mtu = _modem->getMTU();
    if (mtu <= 3) {
        return false;
    }
    size_t maxPayload = mtu - 3;

    size_t totalSegs = (totalBytes + maxPayload - 1) / maxPayload;
    if (totalSegs > 256 || totalSegs == 0) {
        return false;
    }

    if (_dutyCycleEnabled) {
        uint32_t segToA = _modem->getTimeOnAirMs(mtu);
        uint32_t totalToA = (uint32_t)(totalSegs * segToA);
        if (!_tokenBucket.canTransmit(totalToA)) {
            return false;
        }
    }

    _txBundleHandle = bundleHandle;
    _txTotalBytes = totalBytes;
    _txTotalSegments = (uint16_t)totalSegs;
    _txCurrentSegment = 0;
    _txQos = qos;
    _txSessionId = (_sessionSeq++) & LORA_SESSION_MASK;
    _txState = TxState::SENDING_SEGMENT;

    sendNextSegment();
    return true;
}

void LoRaConvergenceLayer::sendNextSegment() {
    size_t maxPayload = _modem->getMTU() - 3;
    size_t offset = _txCurrentSegment * maxPayload;
    size_t chunkLen = _txTotalBytes - offset;
    if (chunkLen > maxPayload) {
        chunkLen = maxPayload;
    }

    LoRaCL_Header hdr;
    hdr.control_session = makeControlWord(
        LORA_TYPE_SEGMENT,
        (_txQos == 1) ? LORA_SC_NOTIFIED : LORA_SC_UNRELIABLE,
        _txSessionId
    );
    hdr.data.total_segments = (_txTotalSegments == 256) ? 0 : (uint8_t)_txTotalSegments;
    hdr.data.segment_index = (uint8_t)_txCurrentSegment;

    memcpy(_txBuffer, &hdr, 3);
    _storage->readData(_txBundleHandle, offset, _txBuffer + 3, chunkLen);

    size_t packetLen = 3 + chunkLen;
    if (_dutyCycleEnabled) {
        uint32_t toa = _modem->getTimeOnAirMs(packetLen);
        _tokenBucket.consume(toa);
    }

    bool started = _modem->transmitAsync(_txBuffer, packetLen);
    if (!started) {
        _txState = TxState::IDLE;
        _modem->startReceive();

        ggg::system::SystemEvent ev = {};
        ev.type = muon::events::MUON_EVT_TX_FAILURE;
        ev.source = _linkId;
        ev.priority = 100;
        ev.payload.u32[0] = _txBundleHandle;
        ggg::system::SystemBus::getInstance().publish(ev);
    }
}

void LoRaConvergenceLayer::sendAck(uint8_t sessionId) {
    LoRaCL_Header hdr;
    hdr.control_session = makeControlWord(LORA_TYPE_ACK, 0, sessionId);
    _sendingControlFrame = true;
    _modem->transmitAsync((const uint8_t*)&hdr, 1);
}

void LoRaConvergenceLayer::sendRefuse(uint8_t sessionId, uint8_t reasonCode, uint8_t sc) {
    LoRaCL_Header hdr;
    hdr.control_session = makeControlWord(LORA_TYPE_REFUSE, sc, sessionId);
    hdr.reason_code = reasonCode;
    _sendingControlFrame = true;
    _modem->transmitAsync((const uint8_t*)&hdr, 2);
}

void LoRaConvergenceLayer::sendReject(uint8_t sessionId, uint8_t reasonCode) {
    LoRaCL_Header hdr;
    hdr.control_session = makeControlWord(LORA_TYPE_MSG_REJECT, 0, sessionId);
    hdr.reason_code = reasonCode;
    _sendingControlFrame = true;
    _modem->transmitAsync((const uint8_t*)&hdr, 2);
}

void LoRaConvergenceLayer::onTxDone() {
    if (_sendingControlFrame) {
        _sendingControlFrame = false;
        _modem->startReceive();
        return;
    }

    if (_txState == TxState::SENDING_SEGMENT) {
        _txCurrentSegment++;
        if (_txCurrentSegment < _txTotalSegments) {
            sendNextSegment();
        } else {
            // All segments sent
            if (_txQos == 0) {
                // Unreliable: complete immediately
                _txState = TxState::IDLE;
                _modem->startReceive();

                ggg::system::SystemEvent ev = {};
                ev.type = muon::events::MUON_EVT_TX_SUCCESS;
                ev.source = _linkId;
                ev.priority = 100;
                ev.payload.u32[0] = _txBundleHandle;
                ggg::system::SystemBus::getInstance().publish(ev);
            } else {
                // Notified: wait for BDL_XFER_ACK
                _txState = TxState::WAIT_ACK;
                _txAckStartTimeMs = getNowMs();
                _modem->startReceive();
            }
        }
    }
}

void LoRaConvergenceLayer::onRxDone(size_t length) {
    if (length == 0 || _modem == nullptr) {
        return;
    }

    size_t rLen = _modem->receive(_rxBuffer, length > sizeof(_rxBuffer) ? sizeof(_rxBuffer) : length);
    if (rLen < 1) {
        return;
    }

    uint8_t control = _rxBuffer[0];
    uint8_t type = getMessageType(control);
    uint8_t sc = getServiceClass(control);
    uint8_t session = getSessionId(control);

    if (type == LORA_TYPE_ACK) {
        if (_txState == TxState::WAIT_ACK && session == _txSessionId) {
            _txState = TxState::IDLE;
            ggg::system::SystemEvent ev = {};
            ev.type = muon::events::MUON_EVT_TX_SUCCESS;
            ev.source = _linkId;
            ev.priority = 100;
            ev.payload.u32[0] = _txBundleHandle;
            ggg::system::SystemBus::getInstance().publish(ev);
        }
        _modem->startReceive();
        return;
    }

    if (type == LORA_TYPE_REFUSE || type == LORA_TYPE_MSG_REJECT) {
        if (_txState != TxState::IDLE && session == _txSessionId) {
            _txState = TxState::IDLE;
            ggg::system::SystemEvent ev = {};
            ev.type = muon::events::MUON_EVT_TX_FAILURE;
            ev.source = _linkId;
            ev.priority = 100;
            ev.payload.u32[0] = _txBundleHandle;
            ggg::system::SystemBus::getInstance().publish(ev);
        }
        _modem->startReceive();
        return;
    }

    if (type == LORA_TYPE_SEGMENT) {
        if (rLen < 3) {
            _modem->startReceive();
            return;
        }

        uint8_t totalSegsRaw = _rxBuffer[1];
        uint8_t segIdx = _rxBuffer[2];
        uint16_t totalSegs = (totalSegsRaw == 0) ? 256 : totalSegsRaw;
        const uint8_t* payload = _rxBuffer + 3;
        size_t payloadLen = rLen - 3;

        if (_rxState == RxState::IDLE) {
            if (_storage == nullptr) {
                sendRefuse(session, LORA_REFUSE_ADMIN_DISCARD, sc);
                return;
            }

            ggg::hal::StorageHandle_t h = _storage->beginWrite();
            if (h == GGG_INVALID_HANDLE) {
                sendRefuse(session, LORA_REFUSE_INSUFFICIENT_SPACE, sc);
                return;
            }

            _rxBundleHandle = h;
            _rxSessionId = session;
            _rxServiceClass = sc;
            _rxTotalSegments = totalSegs;
            _rxSegmentsReceivedCount = 0;
            memset(_rxSegmentBitmask, 0, sizeof(_rxSegmentBitmask));
            _rxStartTimeMs = getNowMs();
            _rxState = RxState::RECEIVING;
        }

        if (_rxState == RxState::RECEIVING) {
            if (session != _rxSessionId) {
                sendReject(session, LORA_REJECT_UNKNOWN_TRANSFER);
                return;
            }

            size_t wordIdx = segIdx / 32;
            uint32_t bit = 1UL << (segIdx % 32);
            if ((_rxSegmentBitmask[wordIdx] & bit) == 0) {
                _rxSegmentBitmask[wordIdx] |= bit;
                _storage->writeData(_rxBundleHandle, payload, payloadLen);
                _rxSegmentsReceivedCount++;
                _rxStartTimeMs = getNowMs();
            }

            if (_rxSegmentsReceivedCount == _rxTotalSegments) {
                _storage->commitWrite(_rxBundleHandle);
                ggg::hal::StorageHandle_t committedHandle = _rxBundleHandle;

                _rxState = RxState::IDLE;
                _rxBundleHandle = GGG_INVALID_HANDLE;

                if (_rxServiceClass == LORA_SC_NOTIFIED) {
                    sendAck(session);
                }

                ggg::system::SystemEvent ev = {};
                ev.type = muon::events::MUON_EVT_RX_READY;
                ev.source = _linkId;
                ev.priority = 100;
                ev.payload.u32[0] = committedHandle;
                ggg::system::SystemBus::getInstance().publish(ev);
            }
        }
    }

    if (!_sendingControlFrame && _txState != TxState::SENDING_SEGMENT) {
        _modem->startReceive();
    }
}

void LoRaConvergenceLayer::tick() {
    uint32_t now = getNowMs();
    _internalTickMs += 10;

    _tokenBucket.update(now);

    if (_txState == TxState::WAIT_ACK) {
        if (now - _txAckStartTimeMs >= _ackTimeoutMs) {
            _txState = TxState::IDLE;

            ggg::system::SystemEvent ev = {};
            ev.type = muon::events::MUON_EVT_TX_FAILURE;
            ev.source = _linkId;
            ev.priority = 100;
            ev.payload.u32[0] = _txBundleHandle;
            ggg::system::SystemBus::getInstance().publish(ev);
        }
    }

    if (_rxState == RxState::RECEIVING) {
        if (now - _rxStartTimeMs >= _reassemblyTimeoutMs) {
            if (_storage != nullptr && _rxBundleHandle != GGG_INVALID_HANDLE) {
                _storage->abortWrite(_rxBundleHandle);
            }
            _rxBundleHandle = GGG_INVALID_HANDLE;
            _rxState = RxState::IDLE;

            if (_rxServiceClass == LORA_SC_NOTIFIED) {
                sendRefuse(_rxSessionId, LORA_REFUSE_NACK_NOTIFIED, _rxServiceClass);
            }
        }
    }
}

} // namespace lora
} // namespace muon
