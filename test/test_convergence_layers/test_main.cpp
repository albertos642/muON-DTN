/**
 * @file test_main.cpp
 * @brief Unit tests for LoRaCL, UARTCL-COBS, duty cycle, and two-node paper simulation.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <unity.h>
#include <string.h>

#include <ggg/system/SystemBus.h>
#include <ggg/hal/RamStorage.h>
#include <ggg/hal/IStream.h>

#include <muon/bpa/MuonEvents.h>
#include <muon/bpa/BundleAgent.h>
#include <muon/bpa/BundleMetadataTable.h>
#include <muon/bpa/CBORSerializer.h>
#include <muon/clm/ConvergenceLayerManager.h>
#include <muon/routing/StaticRoutingEngine.h>

#include <muon/lora/LoRaFraming.h>
#include <muon/lora/LoRaDutyCycleTokenBucket.h>
#include <muon/lora/LoRaConvergenceLayer.h>
#include <muon/lora/MockLoRaModem.h>

#include <muon/uartcobs/UartCobsFraming.h>
#include <muon/uartcobs/Crc16Ccitt.h>
#include <muon/uartcobs/CobsCodec.h>
#include <muon/uartcobs/UartCobsConvergenceLayer.h>

using namespace ggg::system;
using namespace ggg::hal;
using namespace muon::bpa;
using namespace muon::clm;
using namespace muon::routing;
using namespace muon::lora;
using namespace muon::uartcobs;

// Simple in-memory IInputStream & IOutputStream for UART testing
class MockStream : public IInputStream, public IOutputStream {
private:
    uint8_t _buffer[1024];
    size_t _head;
    size_t _tail;
    size_t _count;

public:
    MockStream() : _head(0), _tail(0), _count(0) {}

    void clear() {
        _head = 0;
        _tail = 0;
        _count = 0;
    }

    size_t write(uint8_t b) override {
        if (_count >= sizeof(_buffer)) return 0;
        _buffer[_tail] = b;
        _tail = (_tail + 1) % sizeof(_buffer);
        _count++;
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        size_t written = 0;
        for (size_t i = 0; i < size; i++) {
            if (write(buffer[i]) == 0) break;
            written++;
        }
        return written;
    }

    void flush() override {}

    size_t available() override {
        return _count;
    }

    int read() override {
        if (_count == 0) return -1;
        uint8_t b = _buffer[_head];
        _head = (_head + 1) % sizeof(_buffer);
        _count--;
        return b;
    }

    void flushRX() override {
        clear();
    }

    size_t getBytes(uint8_t* dest, size_t maxLen) {
        size_t toCopy = _count < maxLen ? _count : maxLen;
        for (size_t i = 0; i < toCopy; i++) {
            dest[i] = (uint8_t)read();
        }
        return toCopy;
    }
};

// Global Event Tracking
static SystemEvent g_lastEvent = {};
static uint32_t g_eventCount = 0;
static bool g_seenRxReady = false;
static StorageHandle_t g_lastRxHandle = GGG_INVALID_HANDLE;
static bool g_seenTxSuccess = false;
static StorageHandle_t g_lastTxSuccessHandle = GGG_INVALID_HANDLE;
static bool g_seenTxFailure = false;
static StorageHandle_t g_lastTxFailureHandle = GGG_INVALID_HANDLE;

class TestEventListener : public IEventListener {
public:
    void onEvent(const SystemEvent& event) override {
        g_lastEvent = event;
        g_eventCount++;
        if (event.type == muon::events::MUON_EVT_RX_READY) {
            g_seenRxReady = true;
            g_lastRxHandle = (StorageHandle_t)event.payload.u32[0];
        } else if (event.type == muon::events::MUON_EVT_TX_SUCCESS) {
            g_seenTxSuccess = true;
            g_lastTxSuccessHandle = (StorageHandle_t)event.payload.u32[0];
        } else if (event.type == muon::events::MUON_EVT_TX_FAILURE) {
            g_seenTxFailure = true;
            g_lastTxFailureHandle = (StorageHandle_t)event.payload.u32[0];
        }
    }
};

static TestEventListener g_eventListener;

// Simulated clock
static uint32_t g_mockTimeMs = 1000;
static uint32_t getMockTimeMs() {
    return g_mockTimeMs;
}

static void dispatchAllEvents() {
    while (SystemBus::getInstance().dispatchOne()) {}
}

void setUp(void) {
    SystemBus::getInstance().reset();
    SystemBus::getInstance().init();
    g_lastEvent = {};
    g_eventCount = 0;
    g_seenRxReady = false;
    g_lastRxHandle = GGG_INVALID_HANDLE;
    g_seenTxSuccess = false;
    g_lastTxSuccessHandle = GGG_INVALID_HANDLE;
    g_seenTxFailure = false;
    g_lastTxFailureHandle = GGG_INVALID_HANDLE;
    g_mockTimeMs = 1000;
    SystemBus::getInstance().subscribe(&g_eventListener);
}

void tearDown(void) {
    SystemBus::getInstance().reset();
}

// ----------------------------------------------------------------------------
// 1. COBS Codec Unit Tests
// ----------------------------------------------------------------------------
void test_cobs_standard_rfc_vectors(void) {
    uint8_t encBuf[64];
    uint8_t decBuf[64];

    // Case 1: Empty input
    size_t encLen = CobsCodec::encode(nullptr, 0, encBuf, sizeof(encBuf));
    TEST_ASSERT_EQUAL_UINT32(1, encLen);
    TEST_ASSERT_EQUAL_HEX8(0x01, encBuf[0]);
    size_t decLen = CobsCodec::decode(encBuf, encLen, decBuf, sizeof(decBuf));
    TEST_ASSERT_EQUAL_UINT32(0, decLen);

    // Case 2: Single 0x00 byte -> [0x01, 0x01]
    uint8_t v2[] = {0x00};
    encLen = CobsCodec::encode(v2, sizeof(v2), encBuf, sizeof(encBuf));
    TEST_ASSERT_EQUAL_UINT32(2, encLen);
    TEST_ASSERT_EQUAL_HEX8(0x01, encBuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, encBuf[1]);
    decLen = CobsCodec::decode(encBuf, encLen, decBuf, sizeof(decBuf));
    TEST_ASSERT_EQUAL_UINT32(1, decLen);
    TEST_ASSERT_EQUAL_HEX8(0x00, decBuf[0]);

    // Case 3: [0x11, 0x22, 0x00, 0x33] -> [0x03, 0x11, 0x22, 0x02, 0x33]
    uint8_t v3[] = {0x11, 0x22, 0x00, 0x33};
    encLen = CobsCodec::encode(v3, sizeof(v3), encBuf, sizeof(encBuf));
    TEST_ASSERT_EQUAL_UINT32(5, encLen);
    TEST_ASSERT_EQUAL_HEX8(0x03, encBuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x11, encBuf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x22, encBuf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02, encBuf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x33, encBuf[4]);
    decLen = CobsCodec::decode(encBuf, encLen, decBuf, sizeof(decBuf));
    TEST_ASSERT_EQUAL_UINT32(sizeof(v3), decLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v3, decBuf, sizeof(v3));

    // Case 4: Non-zero string without zero [0x11, 0x22, 0x33, 0x44] -> [0x05, 0x11, 0x22, 0x33, 0x44]
    uint8_t v4[] = {0x11, 0x22, 0x33, 0x44};
    encLen = CobsCodec::encode(v4, sizeof(v4), encBuf, sizeof(encBuf));
    TEST_ASSERT_EQUAL_UINT32(5, encLen);
    TEST_ASSERT_EQUAL_HEX8(0x05, encBuf[0]);
    decLen = CobsCodec::decode(encBuf, encLen, decBuf, sizeof(decBuf));
    TEST_ASSERT_EQUAL_UINT32(sizeof(v4), decLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v4, decBuf, sizeof(v4));
}

// ----------------------------------------------------------------------------
// 2. CRC-16 CCITT-FALSE Unit Tests
// ----------------------------------------------------------------------------
void test_crc16_ccitt_false_known_vectors(void) {
    // Known standard vector: "123456789" -> 0x29B1
    const uint8_t testStr[] = "123456789";
    uint16_t crc = Crc16Ccitt::calculate(testStr, 9);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);

    // Incremental byte update check
    uint16_t incCrc = Crc16Ccitt::INITIAL_VALUE;
    for (size_t i = 0; i < 9; i++) {
        incCrc = Crc16Ccitt::update(incCrc, testStr[i]);
    }
    TEST_ASSERT_EQUAL_HEX16(0x29B1, incCrc);
}

// ----------------------------------------------------------------------------
// 3. LoRa Polymorphic Header Packing & Bitmasks
// ----------------------------------------------------------------------------
void test_lora_polymorphic_header_packing_and_bitmasks(void) {
    TEST_ASSERT_EQUAL_UINT32(3, sizeof(LoRaCL_Header));

    // Segment header
    uint8_t segCtrl = makeControlWord(LORA_TYPE_SEGMENT, LORA_SC_UNRELIABLE, 7);
    TEST_ASSERT_EQUAL_HEX8(LORA_TYPE_SEGMENT, getMessageType(segCtrl));
    TEST_ASSERT_EQUAL_HEX8(LORA_SC_UNRELIABLE, getServiceClass(segCtrl));
    TEST_ASSERT_EQUAL_UINT8(7, getSessionId(segCtrl));
    TEST_ASSERT_EQUAL_UINT32(3, getHeaderLength(LORA_TYPE_SEGMENT));

    // ACK header
    uint8_t ackCtrl = makeControlWord(LORA_TYPE_ACK, LORA_SC_NOTIFIED, 15);
    TEST_ASSERT_EQUAL_HEX8(LORA_TYPE_ACK, getMessageType(ackCtrl));
    TEST_ASSERT_EQUAL_HEX8(LORA_SC_NOTIFIED, getServiceClass(ackCtrl));
    TEST_ASSERT_EQUAL_UINT8(15, getSessionId(ackCtrl));
    TEST_ASSERT_EQUAL_UINT32(1, getHeaderLength(LORA_TYPE_ACK));

    // Refuse header
    uint8_t refCtrl = makeControlWord(LORA_TYPE_REFUSE, 0, 31);
    TEST_ASSERT_EQUAL_HEX8(LORA_TYPE_REFUSE, getMessageType(refCtrl));
    TEST_ASSERT_EQUAL_UINT8(31, getSessionId(refCtrl));
    TEST_ASSERT_EQUAL_UINT32(2, getHeaderLength(LORA_TYPE_REFUSE));

    // Reject header
    uint8_t rejCtrl = makeControlWord(LORA_TYPE_MSG_REJECT, 0, 0);
    TEST_ASSERT_EQUAL_HEX8(LORA_TYPE_MSG_REJECT, getMessageType(rejCtrl));
    TEST_ASSERT_EQUAL_UINT32(2, getHeaderLength(LORA_TYPE_MSG_REJECT));
}

// ----------------------------------------------------------------------------
// 4. LoRa Reason Codes
// ----------------------------------------------------------------------------
void test_lora_reason_codes(void) {
    TEST_ASSERT_EQUAL_HEX8(0x01, LORA_REFUSE_INSUFFICIENT_SPACE);
    TEST_ASSERT_EQUAL_HEX8(0x02, LORA_REFUSE_DUTY_CYCLE_EXHAUSTED);
    TEST_ASSERT_EQUAL_HEX8(0x03, LORA_REFUSE_ADMIN_DISCARD);
    TEST_ASSERT_EQUAL_HEX8(0x04, LORA_REFUSE_NACK_NOTIFIED);

    TEST_ASSERT_EQUAL_HEX8(0x10, LORA_REJECT_MISSING_SEGMENT);
    TEST_ASSERT_EQUAL_HEX8(0x11, LORA_REJECT_UNKNOWN_TRANSFER);
}

// ----------------------------------------------------------------------------
// 5. LoRa Duty Cycle Token Bucket
// ----------------------------------------------------------------------------
void test_lora_duty_cycle_token_bucket(void) {
    LoRaDutyCycleTokenBucket bucket(36000);
    TEST_ASSERT_EQUAL_UINT32(36000, bucket.getAvailableBudgetMs());
    TEST_ASSERT_TRUE(bucket.canTransmit(1000));

    // Consume 10,000 ms
    TEST_ASSERT_TRUE(bucket.consume(10000));
    TEST_ASSERT_EQUAL_UINT32(26000, bucket.getAvailableBudgetMs());

    // Advance 10,000 ms of clock time -> +1ms every 100ms -> +100 ms
    bucket.update(0);
    bucket.update(10000);
    TEST_ASSERT_EQUAL_UINT32(26100, bucket.getAvailableBudgetMs());

    // Consume down to 50 ms
    bucket.consume(26050);
    TEST_ASSERT_EQUAL_UINT32(50, bucket.getAvailableBudgetMs());

    // Check refusal on insufficient budget
    TEST_ASSERT_FALSE(bucket.canTransmit(100));
    TEST_ASSERT_FALSE(bucket.consume(100));
    TEST_ASSERT_EQUAL_UINT32(50, bucket.getAvailableBudgetMs());
}

// ----------------------------------------------------------------------------
// 6. UARTCL-COBS Transmission Test
// ----------------------------------------------------------------------------
void test_uart_cobs_transmission(void) {
    RamStorage storage;
    MockStream mockStream;
    UartCobsConvergenceLayer uartCl(1, &mockStream, &mockStream, &storage);

    // Write a test bundle into storage
    StorageHandle_t h = storage.beginWrite();
    const uint8_t testPayload[] = {0xAA, 0x00, 0xBB, 0xCC, 0xDD};
    storage.writeData(h, testPayload, sizeof(testPayload));
    storage.commitWrite(h);

    bool txOk = uartCl.transmitBundle(h, 0);
    TEST_ASSERT_TRUE(txOk);

    // Verify SystemBus received TX_SUCCESS
    dispatchAllEvents();
    TEST_ASSERT_EQUAL_HEX16(muon::events::MUON_EVT_TX_SUCCESS, g_lastEvent.type);
    TEST_ASSERT_EQUAL_UINT32(h, g_lastEvent.payload.u32[0]);

    // Read bytes from mockStream
    uint8_t wireBytes[64];
    size_t wireLen = mockStream.getBytes(wireBytes, sizeof(wireBytes));
    TEST_ASSERT_GREATER_THAN(4, wireLen);
    TEST_ASSERT_EQUAL_HEX8(UARTCL_DELIMITER, wireBytes[0]);
    TEST_ASSERT_EQUAL_HEX8(UARTCL_DELIMITER, wireBytes[wireLen - 1]);

    // Decode COBS frame (excluding bounding delimiters)
    uint8_t decoded[64];
    size_t decLen = CobsCodec::decode(wireBytes + 1, wireLen - 2, decoded, sizeof(decoded));
    TEST_ASSERT_EQUAL_UINT32(1 + sizeof(testPayload) + 2, decLen); // Header + Payload + CRC16

    // Verify header byte
    TEST_ASSERT_EQUAL_HEX8(0x10, decoded[0]); // Version 1 | Data Flag

    // Verify payload bytes
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testPayload, decoded + 1, sizeof(testPayload));

    // Verify CRC-16
    uint16_t expectedCrc = Crc16Ccitt::calculate(decoded, 1 + sizeof(testPayload));
    uint16_t wireCrc = ((uint16_t)decoded[decLen - 2] << 8) | decoded[decLen - 1];
    TEST_ASSERT_EQUAL_HEX16(expectedCrc, wireCrc);
}

// ----------------------------------------------------------------------------
// 7. UARTCL-COBS Reception Stream-to-Storage with Rollback (Success)
// ----------------------------------------------------------------------------
void test_uart_cobs_rx_stream_to_storage_success(void) {
    RamStorage storage;
    MockStream mockStream;
    UartCobsConvergenceLayer uartCl(1, &mockStream, &mockStream, &storage);

    // Prepare unencoded frame: [Control(0x10)] + [Payload(4 bytes)] + [CRC16(2 bytes)]
    const uint8_t testPayload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t rawFrame[7];
    rawFrame[0] = makeControlHeader(UARTCL_FLAG_DATA);
    memcpy(rawFrame + 1, testPayload, sizeof(testPayload));
    uint16_t crc = Crc16Ccitt::calculate(rawFrame, 5);
    rawFrame[5] = (uint8_t)(crc >> 8);
    rawFrame[6] = (uint8_t)(crc & 0xFF);

    // Encode in COBS
    uint8_t cobsFrame[16];
    size_t encLen = CobsCodec::encode(rawFrame, sizeof(rawFrame), cobsFrame, sizeof(cobsFrame));

    // Feed into mockStream: 0x00 + COBS + 0x00
    mockStream.write(UARTCL_DELIMITER);
    mockStream.write(cobsFrame, encLen);
    mockStream.write(UARTCL_DELIMITER);

    // Process stream in Convergence Layer
    uartCl.tick();
    dispatchAllEvents();

    // Verify RX_READY event was emitted
    TEST_ASSERT_EQUAL_HEX16(muon::events::MUON_EVT_RX_READY, g_lastEvent.type);
    StorageHandle_t rxHandle = (StorageHandle_t)g_lastEvent.payload.u32[0];
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, rxHandle);

    // Verify storage contents
    TEST_ASSERT_EQUAL_UINT32(sizeof(testPayload), storage.getSize(rxHandle));
    uint8_t readBack[16];
    storage.readData(rxHandle, 0, readBack, sizeof(testPayload));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testPayload, readBack, sizeof(testPayload));
}

// ----------------------------------------------------------------------------
// 8. UARTCL-COBS Reception Stream-to-Storage with Rollback (Failure / CRC Error)
// ----------------------------------------------------------------------------
void test_uart_cobs_rx_rollback_on_crc_error(void) {
    RamStorage storage;
    MockStream mockStream;
    UartCobsConvergenceLayer uartCl(1, &mockStream, &mockStream, &storage);

    // Prepare frame with corrupted CRC
    uint8_t rawFrame[7] = {0x10, 0x01, 0x02, 0x03, 0x04, 0xDE, 0xAD}; // Wrong CRC
    uint8_t cobsFrame[16];
    size_t encLen = CobsCodec::encode(rawFrame, sizeof(rawFrame), cobsFrame, sizeof(cobsFrame));

    mockStream.write(UARTCL_DELIMITER);
    mockStream.write(cobsFrame, encLen);
    mockStream.write(UARTCL_DELIMITER);

    uartCl.tick();
    dispatchAllEvents();

    // Verify that NO RX_READY event was published due to CRC failure
    TEST_ASSERT_EQUAL_UINT32(0, g_eventCount);
}

// ----------------------------------------------------------------------------
// 9. LoRaCL Segmentation and Unreliable Service (SC = 0)
// ----------------------------------------------------------------------------
void test_lora_unreliable_multi_segment_tx_and_rx(void) {
    RamStorage storageTx;
    RamStorage storageRx;
    MockLoRaModem modemTx;
    MockLoRaModem modemRx;

    modemTx.linkPeer(&modemRx);
    modemRx.linkPeer(&modemTx);

    LoRaConfig config = {868.0, 9, 250.0, 5, 14, 0x12, 8};
    LoRaConvergenceLayer loraTx(0, &modemTx, &storageTx, config, false);
    LoRaConvergenceLayer loraRx(0, &modemRx, &storageRx, config, false);

    loraTx.begin();
    loraRx.begin();

    // Create a 500-byte bundle (requires 2 segments with MTU 255: 252 + 248 bytes)
    StorageHandle_t hTx = storageTx.beginWrite();
    uint8_t largePayload[500];
    for (size_t i = 0; i < sizeof(largePayload); i++) {
        largePayload[i] = (uint8_t)(i & 0xFF);
    }
    storageTx.writeData(hTx, largePayload, sizeof(largePayload));
    storageTx.commitWrite(hTx);

    // Transmit with QoS = 0 (Unreliable)
    bool txStarted = loraTx.transmitBundle(hTx, 0);
    TEST_ASSERT_TRUE(txStarted);

    dispatchAllEvents();

    // Verify TX and RX completed
    TEST_ASSERT_TRUE(g_seenRxReady);
    TEST_ASSERT_TRUE(g_seenTxSuccess);
    StorageHandle_t hRx = g_lastRxHandle;
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, hRx);

    // Verify reassembled data matches transmitted data exactly
    TEST_ASSERT_EQUAL_UINT32(sizeof(largePayload), storageRx.getSize(hRx));
    uint8_t rxBuffer[500];
    storageRx.readData(hRx, 0, rxBuffer, sizeof(largePayload));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(largePayload, rxBuffer, sizeof(largePayload));
}

// ----------------------------------------------------------------------------
// 10. LoRaCL Notified Service with Block-ACK (SC = 1)
// ----------------------------------------------------------------------------
void test_lora_notified_service_with_ack(void) {
    RamStorage storageTx;
    RamStorage storageRx;
    MockLoRaModem modemTx;
    MockLoRaModem modemRx;

    modemTx.linkPeer(&modemRx);
    modemRx.linkPeer(&modemTx);

    LoRaConfig config = {868.0, 9, 250.0, 5, 14, 0x12, 8};
    LoRaConvergenceLayer loraTx(0, &modemTx, &storageTx, config, false);
    LoRaConvergenceLayer loraRx(0, &modemRx, &storageRx, config, false);

    loraTx.begin();
    loraRx.begin();

    // 100-byte bundle
    StorageHandle_t hTx = storageTx.beginWrite();
    uint8_t payload[100];
    memset(payload, 0x42, sizeof(payload));
    storageTx.writeData(hTx, payload, sizeof(payload));
    storageTx.commitWrite(hTx);

    // Transmit with QoS = 1 (Notified)
    bool ok = loraTx.transmitBundle(hTx, 1);
    TEST_ASSERT_TRUE(ok);

    // Receiver should have received segment, committed write, and sent BDL_XFER_ACK back
    dispatchAllEvents();

    // Check that both RX_READY and TX_SUCCESS were published
    TEST_ASSERT_TRUE(g_seenRxReady);
    TEST_ASSERT_TRUE(g_seenTxSuccess);
    TEST_ASSERT_EQUAL_UINT32(hTx, g_lastTxSuccessHandle);
}

// ----------------------------------------------------------------------------
// 11. LoRaCL Reassembly Timeout and Rollback on Dropped Segment
// ----------------------------------------------------------------------------
void test_lora_reassembly_timeout_and_rollback(void) {
    RamStorage storageRx;
    MockLoRaModem modemRx;

    LoRaConfig config = {868.0, 9, 250.0, 5, 14, 0x12, 8};
    LoRaConvergenceLayer loraRx(0, &modemRx, &storageRx, config, false, 2000);
    loraRx.setTimeProvider(getMockTimeMs);
    loraRx.begin();

    // Inject Segment 0 of 2 (Total segments = 2, Segment index = 0)
    LoRaCL_Header hdr;
    hdr.control_session = makeControlWord(LORA_TYPE_SEGMENT, LORA_SC_NOTIFIED, 3);
    hdr.data.total_segments = 2;
    hdr.data.segment_index = 0;

    uint8_t packet[32];
    memcpy(packet, &hdr, 3);
    memset(packet + 3, 0xEE, 20);

    modemRx.injectPacket(packet, 23);

    // Verify receiver is in RECEIVING state
    TEST_ASSERT_EQUAL(LoRaConvergenceLayer::RxState::RECEIVING, loraRx.getRxState());

    // Advance clock past 2000 ms timeout
    g_mockTimeMs += 2500;
    loraRx.tick();

    // Verify receiver aborted reassembly and returned to IDLE
    TEST_ASSERT_EQUAL(LoRaConvergenceLayer::RxState::IDLE, loraRx.getRxState());

    // Verify NACK (XFER_REFUSE) packet was sent back
    TEST_ASSERT_EQUAL_UINT32(1, modemRx.getTxCount());
    const uint8_t* txPkt = modemRx.getLastTxPacket();
    TEST_ASSERT_EQUAL_HEX8(LORA_TYPE_REFUSE, getMessageType(txPkt[0]));
    TEST_ASSERT_EQUAL_HEX8(LORA_REFUSE_NACK_NOTIFIED, txPkt[1]);
}

// ----------------------------------------------------------------------------
// 12. Paper Experiment End-to-End Two-Node LoRa Bundle Exchange
// ----------------------------------------------------------------------------
void test_paper_experiment_two_nodes_exchange(void) {
    RamStorage storageA;
    RamStorage storageB;
    MockLoRaModem modemA;
    MockLoRaModem modemB;

    modemA.linkPeer(&modemB);
    modemB.linkPeer(&modemA);

    LoRaConfig config = {868.0, 9, 250.0, 5, 14, 0x12, 8};
    LoRaConvergenceLayer loraA(0, &modemA, &storageA, config, false);
    LoRaConvergenceLayer loraB(0, &modemB, &storageB, config, false);

    loraA.begin();
    loraB.begin();

    // Routing and CLM on Node A
    StaticRoutingEngine routerA;
    routerA.setLocalEndpoint(IpnEndpointId{1, 1});
    routerA.addRoute(2, 0); // Node 2 via LoRa (Link 0)

    ConvergenceLayerManager clmA;
    clmA.registerAdapter(&loraA);
    clmA.setRoutingEngine(&routerA);

    // Routing and CLM on Node B
    StaticRoutingEngine routerB;
    routerB.setLocalEndpoint(IpnEndpointId{2, 1});
    routerB.addRoute(1, 0); // Node 1 via LoRa (Link 0)

    ConvergenceLayerManager clmB;
    clmB.registerAdapter(&loraB);
    clmB.setRoutingEngine(&routerB);

    // Node A creates a telemetry bundle destined to ipn:2.1
    StorageHandle_t bdlHandleA = storageA.beginWrite();
    const char telemetryData[] = "{\"node\":1,\"temp\":24.5,\"press\":1013.2}";
    storageA.writeData(bdlHandleA, (const uint8_t*)telemetryData, strlen(telemetryData));
    storageA.commitWrite(bdlHandleA);

    // Node A routes bundle to Node B
    uint8_t outLink = 0xFF;
    RouteDecision dec = routerA.evaluateNode(2, outLink);
    TEST_ASSERT_EQUAL(RouteDecision::FORWARD_DIRECT, dec);
    TEST_ASSERT_EQUAL_UINT8(0, outLink);

    // CLM transmits bundle via LoRa
    bool txStarted = clmA.transmit(outLink, bdlHandleA, 0);
    TEST_ASSERT_TRUE(txStarted);

    dispatchAllEvents();

    // Node B received bundle via LoRaCL and published MUON_EVT_RX_READY!
    TEST_ASSERT_TRUE(g_seenRxReady);
    StorageHandle_t bdlHandleB = g_lastRxHandle;
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, bdlHandleB);

    // Verify Node B's storage holds the identical telemetry bundle
    TEST_ASSERT_EQUAL_UINT32(strlen(telemetryData), storageB.getSize(bdlHandleB));
    char receivedData[64] = {0};
    storageB.readData(bdlHandleB, 0, (uint8_t*)receivedData, strlen(telemetryData));
    TEST_ASSERT_EQUAL_STRING(telemetryData, receivedData);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_cobs_standard_rfc_vectors);
    RUN_TEST(test_crc16_ccitt_false_known_vectors);
    RUN_TEST(test_lora_polymorphic_header_packing_and_bitmasks);
    RUN_TEST(test_lora_reason_codes);
    RUN_TEST(test_lora_duty_cycle_token_bucket);
    RUN_TEST(test_uart_cobs_transmission);
    RUN_TEST(test_uart_cobs_rx_stream_to_storage_success);
    RUN_TEST(test_uart_cobs_rx_rollback_on_crc_error);
    RUN_TEST(test_lora_unreliable_multi_segment_tx_and_rx);
    RUN_TEST(test_lora_notified_service_with_ack);
    RUN_TEST(test_lora_reassembly_timeout_and_rollback);
    RUN_TEST(test_paper_experiment_two_nodes_exchange);

    return UNITY_END();
}
