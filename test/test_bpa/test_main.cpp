/**
 * @file test_main.cpp
 * @brief Unit tests for Bundle Protocol Agent, CBOR serializer, and storage streams.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <unity.h>
#include <stdint.h>
#include <string.h>

#include <ggg/system/SystemBus.h>
#include <ggg/hal/IStorage.h>
#include <ggg/hal/RamStorage.h>

#include <muon/bpa/BundleTypes.h>
#include <muon/bpa/MuonEvents.h>
#include <muon/bpa/StorageStream.h>
#include <muon/bpa/CBORSerializer.h>
#include <muon/bpa/BundleMetadataTable.h>
#include <muon/bpa/BundleAgent.h>
#include <muon/bpa/IRoutingEngine.h>
#include <muon/bpa/ITimeProvider.h>

using namespace muon::bpa;
using namespace muon::events;

// ============================================================================
// Test Mock Implementations
// ============================================================================

class MockTimeProvider : public ITimeProvider {
public:
    uint32_t currentDtnTime = 1000;
    uint32_t getDtnTimestamp() const override {
        return currentDtnTime;
    }
};

class MockEventListener : public ggg::system::IEventListener {
public:
    uint16_t lastEventType = 0;
    uint32_t lastPayloadU32 = 0;
    uint32_t lastPayloadU32_1 = 0;
    uint8_t lastPriority = 0;
    size_t eventCount = 0;

    void onEvent(const ggg::system::SystemEvent& event) override {
        lastEventType = event.type;
        lastPayloadU32 = event.payload.u32[0];
        lastPayloadU32_1 = event.payload.u32[1];
        lastPriority = event.priority;
        eventCount++;
    }

    void reset() {
        lastEventType = 0;
        lastPayloadU32 = 0;
        lastPayloadU32_1 = 0;
        lastPriority = 0;
        eventCount = 0;
    }
};

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// 1. CBOR Primitive Encoding & Decoding Tests
// ============================================================================

void test_cbor_integer_encoding_decoding(void) {
    uint8_t buffer[64];
    uint64_t testValues[] = {
        0, 1, 23,
        24, 100, 255,
        256, 1000, 65535,
        65536, 1000000, 0xFFFFFFFFULL,
        0x100000000ULL, 0x123456789ABCDEF0ULL
    };
    size_t numValues = sizeof(testValues) / sizeof(testValues[0]);

    for (size_t i = 0; i < numValues; ++i) {
        MemoryOutputStream outStream(buffer, sizeof(buffer));
        TEST_ASSERT_TRUE(CBORSerializer::encodeUnsignedInteger(outStream, testValues[i]));

        size_t written = outStream.getBytesWritten();
        TEST_ASSERT_TRUE(written > 0);

        MemoryInputStream inStream(buffer, written);
        uint64_t decodedValue = 0;
        TEST_ASSERT_TRUE(CBORSerializer::decodeUnsignedInteger(inStream, decodedValue));
        TEST_ASSERT_EQUAL_UINT64(testValues[i], decodedValue);
        TEST_ASSERT_EQUAL_size_t(0, inStream.available());
    }

    // Truncated stream decoding error
    uint8_t truncBuf[2] = { 0x19, 0x01 }; // 0x19 implies 2-byte uint, but only 1 byte follows
    MemoryInputStream truncStream(truncBuf, 2);
    uint64_t dummy = 0;
    TEST_ASSERT_FALSE(CBORSerializer::decodeUnsignedInteger(truncStream, dummy));
}

void test_cbor_headers_and_skipping(void) {
    uint8_t buffer[128];
    MemoryOutputStream outStream(buffer, sizeof(buffer));

    // Encode an array of 3 items
    TEST_ASSERT_TRUE(CBORSerializer::encodeArrayHeader(outStream, 3));
    TEST_ASSERT_TRUE(CBORSerializer::encodeUnsignedInteger(outStream, 42));
    const uint8_t sampleStr[] = "HELLO";
    TEST_ASSERT_TRUE(CBORSerializer::encodeByteString(outStream, sampleStr, 5));
    TEST_ASSERT_TRUE(CBORSerializer::encodeUnsignedInteger(outStream, 999));

    size_t written = outStream.getBytesWritten();
    MemoryInputStream inStream(buffer, written);

    // Decode array header
    size_t numElems = 0;
    bool indef = false;
    TEST_ASSERT_TRUE(CBORSerializer::decodeArrayHeader(inStream, numElems, indef));
    TEST_ASSERT_EQUAL_size_t(3, numElems);
    TEST_ASSERT_FALSE(indef);

    // Skip item 1 (42)
    TEST_ASSERT_TRUE(CBORSerializer::skipCborItem(inStream));

    // Decode item 2 (byte string header)
    size_t bstrLen = 0;
    TEST_ASSERT_TRUE(CBORSerializer::decodeByteStringHeader(inStream, bstrLen, indef));
    TEST_ASSERT_EQUAL_size_t(5, bstrLen);
    uint8_t readStr[6] = {0};
    TEST_ASSERT_EQUAL_size_t(5, inStream.readBytes(readStr, 5));
    TEST_ASSERT_EQUAL_STRING_LEN("HELLO", readStr, 5);

    // Decode item 3 (999)
    uint64_t val = 0;
    TEST_ASSERT_TRUE(CBORSerializer::decodeUnsignedInteger(inStream, val));
    TEST_ASSERT_EQUAL_UINT64(999, val);
}

void test_cbor_eid_and_timestamp(void) {
    uint8_t buffer[64];
    MemoryOutputStream outStream(buffer, sizeof(buffer));

    IpnEndpointId origEid = { 100, 20 };
    TEST_ASSERT_TRUE(CBORSerializer::encodeEID(outStream, origEid));
    TEST_ASSERT_TRUE(CBORSerializer::encodeCreationTimestamp(outStream, 50000, 12));

    MemoryInputStream inStream(buffer, outStream.getBytesWritten());

    IpnEndpointId decEid = {0, 0};
    TEST_ASSERT_TRUE(CBORSerializer::decodeEID(inStream, decEid));
    TEST_ASSERT_TRUE(origEid == decEid);

    uint64_t decTime = 0, decSeq = 0;
    TEST_ASSERT_TRUE(CBORSerializer::decodeCreationTimestamp(inStream, decTime, decSeq));
    TEST_ASSERT_EQUAL_UINT64(50000, decTime);
    TEST_ASSERT_EQUAL_UINT64(12, decSeq);
}

// ============================================================================
// 2. StorageStream Adapters on RamStorage Tests
// ============================================================================

void test_storage_stream_roundtrip(void) {
    ggg::hal::RamStorage storage;

    // Write sequential data pattern
    StorageOutputStream outStream(storage, 100);
    TEST_ASSERT_TRUE(outStream.isValid());

    for (uint8_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_size_t(1, outStream.write(i));
    }
    ggg::hal::StorageHandle_t handle = outStream.commit();
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, handle);
    TEST_ASSERT_EQUAL_size_t(100, storage.getSize(handle));

    // Read back via StorageInputStream
    StorageInputStream inStream(storage, handle);
    TEST_ASSERT_EQUAL_size_t(100, inStream.available());

    // Read first 20 bytes one by one
    for (uint8_t i = 0; i < 20; ++i) {
        int b = inStream.read();
        TEST_ASSERT_EQUAL_INT(i, b);
    }

    // Skip 30 bytes (20 to 49 skipped)
    TEST_ASSERT_EQUAL_size_t(30, inStream.skip(30));

    // Read next 50 bytes in batch
    uint8_t batch[50];
    TEST_ASSERT_EQUAL_size_t(50, inStream.readBytes(batch, 50));
    for (size_t i = 0; i < 50; ++i) {
        TEST_ASSERT_EQUAL_UINT8(50 + i, batch[i]);
    }

    // Stream should now be EOF
    TEST_ASSERT_EQUAL_size_t(0, inStream.available());
    TEST_ASSERT_EQUAL_INT(-1, inStream.read());

    TEST_ASSERT_TRUE(storage.deleteRecord(handle));
}

// ============================================================================
// 3. BPv7 Bundle Serialization & Deserialization Stream Tests
// ============================================================================

void test_bpv7_bundle_stream_serialization(void) {
    ggg::hal::RamStorage storage;

    BundleHeader origHeader;
    origHeader.version = 7;
    origHeader.controlFlags = 0;
    origHeader.setPriority(static_cast<uint8_t>(BundlePriority::EXPEDITED));
    origHeader.destination = { 42, 1 };
    origHeader.source = { 1, 1 };
    origHeader.reportTo = { 1, 1 };
    origHeader.creationTimestamp = 1234567;
    origHeader.sequenceNumber = 89;
    origHeader.lifetime = 3600;

    const char* payloadStr = "Zero-Copy DTN Payload";
    size_t payloadLen = strlen(payloadStr);

    // Serialize directly into RamStorage via StorageOutputStream
    StorageOutputStream outStream(storage, payloadLen + 64);
    TEST_ASSERT_TRUE(CBORSerializer::serializeBundle(origHeader, reinterpret_cast<const uint8_t*>(payloadStr), payloadLen, outStream));
    ggg::hal::StorageHandle_t bundleHandle = outStream.commit();
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, bundleHandle);

    // Stream-deserialize header from storage (Zero-Malloc)
    StorageInputStream inStream(storage, bundleHandle);
    BundleHeader decHeader;
    size_t decPayloadLen = 0;

    TEST_ASSERT_TRUE(CBORSerializer::deserializeBundleHeader(inStream, decHeader, decPayloadLen));

    // Verify Primary Block fields
    TEST_ASSERT_EQUAL_UINT8(7, decHeader.version);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BundlePriority::EXPEDITED), decHeader.getPriority());
    TEST_ASSERT_EQUAL_UINT32(42, decHeader.destination.nodeNbr);
    TEST_ASSERT_EQUAL_UINT32(1, decHeader.destination.serviceNbr);
    TEST_ASSERT_EQUAL_UINT32(1, decHeader.source.nodeNbr);
    TEST_ASSERT_EQUAL_UINT32(1, decHeader.source.serviceNbr);
    TEST_ASSERT_EQUAL_UINT64(1234567, decHeader.creationTimestamp);
    TEST_ASSERT_EQUAL_UINT64(89, decHeader.sequenceNumber);
    TEST_ASSERT_EQUAL_UINT64(3600, decHeader.lifetime);
    TEST_ASSERT_EQUAL_size_t(payloadLen, decPayloadLen);

    // Stream is now positioned exactly at the payload data bytes
    uint8_t readPayload[32] = {0};
    TEST_ASSERT_EQUAL_size_t(payloadLen, inStream.readBytes(readPayload, payloadLen));
    TEST_ASSERT_EQUAL_STRING_LEN(payloadStr, readPayload, payloadLen);

    TEST_ASSERT_TRUE(storage.deleteRecord(bundleHandle));
}

// ============================================================================
// 4. BundleMetadataTable Tests
// ============================================================================

void test_bundle_metadata_table_lifecycle(void) {
    BundleMetadataTable table;
    TEST_ASSERT_EQUAL_size_t(0, table.count());
    TEST_ASSERT_EQUAL_size_t(BundleMetadataTable::MAX_ENTRIES, table.capacity());

    BundleMetadata m1 = { 101, 2, 1, 1500, 1, 0, 0 };
    BundleMetadata m2 = { 102, 3, 1, 1200, 2, 0, 0 };
    BundleMetadata m3 = { 103, 4, 1, 1800, 0, 0, 0 };

    TEST_ASSERT_TRUE(table.add(m1));
    TEST_ASSERT_TRUE(table.add(m2));
    TEST_ASSERT_TRUE(table.add(m3));
    TEST_ASSERT_EQUAL_size_t(3, table.count());

    // Duplicate rejection
    TEST_ASSERT_FALSE(table.add(m1));

    // Find
    BundleMetadata* found = table.find(102);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_UINT32(3, found->destNode);
    TEST_ASSERT_EQUAL_UINT8(2, found->bpPriority);

    // Non-existent
    TEST_ASSERT_NULL(table.find(999));

    // Oldest/highest priority
    BundleMetadata oldest;
    TEST_ASSERT_TRUE(table.getOldest(0, oldest));
    // Priority 2 (m2) should be selected first
    TEST_ASSERT_EQUAL_UINT16(102, oldest.storageHandle);

    // Remove
    TEST_ASSERT_TRUE(table.remove(102));
    TEST_ASSERT_EQUAL_size_t(2, table.count());
    TEST_ASSERT_NULL(table.find(102));

    // Purge expired (current time = 1600: m1 has expiration 1500 -> expired)
    size_t purged = table.purgeExpired(1600, nullptr);
    TEST_ASSERT_EQUAL_size_t(1, purged);
    TEST_ASSERT_EQUAL_size_t(1, table.count());
    TEST_ASSERT_NULL(table.find(101));
    TEST_ASSERT_NOT_NULL(table.find(103)); // m3 expires at 1800 -> retained
}

// ============================================================================
// 5. Bundle Protocol Agent (BPA) End-to-End Tests
// ============================================================================

void test_bundle_agent_send_local_data(void) {
    ggg::system::SystemBus& bus = ggg::system::SystemBus::getInstance();
    bus.reset();

    ggg::hal::RamStorage storage;
    MockTimeProvider timeProvider;
    timeProvider.currentDtnTime = 2000;

    BundleAgent agent(&storage, &timeProvider, nullptr);
    TEST_ASSERT_TRUE(agent.init({ 1, 1 }));

    MockEventListener listener;
    TEST_ASSERT_TRUE(bus.subscribe(&listener));

    const char* message = "BPA Sensor Telemetry";
    size_t msgLen = strlen(message);

    // Application sends data to node 10, service 1
    IpnEndpointId destEid = { 10, 1 };
    TEST_ASSERT_TRUE(agent.sendLocalData(destEid, reinterpret_cast<const uint8_t*>(message), msgLen, 1, 300));

    // Verify metadata was stored in BPA custody
    TEST_ASSERT_EQUAL_size_t(1, agent.getMetadataTable().count());

    // Dispatch SystemBus event
    TEST_ASSERT_EQUAL_size_t(1, bus.getPendingCount());
    TEST_ASSERT_TRUE(bus.dispatchOne());

    // Verify listener received MUON_EVT_ROUTE_REQ with valid storage handle
    TEST_ASSERT_EQUAL_UINT16(MUON_EVT_ROUTE_REQ, listener.lastEventType);
    TEST_ASSERT_NOT_EQUAL(0, listener.lastPayloadU32);

    ggg::hal::StorageHandle_t handle = static_cast<ggg::hal::StorageHandle_t>(listener.lastPayloadU32);
    BundleMetadata* meta = agent.getMetadataTable().find(handle);
    TEST_ASSERT_NOT_NULL(meta);
    TEST_ASSERT_EQUAL_UINT32(10, meta->destNode);
    TEST_ASSERT_EQUAL_UINT32(2300, meta->expirationTime);

    // Verify that the bundle can be stream-read from storage
    StorageInputStream inStream(storage, handle);
    BundleHeader header;
    size_t readPayloadLen = 0;
    TEST_ASSERT_TRUE(CBORSerializer::deserializeBundleHeader(inStream, header, readPayloadLen));
    TEST_ASSERT_EQUAL_UINT32(10, header.destination.nodeNbr);
    TEST_ASSERT_EQUAL_size_t(msgLen, readPayloadLen);
}

void test_bundle_agent_rx_and_local_delivery(void) {
    ggg::system::SystemBus& bus = ggg::system::SystemBus::getInstance();
    bus.reset();

    ggg::hal::RamStorage storage;
    MockTimeProvider timeProvider;
    timeProvider.currentDtnTime = 3000;

    BundleAgent agent(&storage, &timeProvider, nullptr);
    TEST_ASSERT_TRUE(agent.init({ 5, 1 })); // Local node is ipn:5.1

    MockEventListener listener;
    TEST_ASSERT_TRUE(bus.subscribe(&listener));

    // Simulate a Convergence Layer storing a bundle addressed to ipn:5.1
    BundleHeader rxHeader;
    rxHeader.version = 7;
    rxHeader.destination = { 5, 1 };
    rxHeader.source = { 2, 1 };
    rxHeader.reportTo = { 2, 1 };
    rxHeader.creationTimestamp = 2950;
    rxHeader.lifetime = 100; // expires at 3050 (valid at 3000)
    rxHeader.setPriority(1);

    const char* rxData = "COMMAND_ACTUATE";
    size_t rxLen = strlen(rxData);

    StorageOutputStream outStream(storage, rxLen + 64);
    TEST_ASSERT_TRUE(CBORSerializer::serializeBundle(rxHeader, reinterpret_cast<const uint8_t*>(rxData), rxLen, outStream));
    ggg::hal::StorageHandle_t bundleHandle = outStream.commit();

    // Publish MUON_EVT_RX_READY from Convergence Layer
    ggg::system::SystemEvent rxEv = {};
    rxEv.type = MUON_EVT_RX_READY;
    rxEv.payload.u32[0] = bundleHandle;
    TEST_ASSERT_TRUE(bus.publish(rxEv));

    // Dispatch event to BPA
    TEST_ASSERT_TRUE(bus.dispatchOne());

    // Because destination is local node (5.1), BPA should emit MUON_EVT_BUNDLE_DELIVERED
    TEST_ASSERT_EQUAL_size_t(1, bus.getPendingCount());
    listener.reset();
    TEST_ASSERT_TRUE(bus.dispatchOne());

    TEST_ASSERT_EQUAL_UINT16(MUON_EVT_BUNDLE_DELIVERED, listener.lastEventType);
    TEST_ASSERT_EQUAL_UINT32(bundleHandle, listener.lastPayloadU32);
    TEST_ASSERT_EQUAL_UINT32((5 << 16) | 1, listener.lastPayloadU32_1);
    TEST_ASSERT_NOT_NULL(agent.getMetadataTable().find(bundleHandle));

    // Test consuming delivered bundle
    agent.consumeDeliveredBundle(bundleHandle);
    TEST_ASSERT_NULL(agent.getMetadataTable().find(bundleHandle));
    TEST_ASSERT_EQUAL_size_t(0, storage.getSize(bundleHandle));
}

void test_bundle_agent_expired_rx_drop(void) {
    ggg::system::SystemBus& bus = ggg::system::SystemBus::getInstance();
    bus.reset();

    ggg::hal::RamStorage storage;
    MockTimeProvider timeProvider;
    timeProvider.currentDtnTime = 5000; // Current time is 5000

    BundleAgent agent(&storage, &timeProvider, nullptr);
    TEST_ASSERT_TRUE(agent.init({ 1, 1 }));

    MockEventListener listener;
    TEST_ASSERT_TRUE(bus.subscribe(&listener));

    // Create a bundle that expired at 4500 (creation 4000 + lifetime 500)
    BundleHeader rxHeader;
    rxHeader.version = 7;
    rxHeader.destination = { 1, 1 };
    rxHeader.source = { 2, 1 };
    rxHeader.reportTo = { 2, 1 };
    rxHeader.creationTimestamp = 4000;
    rxHeader.lifetime = 500;

    StorageOutputStream outStream(storage, 64);
    TEST_ASSERT_TRUE(CBORSerializer::serializeBundle(rxHeader, (const uint8_t*)"OLD", 3, outStream));
    ggg::hal::StorageHandle_t bundleHandle = outStream.commit();

    // Publish MUON_EVT_RX_READY
    ggg::system::SystemEvent rxEv = {};
    rxEv.type = MUON_EVT_RX_READY;
    rxEv.payload.u32[0] = bundleHandle;
    TEST_ASSERT_TRUE(bus.publish(rxEv));

    TEST_ASSERT_TRUE(bus.dispatchOne());

    // BPA must detect expiration, delete from storage, and emit MUON_EVT_BUNDLE_EXPIRED
    TEST_ASSERT_EQUAL_size_t(0, storage.getSize(bundleHandle)); // Record was purged
    TEST_ASSERT_NULL(agent.getMetadataTable().find(bundleHandle));

    TEST_ASSERT_EQUAL_size_t(1, bus.getPendingCount());
    listener.reset();
    TEST_ASSERT_TRUE(bus.dispatchOne());
    TEST_ASSERT_EQUAL_UINT16(MUON_EVT_BUNDLE_EXPIRED, listener.lastEventType);
}

void test_bundle_agent_tx_success_and_failure(void) {
    ggg::system::SystemBus& bus = ggg::system::SystemBus::getInstance();
    bus.reset();

    ggg::hal::RamStorage storage;
    MockTimeProvider timeProvider;
    BundleAgent agent(&storage, &timeProvider, nullptr);
    TEST_ASSERT_TRUE(agent.init({ 1, 1 }));

    // Send local bundle
    TEST_ASSERT_TRUE(agent.sendLocalData({ 3, 1 }, (const uint8_t*)"DATA", 4, 1, 100));
    bus.dispatchOne(); // dispatch ROUTE_REQ

    BundleMetadata oldest;
    TEST_ASSERT_TRUE(agent.getMetadataTable().getOldest(0, oldest));
    ggg::hal::StorageHandle_t handle = oldest.storageHandle;

    // Simulate TX_FAILURE 1 & 2
    ggg::system::SystemEvent failEv = {};
    failEv.type = MUON_EVT_TX_FAILURE;
    failEv.payload.u32[0] = handle;

    bus.publish(failEv);
    bus.dispatchOne();
    BundleMetadata* m = agent.getMetadataTable().find(handle);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_UINT8(1, m->localRetryCount);

    bus.publish(failEv);
    bus.dispatchOne();
    m = agent.getMetadataTable().find(handle);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_UINT8(2, m->localRetryCount);

    // TX_FAILURE 3 (reaches CONFIG_MUON_BPA_MAX_RETRIES = 3 -> dropped)
    bus.publish(failEv);
    bus.dispatchOne();
    TEST_ASSERT_NULL(agent.getMetadataTable().find(handle));
    TEST_ASSERT_EQUAL_size_t(0, storage.getSize(handle)); // Purged from storage

    // Now send a new bundle and test TX_SUCCESS
    TEST_ASSERT_TRUE(agent.sendLocalData({ 4, 1 }, (const uint8_t*)"OK", 2, 1, 100));
    bus.dispatchOne();
    TEST_ASSERT_TRUE(agent.getMetadataTable().getOldest(0, oldest));
    ggg::hal::StorageHandle_t successHandle = oldest.storageHandle;

    ggg::system::SystemEvent okEv = {};
    okEv.type = MUON_EVT_TX_SUCCESS;
    okEv.payload.u32[0] = successHandle;
    bus.publish(okEv);
    bus.dispatchOne();

    // Storage and metadata should be freed
    TEST_ASSERT_NULL(agent.getMetadataTable().find(successHandle));
    TEST_ASSERT_EQUAL_size_t(0, storage.getSize(successHandle));
}

// ============================================================================
// Main Unity Test Runner
// ============================================================================

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_cbor_integer_encoding_decoding);
    RUN_TEST(test_cbor_headers_and_skipping);
    RUN_TEST(test_cbor_eid_and_timestamp);
    RUN_TEST(test_storage_stream_roundtrip);
    RUN_TEST(test_bpv7_bundle_stream_serialization);
    RUN_TEST(test_bundle_metadata_table_lifecycle);
    RUN_TEST(test_bundle_agent_send_local_data);
    RUN_TEST(test_bundle_agent_rx_and_local_delivery);
    RUN_TEST(test_bundle_agent_expired_rx_drop);
    RUN_TEST(test_bundle_agent_tx_success_and_failure);

    return UNITY_END();
}
