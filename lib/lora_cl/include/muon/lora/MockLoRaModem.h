/**
 * @file MockLoRaModem.h
 * @brief Deterministic mock LoRa modem for native desktop unit and integration testing.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_MOCK_LORA_MODEM_H
#define MUON_MOCK_LORA_MODEM_H

#include "muon/lora/ILoRaModem.h"
#include "muon/lora/LoRaDutyCycleTokenBucket.h"
#include <string.h>

namespace muon {
namespace lora {

class MockLoRaModem : public ILoRaModem {
private:
    struct PacketSlot {
        uint8_t data[256];
        size_t len;
    };

    IModemCallback* _callback;
    LoRaConfig _config;
    bool _isReceiving;
    bool _autoTxDone;
    MockLoRaModem* _peer;

    uint8_t _lastTxPacket[256];
    size_t _lastTxLen;
    size_t _txCount;

    uint8_t _rxPacket[256];
    size_t _rxLen;

    PacketSlot _queue[8];
    size_t _qHead;
    size_t _qTail;
    size_t _qCount;
    bool _dispatching;

    uint32_t _simulatedToAMs;

    void dispatchQueue() {
        if (_dispatching || !_isReceiving || _callback == nullptr) {
            return;
        }
        _dispatching = true;
        while (_qCount > 0 && _isReceiving) {
            PacketSlot slot = _queue[_qHead];
            _qHead = (_qHead + 1) % 8;
            _qCount--;

            memcpy(_rxPacket, slot.data, slot.len);
            _rxLen = slot.len;
            _callback->onRxDone(slot.len);
        }
        _dispatching = false;
    }

public:
    MockLoRaModem()
        : _callback(nullptr),
          _config{},
          _isReceiving(false),
          _autoTxDone(true),
          _peer(nullptr),
          _lastTxLen(0),
          _txCount(0),
          _rxLen(0),
          _qHead(0),
          _qTail(0),
          _qCount(0),
          _dispatching(false),
          _simulatedToAMs(50) {
        memset(_lastTxPacket, 0, sizeof(_lastTxPacket));
        memset(_rxPacket, 0, sizeof(_rxPacket));
    }

    void linkPeer(MockLoRaModem* peer) {
        _peer = peer;
    }

    void setAutoTxDone(bool autoDone) {
        _autoTxDone = autoDone;
    }

    void setSimulatedToAMs(uint32_t toa) {
        _simulatedToAMs = toa;
    }

    size_t getTxCount() const { return _txCount; }
    const uint8_t* getLastTxPacket() const { return _lastTxPacket; }
    size_t getLastTxLen() const { return _lastTxLen; }

    bool begin(const LoRaConfig& config, IModemCallback* callback) override {
        _config = config;
        _callback = callback;
        return true;
    }

    void enqueuePacket(const uint8_t* buffer, size_t length) {
        if (length > 256) length = 256;

        if (_qCount < 8) {
            memcpy(_queue[_qTail].data, buffer, length);
            _queue[_qTail].len = length;
            _qTail = (_qTail + 1) % 8;
            _qCount++;
        }
    }

    bool transmitAsync(const uint8_t* buffer, size_t length) override {
        if (length > getMTU()) return false;
        memcpy(_lastTxPacket, buffer, length);
        _lastTxLen = length;
        _txCount++;

        // Queue packet in peer
        if (_peer != nullptr && _peer->_isReceiving) {
            _peer->enqueuePacket(buffer, length);
        }

        if (_autoTxDone && _callback != nullptr) {
            _callback->onTxDone();
        }

        if (_peer != nullptr && _peer->_isReceiving) {
            _peer->dispatchQueue();
        }

        return true;
    }

    void triggerTxDone() {
        if (_callback != nullptr) {
            _callback->onTxDone();
        }
    }

    void injectPacket(const uint8_t* buffer, size_t length) {
        enqueuePacket(buffer, length);
        dispatchQueue();
    }

    size_t receive(uint8_t* buffer, size_t maxLength) override {
        size_t toCopy = _rxLen < maxLength ? _rxLen : maxLength;
        memcpy(buffer, _rxPacket, toCopy);
        _rxLen = 0;
        return toCopy;
    }

    void startReceive() override {
        _isReceiving = true;
        dispatchQueue();
    }

    size_t getMTU() const override {
        return 255;
    }

    void handleInterrupt() override {}

    uint32_t getTimeOnAirMs(size_t packetLength) const override {
        if (_simulatedToAMs > 0) {
            return _simulatedToAMs;
        }
        return LoRaDutyCycleTokenBucket::calculateAnalyticalToAMs(_config, packetLength);
    }

    float getRSSI() const override { return -85.0f; }
    float getSNR() const override { return 8.0f; }
};

} // namespace lora
} // namespace muon

#endif // MUON_MOCK_LORA_MODEM_H
