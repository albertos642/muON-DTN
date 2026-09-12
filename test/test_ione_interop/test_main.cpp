/**
 * @file test_main.cpp
 * @brief Interoperability test suite validating muON-DTN <-> IONe-uartcl-cobs compatibility.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <unity.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

#include <ggg/system/SystemBus.h>
#include <ggg/hal/RamStorage.h>
#include <ggg/hal/IStream.h>

#include <muon/bpa/BundleTypes.h>
#include <muon/bpa/BundleAgent.h>
#include <muon/bpa/CBORSerializer.h>
#include <muon/bpa/MuonEvents.h>
#include <muon/routing/StaticRoutingEngine.h>
#include <muon/uartcobs/CobsCodec.h>
#include <muon/uartcobs/Crc16Ccitt.h>
#include <muon/uartcobs/UartCobsConvergenceLayer.h>

// ----------------------------------------------------------------------------
// Reference IONe UART-COBS structures and implementations for direct verification
// ----------------------------------------------------------------------------
#define ION_UARTCOBS_VERSION_1  0x10
#define ION_UARTCOBS_FLAG_DATA  0x00
#define ION_UARTCOBS_FLAG_SYNC  0x08

struct IonUartCobsDescriptor {
    char     uart_file_descriptor[50];
    uint32_t baud_rate;
    int      isOpen;
};

// IONe's exact corrected CRC-16 table
static const uint16_t s_ione_crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97b8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

static uint16_t ione_compute_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ s_ione_crc16_table[(crc >> 8) ^ data[i]];
    }
    return crc;
}

static size_t ione_cobs_encode(const uint8_t *ptr, size_t length, uint8_t *dst) {
    size_t read_index = 0, write_index = 1, code_index = 0;
    uint8_t code = 1;
    while (read_index < length) {
        if (ptr[read_index] == 0) {
            dst[code_index] = code;
            code = 1;
            code_index = write_index++;
            read_index++;
        } else {
            dst[write_index++] = ptr[read_index++];
            code++;
            if (code == 0xFF) {
                dst[code_index] = code;
                code = 1;
                code_index = write_index++;
            }
        }
    }
    dst[code_index] = code;
    return write_index;
}

static size_t ione_cobs_decode(const uint8_t *ptr, size_t length, uint8_t *dst) {
    size_t read_index = 0, write_index = 0;
    uint8_t code, i;
    while (read_index < length) {
        code = ptr[read_index];
        if (read_index + code > length && code != 1) return 0;
        read_index++;
        for (i = 1; i < code; i++) dst[write_index++] = ptr[read_index++];
        if (code < 0xFF && read_index < length) dst[write_index++] = 0;
    }
    return write_index;
}

static int ione_parse_uart_cobs_spec(const char *socketSpec, IonUartCobsDescriptor *uart) {
    if (socketSpec == NULL || *socketSpec == '\0' || uart == NULL) return -1;
    char *copy = strdup(socketSpec);
    if (!copy) return -1;
    char *delim = strchr(copy, ',');
    if (!delim) {
        delim = strchr(copy, ':');
    }
    if (!delim) {
        free(copy);
        return -1;
    }
    *delim = '\0';
    char *baud = delim + 1;
    memset(uart->uart_file_descriptor, 0, sizeof(uart->uart_file_descriptor));
    strncpy(uart->uart_file_descriptor, copy, sizeof(uart->uart_file_descriptor) - 1);
    if (strlen(uart->uart_file_descriptor) != 0 && strlen(baud) != 0) {
        uart->baud_rate = (uint32_t)atoi(baud);
        free(copy);
        return 0;
    }
    free(copy);
    return -1;
}

// ----------------------------------------------------------------------------
// Test Harness Streams
// ----------------------------------------------------------------------------
class PipeStream : public ggg::hal::IInputStream, public ggg::hal::IOutputStream {
public:
    std::vector<uint8_t> buffer;
    size_t readPos = 0;

    size_t write(uint8_t b) override {
        buffer.push_back(b);
        return 1;
    }

    size_t write(const uint8_t* buf, size_t size) override {
        buffer.insert(buffer.end(), buf, buf + size);
        return size;
    }

    void flush() override {}

    size_t available() override {
        return (readPos < buffer.size()) ? (buffer.size() - readPos) : 0;
    }

    int read() override {
        if (readPos < buffer.size()) {
            return buffer[readPos++];
        }
        return -1;
    }

    void flushRX() override {
        readPos = buffer.size();
    }

    void clear() {
        buffer.clear();
        readPos = 0;
    }
};

class DummyTimeProvider : public muon::bpa::ITimeProvider {
public:
    uint32_t currentTime = 100000;
    uint32_t getDtnTimestamp() const override {
        return currentTime;
    }
};

// ----------------------------------------------------------------------------
// Event Listener & Setup/Teardown
// ----------------------------------------------------------------------------
class TestInteropEventListener : public ggg::system::IEventListener {
public:
    bool rxEventFired = false;
    ggg::hal::StorageHandle_t rxHandle = GGG_INVALID_HANDLE;

    void reset() {
        rxEventFired = false;
        rxHandle = GGG_INVALID_HANDLE;
    }

    void onEvent(const ggg::system::SystemEvent& ev) override {
        if (ev.type == muon::events::MUON_EVT_RX_READY) {
            rxEventFired = true;
            rxHandle = ev.payload.u32[0];
        }
    }
};

static TestInteropEventListener g_interopListener;

static void dispatchAllEvents() {
    while (ggg::system::SystemBus::getInstance().dispatchOne()) {}
}

void setUp(void) {
    ggg::system::SystemBus::getInstance().init();
    ggg::system::SystemBus::getInstance().subscribe(&g_interopListener);
    g_interopListener.reset();
}

void tearDown(void) {
    dispatchAllEvents();
    ggg::system::SystemBus::getInstance().unsubscribe(&g_interopListener);
}

// ============================================================================
// TEST 1: CRC-16 Bit-for-Bit Equivalence Across Full 256 Table & Edge Cases
// ============================================================================
void test_crc16_full_table_and_edge_cases(void) {
    // 1. Table entry equality
    for (size_t i = 0; i < 256; i++) {
        uint16_t muonEntry = muon::uartcobs::Crc16Ccitt::update((uint16_t)(i << 8), (uint8_t)0);
        TEST_ASSERT_EQUAL_HEX16(s_ione_crc16_table[i], muonEntry);
    }

    // 2. Standard CCITT-FALSE vector "123456789" -> 0x29B1
    const uint8_t stdVec[] = "123456789";
    uint16_t crcIon = ione_compute_crc16(stdVec, 9);
    uint16_t crcMuon = muon::uartcobs::Crc16Ccitt::calculate(stdVec, 9);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crcIon);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crcMuon);

    // 3. Test edge case hitting previously corrupted entries 0x21 and 0x23:
    // When initial CRC is 0xFFFF, 0xFFFF >> 8 = 0xFF.
    // 0xFF ^ 0xDE = 0x21 (index 33).
    // 0xFF ^ 0xDC = 0x23 (index 35).
    const uint8_t edgeVec1[] = { 0xDE };
    const uint8_t edgeVec2[] = { 0xDC };
    const uint8_t mixedVec[] = { 0x10, 0xDE, 0x00, 0xDC, 0xFF, 0x82, 0x07, 0x34 };

    TEST_ASSERT_EQUAL_HEX16(ione_compute_crc16(edgeVec1, 1), muon::uartcobs::Crc16Ccitt::calculate(edgeVec1, 1));
    TEST_ASSERT_EQUAL_HEX16(0xCB43, muon::uartcobs::Crc16Ccitt::calculate(edgeVec1, 1));

    TEST_ASSERT_EQUAL_HEX16(ione_compute_crc16(edgeVec2, 1), muon::uartcobs::Crc16Ccitt::calculate(edgeVec2, 1));
    TEST_ASSERT_EQUAL_HEX16(0xEB01, muon::uartcobs::Crc16Ccitt::calculate(edgeVec2, 1));

    TEST_ASSERT_EQUAL_HEX16(ione_compute_crc16(mixedVec, sizeof(mixedVec)), 
                            muon::uartcobs::Crc16Ccitt::calculate(mixedVec, sizeof(mixedVec)));
}

// ============================================================================
// TEST 2: COBS Cross-Codec Symmetry (IONe encodes -> muON decodes, and vice versa)
// ============================================================================
void test_cobs_cross_codec_symmetry(void) {
    // Arbitrary binary sequence with multiple zeros, consecutive zeros, runs > 254 bytes
    std::vector<uint8_t> plainData;
    plainData.push_back(0x00);
    plainData.push_back(0x10);
    plainData.push_back(0x00);
    plainData.push_back(0x00);
    for (int i = 1; i <= 300; i++) {
        plainData.push_back((uint8_t)(i % 255 == 0 ? 1 : (i % 255)));
    }
    plainData.push_back(0x00);

    // Path A: IONe encodes -> muON block decodes and stream decodes
    uint8_t ionEncoded[600];
    size_t encLen = ione_cobs_encode(plainData.data(), plainData.size(), ionEncoded);
    TEST_ASSERT_GREATER_THAN(0, encLen);

    uint8_t muonDecoded[600];
    size_t decLen = muon::uartcobs::CobsCodec::decode(ionEncoded, encLen, muonDecoded, sizeof(muonDecoded));
    TEST_ASSERT_EQUAL_UINT(plainData.size(), decLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plainData.data(), muonDecoded, plainData.size());

    // Stream decoder validation
    muon::uartcobs::CobsStreamDecoder streamDecoder;
    std::vector<uint8_t> streamDecoded;
    // Deliver start delimiter
    uint8_t dummy = 0;
    TEST_ASSERT_EQUAL(muon::uartcobs::CobsStreamDecoder::Result::FRAME_START, streamDecoder.feedByte(0x00, dummy));
    for (size_t i = 0; i < encLen; i++) {
        uint8_t outB = 0;
        auto res = streamDecoder.feedByte(ionEncoded[i], outB);
        if (res == muon::uartcobs::CobsStreamDecoder::Result::DECODED_BYTE) {
            streamDecoded.push_back(outB);
        }
    }
    TEST_ASSERT_EQUAL(muon::uartcobs::CobsStreamDecoder::Result::FRAME_END, streamDecoder.feedByte(0x00, dummy));
    TEST_ASSERT_EQUAL_UINT(plainData.size(), streamDecoded.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plainData.data(), streamDecoded.data(), plainData.size());

    // Path B: muON encodes -> IONe decodes
    uint8_t muonEncoded[600];
    size_t muonEncLen = muon::uartcobs::CobsCodec::encode(plainData.data(), plainData.size(), muonEncoded, sizeof(muonEncoded));
    TEST_ASSERT_EQUAL_UINT(encLen, muonEncLen);

    uint8_t ionDecoded[600];
    size_t ionDecLen = ione_cobs_decode(muonEncoded, muonEncLen, ionDecoded);
    TEST_ASSERT_EQUAL_UINT(plainData.size(), ionDecLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plainData.data(), ionDecoded, plainData.size());
}

static uint32_t s_capturedDtnTime = 0;
static void test_timesync_cb(uint32_t dtnTime) {
    s_capturedDtnTime = dtnTime;
}

// ============================================================================
// TEST 3: Timesync Handshake Flow (muON Request 3B -> IONe Deframes & Responds 7B -> muON Syncs)
// ============================================================================
void test_timesync_handshake_flow(void) {
    PipeStream pipe;
    ggg::hal::RamStorage storage;
    muon::uartcobs::UartCobsConvergenceLayer cl(1, &pipe, &pipe, &storage);

    s_capturedDtnTime = 0;
    cl.setTimeSyncCallback(test_timesync_cb);

    // 1. muON initiates time sync request
    cl.sendSyncRequest();

    // Verify wire contains: 0x00, COBS encoded request, 0x00
    TEST_ASSERT_GREATER_THAN(3, pipe.buffer.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, pipe.buffer.front());
    TEST_ASSERT_EQUAL_HEX8(0x00, pipe.buffer.back());

    // Extract COBS payload on the wire
    std::vector<uint8_t> wireReq(pipe.buffer.begin() + 1, pipe.buffer.end() - 1);
    uint8_t clearReq[16];
    size_t decodedReqLen = ione_cobs_decode(wireReq.data(), wireReq.size(), clearReq);

    // Exact 3-byte request: [Control(0x18), CRC_H, CRC_L]
    TEST_ASSERT_EQUAL_UINT(3, decodedReqLen);
    TEST_ASSERT_EQUAL_HEX8(ION_UARTCOBS_VERSION_1 | ION_UARTCOBS_FLAG_SYNC, clearReq[0]);

    uint16_t reqCrc = (clearReq[1] << 8) | clearReq[2];
    uint16_t calcCrc = ione_compute_crc16(clearReq, 1);
    TEST_ASSERT_EQUAL_HEX16(calcCrc, reqCrc);

    // 2. Simulate IONe responding with DTN Epoch (seconds since 2000-01-01 00:00:00 UTC)
    // Suppose current time is 789123456
    uint32_t simulatedDtnTime = 789123456;
    uint8_t syncResp[7];
    syncResp[0] = ION_UARTCOBS_VERSION_1 | ION_UARTCOBS_FLAG_SYNC;
    syncResp[1] = (simulatedDtnTime >> 24) & 0xFF;
    syncResp[2] = (simulatedDtnTime >> 16) & 0xFF;
    syncResp[3] = (simulatedDtnTime >> 8) & 0xFF;
    syncResp[4] = simulatedDtnTime & 0xFF;
    uint16_t respCrc = ione_compute_crc16(syncResp, 5);
    syncResp[5] = (respCrc >> 8) & 0xFF;
    syncResp[6] = respCrc & 0xFF;

    uint8_t cobsResp[16];
    size_t cobsRespLen = ione_cobs_encode(syncResp, 7, cobsResp);

    // 3. Send IONe response over wire to muON
    pipe.clear();
    pipe.write(0x00);
    pipe.write(cobsResp, cobsRespLen);
    pipe.write(0x00);

    // muON CL processes incoming stream
    cl.tick();

    TEST_ASSERT_EQUAL_UINT32(simulatedDtnTime, s_capturedDtnTime);
}

// ============================================================================
// TEST 4: Bundle Ingress: IONe BPv7 Indefinite Array (0x9F ... 0xFF) -> muON Delivery
// ============================================================================
void test_bundle_ingress_ione_indefinite_array_to_muon(void) {
    PipeStream pipe;
    ggg::hal::RamStorage storage;
    DummyTimeProvider timeProv;
    muon::routing::StaticRoutingEngine router;
    muon::bpa::BundleAgent bpa(&storage, &timeProv, &router);
    muon::uartcobs::UartCobsConvergenceLayer cl(1, &pipe, &pipe, &storage);

    // Construct genuine BPv7 bundle as emitted by IONe:
    // Indefinite Array: 0x9F
    // Primary Block (0x88): 8 items
    //   version: 7
    //   control flags: 0
    //   crc type: 0
    //   dst: ipn:1.10 (0x83, 0x01, 0x01, 0x0A)
    //   src: ipn:3.1  (0x83, 0x01, 0x03, 0x01)
    //   rpt: ipn:3.1  (0x83, 0x01, 0x03, 0x01)
    //   timestamp: [789000, 1] (0x82, 0x1A, 0x00, 0x0C, 0x0A, 0x08, 0x01)
    //   lifetime: 86400 (0x1A, 0x00, 0x01, 0x51, 0x80)
    // Payload Block (0x85): 5 items
    //   type: 1, nbr: 1, flags: 0, crc: 0
    //   payload bstr: "Hello from IONe!" (0x50, ...)
    // Break: 0xFF
    const char* textPayload = "Hello from IONe!";
    size_t textLen = strlen(textPayload);

    std::vector<uint8_t> ioneBundle;
    ioneBundle.push_back(0x9F); // Indefinite-length array open

    // Primary Block
    ioneBundle.push_back(0x88); // Array of 8 elements
    ioneBundle.push_back(0x07); // Version 7
    ioneBundle.push_back(0x00); // Flags 0
    ioneBundle.push_back(0x00); // CRC 0
    // Dst: ipn:1.10 -> [2, [1, 10]]
    ioneBundle.push_back(0x82); ioneBundle.push_back(0x02); ioneBundle.push_back(0x82); ioneBundle.push_back(0x01); ioneBundle.push_back(0x0A);
    // Src: ipn:3.1 -> [2, [3, 1]]
    ioneBundle.push_back(0x82); ioneBundle.push_back(0x02); ioneBundle.push_back(0x82); ioneBundle.push_back(0x03); ioneBundle.push_back(0x01);
    // Rpt: ipn:3.1 -> [2, [3, 1]]
    ioneBundle.push_back(0x82); ioneBundle.push_back(0x02); ioneBundle.push_back(0x82); ioneBundle.push_back(0x03); ioneBundle.push_back(0x01);
    // Timestamp: [789000, 1]
    ioneBundle.push_back(0x82);
    ioneBundle.push_back(0x1A);
    ioneBundle.push_back((789000 >> 24) & 0xFF);
    ioneBundle.push_back((789000 >> 16) & 0xFF);
    ioneBundle.push_back((789000 >> 8) & 0xFF);
    ioneBundle.push_back(789000 & 0xFF);
    ioneBundle.push_back(0x01); // Seq 1
    // Lifetime: 86400
    ioneBundle.push_back(0x1A);
    ioneBundle.push_back(0x00); ioneBundle.push_back(0x01); ioneBundle.push_back(0x51); ioneBundle.push_back(0x80);

    // Payload Block
    ioneBundle.push_back(0x85); // Array of 5 elements
    ioneBundle.push_back(0x01); // Block Type 1
    ioneBundle.push_back(0x01); // Block Nbr 1
    ioneBundle.push_back(0x00); // Flags 0
    ioneBundle.push_back(0x00); // CRC 0
    // Byte string header
    ioneBundle.push_back((uint8_t)(0x40 | textLen));
    ioneBundle.insert(ioneBundle.end(), textPayload, textPayload + textLen);

    // Break code
    ioneBundle.push_back(0xFF);

    // Encapsulate into UARTCL-COBS frame: [0x10] + [bundle] + [CRC16] -> COBS
    std::vector<uint8_t> clearFrame;
    clearFrame.push_back(ION_UARTCOBS_VERSION_1 | ION_UARTCOBS_FLAG_DATA);
    clearFrame.insert(clearFrame.end(), ioneBundle.begin(), ioneBundle.end());
    uint16_t crc = ione_compute_crc16(clearFrame.data(), clearFrame.size());
    clearFrame.push_back((crc >> 8) & 0xFF);
    clearFrame.push_back(crc & 0xFF);

    std::vector<uint8_t> cobsFrame(clearFrame.size() + (clearFrame.size() / 254) + 2);
    size_t cobsLen = ione_cobs_encode(clearFrame.data(), clearFrame.size(), cobsFrame.data());

    // Feed to muON convergence layer
    pipe.write(0x00);
    pipe.write(cobsFrame.data(), cobsLen);
    pipe.write(0x00);

    g_interopListener.reset();
    cl.tick();
    dispatchAllEvents();
    TEST_ASSERT_TRUE(g_interopListener.rxEventFired);
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, g_interopListener.rxHandle);
    ggg::hal::StorageHandle_t rxHandle = g_interopListener.rxHandle;

    // Now test that CBORSerializer::deserializeBundleHeader parses this indefinite bundle successfully
    muon::bpa::StorageInputStream storageIn(storage, rxHandle, 0);
    muon::bpa::BundleHeader parsedHeader;
    size_t parsedPayloadLen = 0;
    bool parseSuccess = muon::bpa::CBORSerializer::deserializeBundleHeader(storageIn, parsedHeader, parsedPayloadLen);

    TEST_ASSERT_TRUE(parseSuccess);
    TEST_ASSERT_EQUAL_UINT8(7, parsedHeader.version);
    TEST_ASSERT_EQUAL_UINT32(1, parsedHeader.destination.nodeNbr);
    TEST_ASSERT_EQUAL_UINT32(10, parsedHeader.destination.serviceNbr);
    TEST_ASSERT_EQUAL_UINT32(3, parsedHeader.source.nodeNbr);
    TEST_ASSERT_EQUAL_UINT32(1, parsedHeader.source.serviceNbr);
    TEST_ASSERT_EQUAL_UINT(textLen, parsedPayloadLen);

    // Read payload bytes
    std::vector<uint8_t> recoveredPayload(parsedPayloadLen + 1, 0);
    storageIn.readBytes(recoveredPayload.data(), parsedPayloadLen);
    TEST_ASSERT_EQUAL_STRING(textPayload, (char*)recoveredPayload.data());
}

// ============================================================================
// TEST 5: Bundle Egress: muON-DTN -> IONe receiveFrameByUartCobs
// ============================================================================
void test_bundle_egress_muon_to_ione(void) {
    PipeStream pipe;
    ggg::hal::RamStorage storage;
    DummyTimeProvider timeProv;
    muon::routing::StaticRoutingEngine router;
    muon::bpa::BundleAgent bpa(&storage, &timeProv, &router);
    muon::uartcobs::UartCobsConvergenceLayer cl(1, &pipe, &pipe, &storage);

    const char* outgoingMsg = "Sensor Telemetry: Temp=22.4C Hum=45%";
    size_t msgLen = strlen(outgoingMsg);

    // Generate bundle in storage via BPA
    bool sendOk = bpa.sendLocalData(
        muon::bpa::IpnEndpointId{3, 1}, // Dest: IONe node 3.1
        reinterpret_cast<const uint8_t*>(outgoingMsg),
        msgLen,
        1, // Priority
        3600 // Lifetime
    );
    TEST_ASSERT_TRUE(sendOk);

    muon::bpa::BundleMetadata meta;
    TEST_ASSERT_TRUE(bpa.getMetadataTable().getOldest(0, meta));
    ggg::hal::StorageHandle_t bdlHandle = meta.storageHandle;
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, bdlHandle);

    // Transmit via UARTCL-COBS
    pipe.clear();
    bool txOk = cl.transmitBundle(bdlHandle, 1);
    TEST_ASSERT_TRUE(txOk);

    // Now test that IONe deframer receives and verifies it
    TEST_ASSERT_GREATER_THAN(4, pipe.buffer.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, pipe.buffer.front());
    TEST_ASSERT_EQUAL_HEX8(0x00, pipe.buffer.back());

    std::vector<uint8_t> wireBytes(pipe.buffer.begin() + 1, pipe.buffer.end() - 1);
    uint8_t ionDecoded[512];
    size_t decodedLen = ione_cobs_decode(wireBytes.data(), wireBytes.size(), ionDecoded);

    // Verify minimum length: 1B control + CBOR bundle + 2B CRC
    TEST_ASSERT_GREATER_OR_EQUAL(4, decodedLen);
    TEST_ASSERT_EQUAL_HEX8(ION_UARTCOBS_VERSION_1 | ION_UARTCOBS_FLAG_DATA, ionDecoded[0]);

    // Verify CRC-16 computed by IONe matches received CRC
    uint16_t rcvCrc = (ionDecoded[decodedLen - 2] << 8) | ionDecoded[decodedLen - 1];
    uint16_t calcCrc = ione_compute_crc16(ionDecoded, decodedLen - 2);
    TEST_ASSERT_EQUAL_HEX16(calcCrc, rcvCrc);

    // Extract payload from IONe viewpoint (strip header and CRC)
    size_t bundleBytesLen = decodedLen - 3;
    uint8_t* bundleBytes = ionDecoded + 1;

    // RFC 9171 Section 4.1 verification: Bundle must be encoded as an indefinite array (0x9F ... 0xFF)
    TEST_ASSERT_EQUAL_HEX8(0x9F, bundleBytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bundleBytes[bundleBytesLen - 1]);

    // Verify bundle is valid CBOR
    muon::bpa::MemoryInputStream cborIn(bundleBytes, bundleBytesLen);
    muon::bpa::BundleHeader hdr;
    size_t payloadLen = 0;
    bool ok = muon::bpa::CBORSerializer::deserializeBundleHeader(cborIn, hdr, payloadLen);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(3, hdr.destination.nodeNbr);
    TEST_ASSERT_EQUAL_UINT32(1, hdr.destination.serviceNbr);
    TEST_ASSERT_EQUAL_UINT(msgLen, payloadLen);

    std::vector<uint8_t> payloadBuf(payloadLen + 1, 0);
    cborIn.readBytes(payloadBuf.data(), payloadLen);
    TEST_ASSERT_EQUAL_STRING(outgoingMsg, (char*)payloadBuf.data());
}

// ============================================================================
// TEST 6: Injected CRC Corruption and Storage Rollback
// ============================================================================
void test_crc_corruption_and_rollback(void) {
    PipeStream pipe;
    ggg::hal::RamStorage storage;
    muon::uartcobs::UartCobsConvergenceLayer cl(1, &pipe, &pipe, &storage);

    size_t initialSlots = storage.getAvailableSlots();

    // Create a valid frame
    uint8_t plain[] = { 0x10, 0x82, 0x01, 0x02, 0x03, 0x04 };
    uint16_t crc = ione_compute_crc16(plain, sizeof(plain));
    
    std::vector<uint8_t> frame(plain, plain + sizeof(plain));
    frame.push_back((crc >> 8) & 0xFF);
    frame.push_back(crc & 0xFF);

    // Corrupt one byte of payload
    frame[3] ^= 0xFF;

    uint8_t encoded[64];
    size_t encLen = ione_cobs_encode(frame.data(), frame.size(), encoded);

    pipe.write(0x00);
    pipe.write(encoded, encLen);
    pipe.write(0x00);

    g_interopListener.reset();
    cl.tick();
    dispatchAllEvents();

    // Must NOT fire event, and storage slot must be rolled back
    TEST_ASSERT_FALSE(g_interopListener.rxEventFired);
    TEST_ASSERT_EQUAL_UINT(initialSlots, storage.getAvailableSlots());
}

// ============================================================================
// TEST 7: Parse UART-COBS Spec Robustness (Valid, Missing Comma, Bad Baud, Free)
// ============================================================================
void test_parse_uart_cobs_spec_robustness(void) {
    IonUartCobsDescriptor desc = {};

    // 1. Valid POSIX path
    int r1 = ione_parse_uart_cobs_spec("/dev/ttyUSB0,115200", &desc);
    TEST_ASSERT_EQUAL(0, r1);
    TEST_ASSERT_EQUAL_STRING("/dev/ttyUSB0", desc.uart_file_descriptor);
    TEST_ASSERT_EQUAL_UINT32(115200, desc.baud_rate);

    // 2. Valid Windows COM port spec with comma
    int r2 = ione_parse_uart_cobs_spec("COM3,9600", &desc);
    TEST_ASSERT_EQUAL(0, r2);
    TEST_ASSERT_EQUAL_STRING("COM3", desc.uart_file_descriptor);
    TEST_ASSERT_EQUAL_UINT32(9600, desc.baud_rate);

    // 3. Valid spec with colon delimiter
    int r3 = ione_parse_uart_cobs_spec("/dev/ttyUSB0:115200", &desc);
    TEST_ASSERT_EQUAL(0, r3);
    TEST_ASSERT_EQUAL_STRING("/dev/ttyUSB0", desc.uart_file_descriptor);
    TEST_ASSERT_EQUAL_UINT32(115200, desc.baud_rate);

    // 4. Invalid: missing delimiter (Must NOT crash/segfault, must return -1)
    int r3_inv = ione_parse_uart_cobs_spec("/dev/ttyUSB0", &desc);
    TEST_ASSERT_EQUAL(-1, r3_inv);

    // 4. Invalid: empty string
    int r4 = ione_parse_uart_cobs_spec("", &desc);
    TEST_ASSERT_EQUAL(-1, r4);

    // 5. Invalid: NULL
    int r5 = ione_parse_uart_cobs_spec(NULL, &desc);
    TEST_ASSERT_EQUAL(-1, r5);

    // 6. Invalid: missing baud rate after comma
    int r6 = ione_parse_uart_cobs_spec("/dev/ttyACM0,", &desc);
    TEST_ASSERT_EQUAL(-1, r6);
}

// ============================================================================
// TEST 8: Full End-to-End Roundtrip Ping-Pong Simulation (Node B muON <-> Node C IONe)
// ============================================================================
void test_roundtrip_ping_pong_simulation(void) {
    PipeStream bToC; // Node B (muON) -> Node C (IONe)
    PipeStream cToB; // Node C (IONe) -> Node B (muON)

    ggg::hal::RamStorage storage;
    DummyTimeProvider timeProv;
    muon::routing::StaticRoutingEngine router;
    muon::bpa::BundleAgent bpa(&storage, &timeProv, &router);
    muon::uartcobs::UartCobsConvergenceLayer cl(1, &cToB, &bToC, &storage);

    // 1. Node B (muON) sends telemetry bundle to Node C (IONe)
    const char* pingMsg = "{\"temp\":21.8,\"press\":1012.3}";
    bool sendOk = bpa.sendLocalData(
        muon::bpa::IpnEndpointId{3, 1},
        (const uint8_t*)pingMsg,
        strlen(pingMsg),
        1,
        3600
    );
    TEST_ASSERT_TRUE(sendOk);

    muon::bpa::BundleMetadata meta;
    TEST_ASSERT_TRUE(bpa.getMetadataTable().getOldest(0, meta));
    ggg::hal::StorageHandle_t h = meta.storageHandle;
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, h);

    bool txOk = cl.transmitBundle(h, 1);
    TEST_ASSERT_TRUE(txOk);

    // 2. Node C (IONe) receives the wire frame
    std::vector<uint8_t> cobsWire(bToC.buffer.begin() + 1, bToC.buffer.end() - 1);
    uint8_t clearWire[512];
    size_t decLen = ione_cobs_decode(cobsWire.data(), cobsWire.size(), clearWire);
    TEST_ASSERT_GREATER_THAN(3, decLen);

    uint16_t crcReceived = (clearWire[decLen - 2] << 8) | clearWire[decLen - 1];
    TEST_ASSERT_EQUAL_HEX16(ione_compute_crc16(clearWire, decLen - 2), crcReceived);

    // 3. Node C (IONe) formats a response acknowledgment bundle (Indefinite Array BPv7)
    const char* ackMsg = "{\"status\":\"ACK\",\"rssi\":-68,\"snr\":9}";
    size_t ackLen = strlen(ackMsg);

    std::vector<uint8_t> ioneAckBundle;
    ioneAckBundle.push_back(0x9F); // Indefinite Array
    // Primary Block
    ioneAckBundle.push_back(0x88);
    ioneAckBundle.push_back(0x07); // v7
    ioneAckBundle.push_back(0x00);
    ioneAckBundle.push_back(0x00);
    // Dst: ipn:1.10 (OLED Display / App endpoint) -> [2, [1, 10]]
    ioneAckBundle.push_back(0x82); ioneAckBundle.push_back(0x02); ioneAckBundle.push_back(0x82); ioneAckBundle.push_back(0x01); ioneAckBundle.push_back(0x0A);
    // Src: ipn:3.1 -> [2, [3, 1]]
    ioneAckBundle.push_back(0x82); ioneAckBundle.push_back(0x02); ioneAckBundle.push_back(0x82); ioneAckBundle.push_back(0x03); ioneAckBundle.push_back(0x01);
    // Rpt: ipn:3.1 -> [2, [3, 1]]
    ioneAckBundle.push_back(0x82); ioneAckBundle.push_back(0x02); ioneAckBundle.push_back(0x82); ioneAckBundle.push_back(0x03); ioneAckBundle.push_back(0x01);
    // Timestamp [100001, 1]
    ioneAckBundle.push_back(0x82); ioneAckBundle.push_back(0x1A);
    ioneAckBundle.push_back(0x00); ioneAckBundle.push_back(0x01); ioneAckBundle.push_back(0x86); ioneAckBundle.push_back(0xA1);
    ioneAckBundle.push_back(0x01);
    // Lifetime: 3600
    ioneAckBundle.push_back(0x19); ioneAckBundle.push_back(0x0E); ioneAckBundle.push_back(0x10);

    // Payload Block
    ioneAckBundle.push_back(0x85);
    ioneAckBundle.push_back(0x01); // Type 1
    ioneAckBundle.push_back(0x01); // Nbr 1
    ioneAckBundle.push_back(0x00);
    ioneAckBundle.push_back(0x00);
    if (ackLen < 24) {
        ioneAckBundle.push_back((uint8_t)(0x40 | ackLen));
    } else {
        ioneAckBundle.push_back(0x58);
        ioneAckBundle.push_back((uint8_t)ackLen);
    }
    ioneAckBundle.insert(ioneAckBundle.end(), ackMsg, ackMsg + ackLen);
    ioneAckBundle.push_back(0xFF); // Break

    // Frame into UARTCL wire frame
    std::vector<uint8_t> clearAck;
    clearAck.push_back(ION_UARTCOBS_VERSION_1 | ION_UARTCOBS_FLAG_DATA);
    clearAck.insert(clearAck.end(), ioneAckBundle.begin(), ioneAckBundle.end());
    uint16_t ackCrc = ione_compute_crc16(clearAck.data(), clearAck.size());
    clearAck.push_back((ackCrc >> 8) & 0xFF);
    clearAck.push_back(ackCrc & 0xFF);

    std::vector<uint8_t> cobsAck(clearAck.size() + (clearAck.size() / 254) + 2);
    size_t cobsAckLen = ione_cobs_encode(clearAck.data(), clearAck.size(), cobsAck.data());

    // 4. Node C sends to Node B (muON)
    cToB.write(0x00);
    cToB.write(cobsAck.data(), cobsAckLen);
    cToB.write(0x00);

    g_interopListener.reset();
    cl.tick();
    dispatchAllEvents();

    TEST_ASSERT_TRUE(g_interopListener.rxEventFired);
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, g_interopListener.rxHandle);
    ggg::hal::StorageHandle_t deliveredHandle = g_interopListener.rxHandle;

    // Verify delivered payload matches the acknowledgment message
    muon::bpa::StorageInputStream ackIn(storage, deliveredHandle, 0);
    muon::bpa::BundleHeader ackHdr;
    size_t ackPlLen = 0;
    bool parsed = muon::bpa::CBORSerializer::deserializeBundleHeader(ackIn, ackHdr, ackPlLen);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT32(10, ackHdr.destination.serviceNbr);
    TEST_ASSERT_EQUAL_UINT(ackLen, ackPlLen);

    std::vector<uint8_t> finalPayload(ackPlLen + 1, 0);
    ackIn.readBytes(finalPayload.data(), ackPlLen);
    TEST_ASSERT_EQUAL_STRING(ackMsg, (char*)finalPayload.data());
}

// ============================================================================
// Main Unity Test Runner
// ============================================================================
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_crc16_full_table_and_edge_cases);
    RUN_TEST(test_cobs_cross_codec_symmetry);
    RUN_TEST(test_timesync_handshake_flow);
    RUN_TEST(test_bundle_ingress_ione_indefinite_array_to_muon);
    RUN_TEST(test_bundle_egress_muon_to_ione);
    RUN_TEST(test_crc_corruption_and_rollback);
    RUN_TEST(test_parse_uart_cobs_spec_robustness);
    RUN_TEST(test_roundtrip_ping_pong_simulation);

    return UNITY_END();
}
