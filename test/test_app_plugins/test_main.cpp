/**
 * @file test_main.cpp
 * @brief Unit tests for muON Application Plugins: OLED, BME280, and RTC DS3231.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <unity.h>
#include <ggg/system/SystemBus.h>
#include <ggg/hal/RamStorage.h>

#include <muon/plugins/OledDisplayPlugin.h>
#include <muon/plugins/Bme280Plugin.h>
#include <muon/plugins/RtcDs3231Plugin.h>
#include <muon/plugins/LedActuatorPlugin.h>

#include <muon/bpa/BundleAgent.h>
#include <muon/bpa/StorageStream.h>
#include <muon/bpa/CBORSerializer.h>
#include <muon/bpa/MuonEvents.h>
#include <muon/routing/StaticRoutingEngine.h>

#include <cstring>
#include <cstdio>

using namespace ggg::system;
using namespace ggg::hal;
using namespace muon::plugins;
using namespace muon::bpa;
using namespace muon::routing;
using namespace muon::events;

// ============================================================================
// Dummy Time Provider for testing
// ============================================================================
class MockSimpleTime : public ITimeProvider {
private:
    uint32_t _time;
    bool     _isAuth;
public:
    MockSimpleTime(uint32_t t = 1750000000UL, bool auth = true) : _time(t), _isAuth(auth) {}
    uint32_t getDtnTimestamp() const override { return _time; }
    void setTime(uint32_t t) { _time = t; }
    bool isAuthoritative() const override { return _isAuth; }
    void setAuth(bool a) { _isAuth = a; }
};

// Setup / Teardown
void setUp() {
    SystemBus::getInstance().reset();
    SystemBus::getInstance().init();
}

void tearDown() {
    SystemBus::getInstance().reset();
}

// ============================================================================
// 1. OLED Display Plugin Tests
// ============================================================================
void test_oled_plugin_lifecycle_and_events() {
    MockOledRenderer renderer;
    RamStorage storage;
    OledDisplayPlugin plugin(&renderer, &storage, 10, 2);

    TEST_ASSERT_TRUE(plugin.begin());
    TEST_ASSERT_TRUE(renderer.isInitialized());
    TEST_ASSERT_GREATER_THAN(0, renderer.getDrawCount());

    // Initially status is BOOT, after begin forceRedraw sets it
    TEST_ASSERT_NOT_NULL(strstr(renderer.getLine(0), "BOOT"));

    // 1. Send STARTUP
    SystemEvent ev = {};
    ev.type = GGG_EVT_STARTUP;
    ev.source = 1;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    plugin.forceRedraw();
    TEST_ASSERT_NOT_NULL(strstr(renderer.getLine(0), "IDLE"));

    // 2. Send TX_SUCCESS
    ev.type = MUON_EVT_TX_SUCCESS;
    ev.payload.u32[0] = 1;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    plugin.forceRedraw();
    TEST_ASSERT_EQUAL_UINT16(1, plugin.getData().txSuccessCount);
    TEST_ASSERT_NOT_NULL(strstr(renderer.getLine(0), "TX OK"));

    // 3. Send TX_FAILURE
    ev.type = MUON_EVT_TX_FAILURE;
    ev.payload.u32[0] = 1;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    plugin.forceRedraw();
    TEST_ASSERT_EQUAL_UINT16(1, plugin.getData().txFailureCount);
    TEST_ASSERT_NOT_NULL(strstr(renderer.getLine(0), "TX ERR"));

    // 4. Send RX_READY
    ev.type = MUON_EVT_RX_READY;
    ev.payload.u32[0] = 2;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    plugin.forceRedraw();
    TEST_ASSERT_EQUAL_UINT16(1, plugin.getData().rxReadyCount);
    TEST_ASSERT_NOT_NULL(strstr(renderer.getLine(0), "RX OK"));

    // 5. Test RF Telemetry update
    plugin.updateRfTelemetry(-84, 9);
    plugin.forceRedraw();
    TEST_ASSERT_EQUAL_INT16(-84, plugin.getData().lastRssi);
    TEST_ASSERT_EQUAL_INT8(9, plugin.getData().lastSnr);
    TEST_ASSERT_NOT_NULL(strstr(renderer.getLine(2), "RSSI:-84"));
}

void test_oled_plugin_bundle_delivery_service_id() {
    MockOledRenderer renderer;
    RamStorage storage;
    const uint8_t OLED_SERVICE_ID = 10;
    OledDisplayPlugin plugin(&renderer, &storage, OLED_SERVICE_ID, 2);
    plugin.begin();

    // Store a text message payload in storage
    const char* sampleMsg = "Sensor: OK, Battery: 98%";
    StorageHandle_t h = storage.beginWrite();
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, h);
    storage.writeData(h, reinterpret_cast<const uint8_t*>(sampleMsg), strlen(sampleMsg));
    TEST_ASSERT_TRUE(storage.commitWrite(h));

    // Emit MUON_EVT_BUNDLE_DELIVERED to target service 10
    SystemEvent ev = {};
    ev.type = MUON_EVT_BUNDLE_DELIVERED;
    ev.payload.u32[0] = h;
    ev.payload.u32[1] = (1 << 16) | OLED_SERVICE_ID; // destNode=1, destService=10
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    plugin.forceRedraw();
    TEST_ASSERT_TRUE(plugin.getData().hasNewMessage);
    TEST_ASSERT_EQUAL_STRING(sampleMsg, plugin.getData().lastMessage);
    TEST_ASSERT_NOT_NULL(strstr(renderer.getLine(3), sampleMsg));

    // Now emit bundle to a DIFFERENT service ID (e.g. 42) -> OLED should ignore it
    const char* otherMsg = "Not for OLED!";
    StorageHandle_t h2 = storage.beginWrite();
    storage.writeData(h2, reinterpret_cast<const uint8_t*>(otherMsg), strlen(otherMsg));
    storage.commitWrite(h2);

    ev.payload.u32[0] = h2;
    ev.payload.u32[1] = (1 << 16) | 42; // destService=42
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    plugin.forceRedraw();
    // Message remains unchanged
    TEST_ASSERT_EQUAL_STRING(sampleMsg, plugin.getData().lastMessage);
}

void test_oled_plugin_tick_rate_limiting() {
    MockOledRenderer renderer;
    RamStorage storage;
    // Refresh rate = 2 Hz -> min interval = 500 ms
    OledDisplayPlugin plugin(&renderer, &storage, 10, 2);
    plugin.begin();

    size_t initialDraws = renderer.getDrawCount();

    // Trigger an event so dirty flag is set
    SystemEvent ev = {};
    ev.type = GGG_EVT_STARTUP;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    // Calling tick before 500ms should NOT redraw
    plugin.tick(100);
    TEST_ASSERT_EQUAL_UINT(initialDraws, renderer.getDrawCount());

    plugin.tick(300);
    TEST_ASSERT_EQUAL_UINT(initialDraws, renderer.getDrawCount());

    // Calling tick after 500ms should redraw exactly once
    plugin.tick(501);
    TEST_ASSERT_EQUAL_UINT(initialDraws + 1, renderer.getDrawCount());

    // Calling tick immediately again without new changes should NOT redraw
    plugin.tick(502);
    TEST_ASSERT_EQUAL_UINT(initialDraws + 1, renderer.getDrawCount());
}

void test_oled_plugin_cbor_bundle_delivery() {
    MockOledRenderer renderer;
    RamStorage storage;
    const uint8_t OLED_SERVICE_ID = 10;
    MockSimpleTime timeProv(842000000UL, true);
    StaticRoutingEngine router;
    BundleAgent bpa(&storage, &timeProv, &router);
    OledDisplayPlugin plugin(&renderer, &storage, OLED_SERVICE_ID, 2, &timeProv, &bpa);
    plugin.begin();

    // Create a genuine BPv7 bundle containing payload "REBOOT SYSTEM"
    BundleHeader header;
    header.version = 7;
    header.controlFlags = 0;
    header.crcType = 0;
    header.destination = { 1, OLED_SERVICE_ID }; // ipn:1.10
    header.source = { 3, 1 }; // ipn:3.1
    header.reportTo = { 3, 1 };
    header.creationTimestamp = 842000000UL;
    header.sequenceNumber = 1;
    header.lifetime = 3600;

    const char* cmdPayload = "REBOOT SYSTEM";
    StorageOutputStream outStream(storage, 128);
    TEST_ASSERT_TRUE(CBORSerializer::serializeBundle(header, reinterpret_cast<const uint8_t*>(cmdPayload), strlen(cmdPayload), outStream));
    StorageHandle_t h = outStream.commit();
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, h);

    // Deliver to OLED plugin
    SystemEvent ev = {};
    ev.type = MUON_EVT_BUNDLE_DELIVERED;
    ev.payload.u32[0] = h;
    ev.payload.u32[1] = (1 << 16) | OLED_SERVICE_ID;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    plugin.forceRedraw();
    TEST_ASSERT_TRUE(plugin.getData().hasNewMessage);
    TEST_ASSERT_EQUAL_STRING(cmdPayload, plugin.getData().lastMessage);
    TEST_ASSERT_EQUAL_STRING("MSG RCVD", plugin.getData().statusStr);
}

void test_oled_plugin_rtc_display_and_marquee() {
    MockOledRenderer renderer;
    RamStorage storage;
    // 842000000 seconds: 842000000 % 86400 = 32000 -> 08:53:20 UTC
    MockSimpleTime timeProv(842000000UL, true);
    OledDisplayPlugin plugin(&renderer, &storage, 10, 2, &timeProv);
    plugin.begin();

    plugin.tick(1000);
    TEST_ASSERT_TRUE(plugin.getData().isRtcAuthoritative);
    TEST_ASSERT_EQUAL_STRING("08:53:20", plugin.getData().rtcStr);

    // If time provider is not authoritative and time is 0
    timeProv.setAuth(false);
    timeProv.setTime(0);
    plugin.tick(2000);
    TEST_ASSERT_FALSE(plugin.getData().isRtcAuthoritative);
    TEST_ASSERT_EQUAL_STRING("RTC:--", plugin.getData().rtcStr);

    // Test marquee scrolling with a long message (> 124 px, i.e. > 20 chars)
    const char* longMsg = "ALERT: This is a very long sensor alert message for marquee testing!";
    StorageHandle_t h = storage.beginWrite();
    storage.writeData(h, reinterpret_cast<const uint8_t*>(longMsg), strlen(longMsg));
    storage.commitWrite(h);

    SystemEvent ev = {};
    ev.type = MUON_EVT_BUNDLE_DELIVERED;
    ev.payload.u32[0] = h;
    ev.payload.u32[1] = (1 << 16) | 10;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    TEST_ASSERT_EQUAL_UINT16(0, plugin.getData().scrollOffset);

    // Tick forward past marquee interval (80ms)
    plugin.tick(2100);
    TEST_ASSERT_GREATER_THAN(0, plugin.getData().scrollOffset);
}

// ============================================================================
// 2. BME280 Environmental Sensor Plugin Tests
// ============================================================================
void test_bme280_plugin_trigger_and_transmission() {
    MockBme280Driver driver(24.5f, 55.2f, 1013.25f);
    RamStorage storage;
    MockSimpleTime timeProv;
    StaticRoutingEngine router;
    BundleAgent bpa(&storage, &timeProv, &router);

    Bme280Plugin sensor(&driver, &bpa, 0x76, 0x0100, 1, 2, 1, 1, 3600);
    TEST_ASSERT_TRUE(sensor.begin());
    TEST_ASSERT_TRUE(sensor.isInitialized());

    // Publish trigger event with matching code (0x0100, code=1)
    SystemEvent ev = {};
    ev.type = 0x0100; // GGG_EVT_APP_TRIGGER
    ev.payload.u32[0] = 1;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    TEST_ASSERT_EQUAL_UINT(1, driver.getReadCount());
    TEST_ASSERT_EQUAL_UINT(1, sensor.getTransmittedCount());

    // Verify bundle was created in storage
    TEST_ASSERT_GREATER_THAN(0, storage.getCommittedCount());

    // Read the bundle from storage and verify JSON content
    BundleMetadata meta = {};
    TEST_ASSERT_TRUE(bpa.getMetadataTable().getOldest(0, meta));
    StorageHandle_t h = meta.storageHandle;

    StorageInputStream inStream(storage, h);
    BundleHeader header;
    size_t readPayloadLen = 0;
    TEST_ASSERT_TRUE(CBORSerializer::deserializeBundleHeader(inStream, header, readPayloadLen));

    char buf[128];
    size_t toRead = (readPayloadLen < sizeof(buf) - 1) ? readPayloadLen : sizeof(buf) - 1;
    size_t sz = inStream.readBytes(reinterpret_cast<uint8_t*>(buf), toRead);
    buf[sz] = '\0';

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"T\":24.50"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"H\":55.20"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"P\":1013.25"));
}

void test_bme280_plugin_ignore_mismatched_trigger_code() {
    MockBme280Driver driver(20.0f, 40.0f, 1000.0f);
    RamStorage storage;
    MockSimpleTime timeProv;
    StaticRoutingEngine router;
    BundleAgent bpa(&storage, &timeProv, &router);

    // Configured for code=1
    Bme280Plugin sensor(&driver, &bpa, 0x76, 0x0100, 1, 2, 1, 1, 3600);
    sensor.begin();

    // Publish trigger event with DIFFERENT code (code=99)
    SystemEvent ev = {};
    ev.type = 0x0100;
    ev.payload.u32[0] = 99;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    TEST_ASSERT_EQUAL_UINT(0, driver.getReadCount());
    TEST_ASSERT_EQUAL_UINT(0, sensor.getTransmittedCount());
}

void test_bme280_payload_template_formatting() {
    char outBuf[128];

    // 1. Default template
    size_t len1 = Bme280Plugin::formatPayloadWithTemplate("{\"T\":{T},\"H\":{H},\"P\":{P}}", 25.10f, 60.50f, 1012.30f, outBuf, sizeof(outBuf));
    TEST_ASSERT_GREATER_THAN(0, len1);
    TEST_ASSERT_EQUAL_STRING("{\"T\":25.10,\"H\":60.50,\"P\":1012.30}", outBuf);

    // 2. Custom text template with lowercase placeholders
    size_t len2 = Bme280Plugin::formatPayloadWithTemplate("T={t}C H={h}% P={p}hPa", 21.05f, 45.00f, 998.50f, outBuf, sizeof(outBuf));
    TEST_ASSERT_GREATER_THAN(0, len2);
    TEST_ASSERT_EQUAL_STRING("T=21.05C H=45.00% P=998.50hPa", outBuf);

    // 3. Partial / repeated placeholders
    size_t len3 = Bme280Plugin::formatPayloadWithTemplate("TEMP:{T}, AGAIN:{T}", 30.00f, 50.00f, 1000.00f, outBuf, sizeof(outBuf));
    TEST_ASSERT_GREATER_THAN(0, len3);
    TEST_ASSERT_EQUAL_STRING("TEMP:30.00, AGAIN:30.00", outBuf);

    // 4. Runtime plugin setting and transmission test
    MockBme280Driver driver(28.0f, 70.0f, 1015.0f);
    RamStorage storage;
    MockSimpleTime timeProv;
    StaticRoutingEngine router;
    BundleAgent bpa(&storage, &timeProv, &router);

    Bme280Plugin sensor(&driver, &bpa, 0x76, 0x0100, 1, 2, 1, 1, 3600, "DATA: {T}|{H}|{P}");
    sensor.begin();

    SystemEvent ev = {};
    ev.type = 0x0100;
    ev.payload.u32[0] = 1;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    BundleMetadata meta = {};
    TEST_ASSERT_TRUE(bpa.getMetadataTable().getOldest(0, meta));
    StorageInputStream inStream(storage, meta.storageHandle);
    BundleHeader header;
    size_t readPayloadLen = 0;
    TEST_ASSERT_TRUE(CBORSerializer::deserializeBundleHeader(inStream, header, readPayloadLen));

    char receivedPayload[128];
    size_t toRead = (readPayloadLen < sizeof(receivedPayload) - 1) ? readPayloadLen : sizeof(receivedPayload) - 1;
    size_t sz = inStream.readBytes(reinterpret_cast<uint8_t*>(receivedPayload), toRead);
    receivedPayload[sz] = '\0';

    TEST_ASSERT_EQUAL_STRING("DATA: 28.00|70.00|1015.00", receivedPayload);
}

// ============================================================================
// 3. RTC DS3231 Plugin Tests
// ============================================================================
void test_rtc_ds3231_datetime_conversions() {
    // Known timestamp: 2026-09-11 12:30:45 UTC in DTN Epoch (seconds since 2000-01-01)
    uint32_t epoch = RtcDs3231Plugin::dateTimeToEpoch(2026, 9, 11, 12, 30, 45);
    TEST_ASSERT_GREATER_THAN(800000000UL, epoch);

    // Convert back and verify round-trip
    uint16_t y = 0;
    uint8_t m = 0, d = 0, hh = 0, mm = 0, ss = 0;
    RtcDs3231Plugin::epochToDateTime(epoch, y, m, d, hh, mm, ss);

    TEST_ASSERT_EQUAL_UINT16(2026, y);
    TEST_ASSERT_EQUAL_UINT8(9, m);
    TEST_ASSERT_EQUAL_UINT8(11, d);
    TEST_ASSERT_EQUAL_UINT8(12, hh);
    TEST_ASSERT_EQUAL_UINT8(30, mm);
    TEST_ASSERT_EQUAL_UINT8(45, ss);

    // Leap year test: 2024-02-29 23:59:59
    uint32_t leapEpoch = RtcDs3231Plugin::dateTimeToEpoch(2024, 2, 29, 23, 59, 59);
    RtcDs3231Plugin::epochToDateTime(leapEpoch, y, m, d, hh, mm, ss);
    TEST_ASSERT_EQUAL_UINT16(2024, y);
    TEST_ASSERT_EQUAL_UINT8(2, m);
    TEST_ASSERT_EQUAL_UINT8(29, d);
    TEST_ASSERT_EQUAL_UINT8(23, hh);
    TEST_ASSERT_EQUAL_UINT8(59, mm);
    TEST_ASSERT_EQUAL_UINT8(59, ss);
}

void test_rtc_ds3231_initialization_and_time_provider() {
    MockRtcHardwareHal hal(2026, 9, 11, 14, 0, 0);
    RtcDs3231Plugin rtc(&hal, 0x68, true, true, 1);

    TEST_ASSERT_TRUE(rtc.begin());
    TEST_ASSERT_TRUE(rtc.isInitialized());
    TEST_ASSERT_TRUE(rtc.isAuthoritative());

    uint32_t expectedEpoch = RtcDs3231Plugin::dateTimeToEpoch(2026, 9, 11, 14, 0, 0);
    uint32_t curTimestamp = rtc.getDtnTimestamp();

    TEST_ASSERT_EQUAL_UINT32(expectedEpoch, curTimestamp);
}

void test_rtc_ds3231_time_sync_event() {
    MockRtcHardwareHal hal(2026, 1, 1, 0, 0, 0);
    RtcDs3231Plugin rtc(&hal, 0x68, true, true, 1);
    rtc.begin();

    uint32_t authoritativeDtnTime = 1789123456UL;

    // Emit MUON_EVT_TIME_SYNC from UARTCL
    SystemEvent ev = {};
    ev.type = MUON_EVT_TIME_SYNC;
    ev.source = 1; // Link ID
    ev.payload.u32[0] = authoritativeDtnTime;
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    TEST_ASSERT_EQUAL_UINT(1, rtc.getSyncCount());
    TEST_ASSERT_TRUE(rtc.isAuthoritative());
    TEST_ASSERT_EQUAL_UINT32(authoritativeDtnTime, rtc.getDtnTimestamp());
    TEST_ASSERT_EQUAL_UINT(1, hal.getWriteCount());
}

void test_rtc_ds3231_ingress_bundle_sync_fallback() {
    MockRtcHardwareHal hal(2000, 1, 1, 0, 0, 0);
    hal.setOscillatorStopped(true); // Lost power!
    RtcDs3231Plugin rtc(&hal, 0x68, true, true, 1);
    rtc.begin();

    // Starts unauthoritative because oscillator was stopped
    TEST_ASSERT_FALSE(rtc.isAuthoritative());

    // Ingress bundle arrives with higher candidate timestamp
    uint32_t bundleTimestamp = 1789000000UL;
    SystemEvent ev = {};
    ev.type = MUON_EVT_RX_READY;
    ev.payload.u32[0] = 1;
    ev.payload.u32[1] = bundleTimestamp; // Candidate time in payload
    SystemBus::getInstance().publish(ev);
    SystemBus::getInstance().dispatchOne();

    // RTC should adopt bundle timestamp
    TEST_ASSERT_TRUE(rtc.isAuthoritative());
    TEST_ASSERT_EQUAL_UINT32(bundleTimestamp, rtc.getDtnTimestamp());
    TEST_ASSERT_EQUAL_UINT(1, rtc.getSyncCount());
}

// ============================================================================
// 4. LED Actuator Plugin Tests
// ============================================================================
void test_led_actuator_lifecycle() {
    RamStorage storage;
    MockSimpleTime timeProv;
    BundleAgent agent(&storage, &timeProv, nullptr);
    LedActuatorPlugin actuator(13, 2, &agent, &storage);

    TEST_ASSERT_EQUAL_UINT8(13, actuator.getPin());
    TEST_ASSERT_EQUAL_UINT16(2, actuator.getServiceId());
    TEST_ASSERT_FALSE(actuator.getState());
    TEST_ASSERT_EQUAL_UINT32(0, actuator.getToggleCount());

    TEST_ASSERT_TRUE(actuator.begin());

    actuator.toggle();
    TEST_ASSERT_TRUE(actuator.getState());
    TEST_ASSERT_EQUAL_UINT32(1, actuator.getToggleCount());

    actuator.toggle();
    TEST_ASSERT_FALSE(actuator.getState());
    TEST_ASSERT_EQUAL_UINT32(2, actuator.getToggleCount());

    actuator.end();
}

void test_led_actuator_dtn_delivery_and_consumption() {
    RamStorage storage;
    MockSimpleTime timeProv;
    StaticRoutingEngine router;
    BundleAgent agent(&storage, &timeProv, &router);
    agent.init({ 1, 1 }); // Local is node 1

    LedActuatorPlugin actuator(13, 2, &agent, &storage);
    TEST_ASSERT_TRUE(actuator.begin());

    // 1. Create a bundle destined for service 99 (different service)
    BundleHeader hdr1;
    hdr1.version = 7;
    hdr1.destination = { 1, 99 }; // destNode=1, service=99
    hdr1.source = { 3, 1 };
    hdr1.reportTo = { 3, 1 };
    hdr1.creationTimestamp = 1750000000;
    hdr1.lifetime = 3600;

    StorageOutputStream outStream1(storage, 64);
    TEST_ASSERT_TRUE(CBORSerializer::serializeBundle(hdr1, (const uint8_t*)"CMD", 3, outStream1));
    StorageHandle_t h1 = outStream1.commit();

    // Trigger arrival at BPA
    SystemEvent rxEv1 = {};
    rxEv1.type = MUON_EVT_RX_READY;
    rxEv1.payload.u32[0] = h1;
    SystemBus::getInstance().publish(rxEv1);
    SystemBus::getInstance().dispatchOne(); // BPA processes RX_READY and publishes BUNDLE_DELIVERED
    SystemBus::getInstance().dispatchOne(); // Actuator handles BUNDLE_DELIVERED

    // Mismatched service: actuator should NOT toggle, bundle should remain in storage
    TEST_ASSERT_FALSE(actuator.getState());
    TEST_ASSERT_EQUAL_UINT32(0, actuator.getToggleCount());
    TEST_ASSERT_GREATER_THAN(0, storage.getSize(h1));

    // 2. Create a bundle destined for service 2 (LedActuator service)
    BundleHeader hdr2;
    hdr2.version = 7;
    hdr2.destination = { 1, 2 }; // destNode=1, service=2
    hdr2.source = { 3, 1 };
    hdr2.reportTo = { 3, 1 };
    hdr2.creationTimestamp = 1750000000;
    hdr2.lifetime = 3600;

    StorageOutputStream outStream2(storage, 64);
    TEST_ASSERT_TRUE(CBORSerializer::serializeBundle(hdr2, (const uint8_t*)"TOGGLE", 6, outStream2));
    StorageHandle_t h2 = outStream2.commit();

    SystemEvent rxEv2 = {};
    rxEv2.type = MUON_EVT_RX_READY;
    rxEv2.payload.u32[0] = h2;
    SystemBus::getInstance().publish(rxEv2);
    SystemBus::getInstance().dispatchOne(); // BPA publishes BUNDLE_DELIVERED
    SystemBus::getInstance().dispatchOne(); // Actuator receives BUNDLE_DELIVERED

    // Matched service: actuator toggles to TRUE, and consumes bundle (clears from storage and BPA)
    TEST_ASSERT_TRUE(actuator.getState());
    TEST_ASSERT_EQUAL_UINT32(1, actuator.getToggleCount());
    TEST_ASSERT_EQUAL_size_t(0, storage.getSize(h2));
    TEST_ASSERT_NULL(agent.getMetadataTable().find(h2));

    // 3. Second bundle to service 2 toggles state back to FALSE
    StorageOutputStream outStream3(storage, 64);
    TEST_ASSERT_TRUE(CBORSerializer::serializeBundle(hdr2, (const uint8_t*)"TOGGLE", 6, outStream3));
    StorageHandle_t h3 = outStream3.commit();

    SystemEvent rxEv3 = {};
    rxEv3.type = MUON_EVT_RX_READY;
    rxEv3.payload.u32[0] = h3;
    SystemBus::getInstance().publish(rxEv3);
    SystemBus::getInstance().dispatchOne();
    SystemBus::getInstance().dispatchOne();

    TEST_ASSERT_FALSE(actuator.getState());
    TEST_ASSERT_EQUAL_UINT32(2, actuator.getToggleCount());
    TEST_ASSERT_EQUAL_size_t(0, storage.getSize(h3));

    actuator.end();
}

// ============================================================================
// Main Unity Test Runner
// ============================================================================
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // 1. OLED Display Plugin
    RUN_TEST(test_oled_plugin_lifecycle_and_events);
    RUN_TEST(test_oled_plugin_bundle_delivery_service_id);
    RUN_TEST(test_oled_plugin_cbor_bundle_delivery);
    RUN_TEST(test_oled_plugin_rtc_display_and_marquee);
    RUN_TEST(test_oled_plugin_tick_rate_limiting);

    // 2. BME280 Sensor Plugin
    RUN_TEST(test_bme280_plugin_trigger_and_transmission);
    RUN_TEST(test_bme280_plugin_ignore_mismatched_trigger_code);
    RUN_TEST(test_bme280_payload_template_formatting);

    // 3. RTC DS3231 Plugin
    RUN_TEST(test_rtc_ds3231_datetime_conversions);
    RUN_TEST(test_rtc_ds3231_initialization_and_time_provider);
    RUN_TEST(test_rtc_ds3231_time_sync_event);
    RUN_TEST(test_rtc_ds3231_ingress_bundle_sync_fallback);

    // 4. LED Actuator Plugin
    RUN_TEST(test_led_actuator_lifecycle);
    RUN_TEST(test_led_actuator_dtn_delivery_and_consumption);

    return UNITY_END();
}
