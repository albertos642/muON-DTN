/**
 * @file LoRaConvergenceLayer.cpp
 * @brief LoRa CL transmission state machine, reception reassembly, and rollback logic.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include "muon/lora/LoRaConvergenceLayer.h"
#include "muon/common/Logger.h"
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
      _txSegmentStartTimeMs(0),
      _rxState(RxState::IDLE),
      _rxBundleHandle(GGG_INVALID_HANDLE),
      _rxSessionId(0),
      _rxServiceClass(0),
      _rxTotalSegments(0),
      _rxSegmentsReceivedCount(0),
      _rxStartTimeMs(0),
      _sendingControlFrame(false),
      _controlFrameStartTimeMs(0),
      _pendingRxBundleHandle(GGG_INVALID_HANDLE),
      _lastRssi(-120),
      _lastSnr(0) {
    memset(_rxSegmentBitmask, 0, sizeof(_rxSegmentBitmask));
    memset(_txBuffer, 0, sizeof(_txBuffer));
    memset(_rxBuffer, 0, sizeof(_rxBuffer));
    memset(_ctrlBuffer, 0, sizeof(_ctrlBuffer));
}

bool LoRaConvergenceLayer::begin() {
    if (_modem == nullptr) {
        return false;
    }
    bool ok = _modem->begin(_config, this);
    if (ok) {
        _modem->startReceive();
    }
    return ok;
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
    MUON_LOG_STR("[LoRaCL] transmitBundle: Handle=");
    MUON_LOG_U32(bundleHandle);
    MUON_LOG_STR(", QoS=");
    MUON_LOG_U32(qos);
    MUON_LOG_LN(qos == 1 ? " (Notified)" : " (Unreliable)");

    if (_txState != TxState::IDLE || _modem == nullptr || _storage == nullptr) {
        MUON_LOG_STR("[LoRaCL] WARNING: TX rejected (State=");
        MUON_LOG_I32(static_cast<int32_t>(_txState));
        MUON_LOG_LN(" or null driver)");
        return false;
    }

    size_t totalBytes = _storage->getSize(bundleHandle);
    if (totalBytes == 0) {
        MUON_LOG_LN("[LoRaCL] ERROR: Bundle storage record size is 0 bytes!");
        return false;
    }

    size_t mtu = _modem->getMTU();
    if (mtu <= 3) {
        MUON_LOG_LN("[LoRaCL] ERROR: Modem MTU too small (<= 3)!");
        return false;
    }
    size_t maxPayload = mtu - 3;

    size_t totalSegs = (totalBytes + maxPayload - 1) / maxPayload;
    if (totalSegs > 256 || totalSegs == 0) {
        MUON_LOG_STR("[LoRaCL] ERROR: Total segments out of bounds (");
        MUON_LOG_U32(totalSegs);
        MUON_LOG_LN(" > 256)");
        return false;
    }

    if (_dutyCycleEnabled) {
        uint32_t segToA = _modem->getTimeOnAirMs(mtu);
        uint32_t totalToA = (uint32_t)(totalSegs * segToA);
        MUON_LOG_STR("[LoRaCL] Duty Cycle check: Available=");
        MUON_LOG_U32(_tokenBucket.getAvailableBudgetMs());
        MUON_LOG_STR(" ms, Needed=");
        MUON_LOG_U32(totalToA);
        MUON_LOG_LN(" ms");
        if (!_tokenBucket.canTransmit(totalToA)) {
            MUON_LOG_LN("[LoRaCL] ERROR: Duty Cycle budget exhausted! Transmission rejected.");
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

    MUON_LOG_STR("[LoRaCL] Packet segmented: ");
    MUON_LOG_U32(_txTotalSegments);
    MUON_LOG_STR(" segment(s) for ");
    MUON_LOG_U32(_txTotalBytes);
    MUON_LOG_STR(" bytes. Session ID: ");
    MUON_LOG_U32(_txSessionId);
    MUON_LOG_LN("");

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
    uint32_t toa = 0;
    if (_dutyCycleEnabled) {
        toa = _modem->getTimeOnAirMs(packetLen);
        _tokenBucket.consume(toa);
    }

    _txSegmentStartTimeMs = getNowMs();

    MUON_LOG_STR("[LoRaCL] Sending segment ");
    MUON_LOG_U32(_txCurrentSegment + 1);
    MUON_LOG_STR("/");
    MUON_LOG_U32(_txTotalSegments);
    MUON_LOG_STR(" (Payload: ");
    MUON_LOG_U32(chunkLen);
    MUON_LOG_STR(" B, Wire: ");
    MUON_LOG_U32(packetLen);
    MUON_LOG_STR(" B, ToA: ");
    MUON_LOG_U32(toa);
    MUON_LOG_LN(" ms)...");

    bool started = _modem->transmitAsync(_txBuffer, packetLen);
    if (!started) {
        MUON_LOG_LN("[LoRaCL] ERROR: Modem transmitAsync failed! Aborting transmission.");
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
    _ctrlBuffer[0] = makeControlWord(LORA_TYPE_ACK, 0, sessionId);
    _ctrlBuffer[1] = 0x00; // 2-byte frame for SX1276 explicit header + CRC robustness
    _sendingControlFrame = true;
    _controlFrameStartTimeMs = getNowMs();
    MUON_LOG_STR("[LoRaCL] Sending BDL_XFER_ACK for Session ");
    MUON_LOG_U32(sessionId);
    MUON_LOG_LN("...");
    _modem->transmitAsync(_ctrlBuffer, 2);
}

void LoRaConvergenceLayer::sendRefuse(uint8_t sessionId, uint8_t reasonCode, uint8_t sc) {
    _ctrlBuffer[0] = makeControlWord(LORA_TYPE_REFUSE, sc, sessionId);
    _ctrlBuffer[1] = reasonCode;
    _sendingControlFrame = true;
    _controlFrameStartTimeMs = getNowMs();
    MUON_LOG_STR("[LoRaCL] Sending XFER_REFUSE (Reason: 0x");
    MUON_LOG_U32(reasonCode);
    MUON_LOG_STR(") for Session ");
    MUON_LOG_U32(sessionId);
    MUON_LOG_LN("...");
    _modem->transmitAsync(_ctrlBuffer, 2);
}

void LoRaConvergenceLayer::sendReject(uint8_t sessionId, uint8_t reasonCode) {
    _ctrlBuffer[0] = makeControlWord(LORA_TYPE_MSG_REJECT, 0, sessionId);
    _ctrlBuffer[1] = reasonCode;
    _sendingControlFrame = true;
    _controlFrameStartTimeMs = getNowMs();
    MUON_LOG_STR("[LoRaCL] Sending MSG_REJECT (Reason: 0x");
    MUON_LOG_U32(reasonCode);
    MUON_LOG_STR(") for Session ");
    MUON_LOG_U32(sessionId);
    MUON_LOG_LN("...");
    _modem->transmitAsync(_ctrlBuffer, 2);
}

void LoRaConvergenceLayer::onTxDone() {
    if (_sendingControlFrame) {
        _sendingControlFrame = false;
        MUON_LOG_LN("[LoRaCL] Control frame (ACK/Reject) transmitted over the air. Re-arming receiver...");
        _modem->startReceive();

        if (_pendingRxBundleHandle != GGG_INVALID_HANDLE) {
            ggg::hal::StorageHandle_t handleToPublish = _pendingRxBundleHandle;
            _pendingRxBundleHandle = GGG_INVALID_HANDLE;

            MUON_LOG_STR("[LoRaCL] Bundle completely reassembled (Handle: ");
            MUON_LOG_U32(handleToPublish);
            MUON_LOG_LN("). Publishing MUON_EVT_RX_READY.");

            ggg::system::SystemEvent ev = {};
            ev.type = muon::events::MUON_EVT_RX_READY;
            ev.source = _linkId;
            ev.priority = 100;
            ev.payload.u32[0] = handleToPublish;
            ggg::system::SystemBus::getInstance().publish(ev);
        }
        return;
    }

    if (_txState == TxState::SENDING_SEGMENT) {
        MUON_LOG_STR("[LoRaCL] Segment ");
        MUON_LOG_U32(_txCurrentSegment + 1);
        MUON_LOG_STR("/");
        MUON_LOG_U32(_txTotalSegments);
        MUON_LOG_LN(" transmitted over the air.");

        _txCurrentSegment++;
        if (_txCurrentSegment < _txTotalSegments) {
            sendNextSegment();
        } else {
            // All segments sent
            if (_txQos == 0) {
                // Unreliable: complete immediately
                MUON_LOG_LN("[LoRaCL] All segments sent (Unreliable QoS) -> TX_SUCCESS.");
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
                MUON_LOG_STR("[LoRaCL] All segments sent (Notified QoS) -> Waiting for ACK (Timeout: ");
                MUON_LOG_U32(_ackTimeoutMs);
                MUON_LOG_LN(" ms)...");
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

    _lastRssi = static_cast<int16_t>(_modem->getRSSI());
    _lastSnr = static_cast<int8_t>(_modem->getSNR());

    MUON_LOG_STR("[LoRaCL] RX packet: len=");
    MUON_LOG_U32(rLen);
    MUON_LOG_STR(" B, RSSI=");
    MUON_LOG_I32(_lastRssi);
    MUON_LOG_STR(" dBm, SNR=");
    MUON_LOG_I32(_lastSnr);
    MUON_LOG_LN(" dB");

    uint8_t control = _rxBuffer[0];
    uint8_t type = getMessageType(control);
    uint8_t sc = getServiceClass(control);
    uint8_t session = getSessionId(control);

    if (type == LORA_TYPE_ACK) {
        MUON_LOG_STR("[LoRaCL] Received BDL_XFER_ACK for Session ");
        MUON_LOG_U32(session);
        MUON_LOG_LN("");
        if (_txState == TxState::WAIT_ACK && session == _txSessionId) {
            _txState = TxState::IDLE;
            MUON_LOG_LN("[LoRaCL] ACK matched active session! Publishing TX_SUCCESS.");
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
        uint8_t reason = (rLen >= 2) ? _rxBuffer[1] : 0xFF;
        MUON_LOG_STR("[LoRaCL] Received REFUSE/REJECT (Reason: 0x");
        MUON_LOG_U32(reason);
        MUON_LOG_STR(") for Session ");
        MUON_LOG_U32(session);
        MUON_LOG_LN("");
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

        MUON_LOG_STR("[LoRaCL] Ingress segment ");
        MUON_LOG_U32(segIdx + 1);
        MUON_LOG_STR("/");
        MUON_LOG_U32(totalSegs);
        MUON_LOG_STR(" (Session: ");
        MUON_LOG_U32(session);
        MUON_LOG_STR(", SC: ");
        MUON_LOG_U32(sc);
        MUON_LOG_LN(")");

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

                _lastRssi = static_cast<int16_t>(_modem->getRSSI());
                _lastSnr = static_cast<int8_t>(_modem->getSNR());

                if (_rxServiceClass == LORA_SC_NOTIFIED) {
                    _pendingRxBundleHandle = committedHandle;
                    sendAck(session);
                } else {
                    _pendingRxBundleHandle = GGG_INVALID_HANDLE;
                    MUON_LOG_STR("[LoRaCL] Bundle completely reassembled (Handle: ");
                    MUON_LOG_U32(committedHandle);
                    MUON_LOG_LN("). Publishing MUON_EVT_RX_READY.");

                    ggg::system::SystemEvent ev = {};
                    ev.type = muon::events::MUON_EVT_RX_READY;
                    ev.source = _linkId;
                    ev.priority = 100;
                    ev.payload.u32[0] = committedHandle;
                    ggg::system::SystemBus::getInstance().publish(ev);
                }
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
            MUON_LOG_STR("[LoRaCL] ACK timeout (");
            MUON_LOG_U32(_ackTimeoutMs);
            MUON_LOG_STR(" ms) expired for Session ");
            MUON_LOG_U32(_txSessionId);
            MUON_LOG_LN("! Publishing TX_FAILURE.");

            _txState = TxState::IDLE;

            ggg::system::SystemEvent ev = {};
            ev.type = muon::events::MUON_EVT_TX_FAILURE;
            ev.source = _linkId;
            ev.priority = 100;
            ev.payload.u32[0] = _txBundleHandle;
            ggg::system::SystemBus::getInstance().publish(ev);
        }
    } else if (_txState == TxState::SENDING_SEGMENT) {
        // Watchdog against hardware TX lockup (5000 ms per segment)
        if (_txSegmentStartTimeMs > 0 && (now - _txSegmentStartTimeMs >= 5000)) {
            MUON_LOG_LN("[LoRaCL] WARNING: Segment TX watchdog timeout (5000 ms)! Aborting TX.");
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

    if (_sendingControlFrame) {
        // Watchdog against control frame (ACK/NACK) TX lockup (2000 ms)
        if (_controlFrameStartTimeMs > 0 && (now - _controlFrameStartTimeMs >= 2000)) {
            MUON_LOG_LN("[LoRaCL] WARNING: Control frame TX watchdog timeout (2000 ms)! Forcing receive mode.");
            _sendingControlFrame = false;
            _modem->forceStandby();
            _modem->startReceive();

            if (_pendingRxBundleHandle != GGG_INVALID_HANDLE) {
                ggg::hal::StorageHandle_t handleToPublish = _pendingRxBundleHandle;
                _pendingRxBundleHandle = GGG_INVALID_HANDLE;

                MUON_LOG_STR("[LoRaCL] Bundle completely reassembled (Handle: ");
                MUON_LOG_U32(handleToPublish);
                MUON_LOG_LN("). Publishing MUON_EVT_RX_READY.");

                ggg::system::SystemEvent ev = {};
                ev.type = muon::events::MUON_EVT_RX_READY;
                ev.source = _linkId;
                ev.priority = 100;
                ev.payload.u32[0] = handleToPublish;
                ggg::system::SystemBus::getInstance().publish(ev);
            }
        }
    }

    if (_rxState == RxState::RECEIVING) {
        if (now - _rxStartTimeMs >= _reassemblyTimeoutMs) {
            MUON_LOG_STR("[LoRaCL] RX reassembly timeout (");
            MUON_LOG_U32(_reassemblyTimeoutMs);
            MUON_LOG_LN(" ms)! Rolling back bundle.");
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
