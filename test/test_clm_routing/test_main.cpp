/**
 * @file test_main.cpp
 * @brief Unit tests for Convergence Layer Manager and Static Routing Engine.
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

#include <muon/clm/IConvergenceLayer.h>
#include <muon/clm/ConvergenceLayerManager.h>
#include <muon/routing/IRoutingEngine.h>
#include <muon/routing/StaticRoutingEngine.h>

using namespace muon::bpa;
using namespace muon::events;
using namespace muon::clm;
using namespace muon::routing;

// ============================================================================
// Mock Convergence Layer Adapter
// ============================================================================

class MockConvergenceLayer : public IConvergenceLayer {
public:
    uint8_t linkId;
    ggg::hal::StorageHandle_t lastTransmittedHandle;
    uint8_t lastQos;
    size_t transmitCount;
    size_t tickCount;
    bool acceptTx;

    explicit MockConvergenceLayer(uint8_t id, bool accept = true)
        : linkId(id),
          lastTransmittedHandle(GGG_INVALID_HANDLE),
          lastQos(0),
          transmitCount(0),
          tickCount(0),
          acceptTx(accept)
    {
    }

    uint8_t getLinkId() const override {
        return linkId;
    }

    bool transmitBundle(ggg::hal::StorageHandle_t bundleHandle, uint8_t qos) override {
        if (!acceptTx) {
            return false;
        }
        lastTransmittedHandle = bundleHandle;
        lastQos = qos;
        transmitCount++;
        return true;
    }

    void tick() override {
        tickCount++;
    }

    void reset() {
        lastTransmittedHandle = GGG_INVALID_HANDLE;
        lastQos = 0;
        transmitCount = 0;
        tickCount = 0;
    }
};

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// 1. Static Routing Engine Tests
// ============================================================================

void test_static_routing_explicit_and_updates(void) {
    StaticRoutingEngine router;
    router.setLocalEndpoint({ 1, 1 });

    TEST_ASSERT_EQUAL_size_t(0, router.getRouteCount());
    TEST_ASSERT_EQUAL_size_t(StaticRoutingEngine::MAX_ROUTES, router.getCapacity());

    // Add explicit routes
    TEST_ASSERT_TRUE(router.addRoute(10, 1));
    TEST_ASSERT_TRUE(router.addRoute(20, 2));
    TEST_ASSERT_EQUAL_size_t(2, router.getRouteCount());

    uint8_t outLink = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::FORWARD_DIRECT),
                          static_cast<int>(router.evaluateNode(10, outLink)));
    TEST_ASSERT_EQUAL_UINT8(1, outLink);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::FORWARD_DIRECT),
                          static_cast<int>(router.evaluateNode(20, outLink)));
    TEST_ASSERT_EQUAL_UINT8(2, outLink);

    // Update existing route (Node 10 -> Link 3)
    TEST_ASSERT_TRUE(router.addRoute(10, 3));
    TEST_ASSERT_EQUAL_size_t(2, router.getRouteCount()); // Count shouldn't increase
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::FORWARD_DIRECT),
                          static_cast<int>(router.evaluateNode(10, outLink)));
    TEST_ASSERT_EQUAL_UINT8(3, outLink);

    // Remove route
    TEST_ASSERT_TRUE(router.removeRoute(10));
    TEST_ASSERT_EQUAL_size_t(1, router.getRouteCount());

    // Unknown route without default -> STORE_FOR_LATER
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::STORE_FOR_LATER),
                          static_cast<int>(router.evaluateNode(10, outLink)));
}

void test_static_routing_default_fallback(void) {
    StaticRoutingEngine router;
    router.setLocalEndpoint({ 1, 1 });

    router.addRoute(100, 1);

    // No default route initially
    TEST_ASSERT_FALSE(router.hasDefaultRoute());
    uint8_t outLink = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::STORE_FOR_LATER),
                          static_cast<int>(router.evaluateNode(999, outLink)));

    // Set default route -> Link 5
    router.setDefaultRoute(5);
    TEST_ASSERT_TRUE(router.hasDefaultRoute());
    TEST_ASSERT_EQUAL_UINT8(5, router.getDefaultRoute());

    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::FORWARD_DIRECT),
                          static_cast<int>(router.evaluateNode(999, outLink)));
    TEST_ASSERT_EQUAL_UINT8(5, outLink);

    // Explicit route takes precedence over default
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::FORWARD_DIRECT),
                          static_cast<int>(router.evaluateNode(100, outLink)));
    TEST_ASSERT_EQUAL_UINT8(1, outLink);

    // Clear default route
    router.clearDefaultRoute();
    TEST_ASSERT_FALSE(router.hasDefaultRoute());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::STORE_FOR_LATER),
                          static_cast<int>(router.evaluateNode(999, outLink)));
}

void test_static_routing_local_delivery(void) {
    StaticRoutingEngine router;
    router.setLocalEndpoint({ 1, 1 });

    // Header addressed to this node
    BundleHeader localHeader;
    localHeader.destination = { 1, 1 };
    uint8_t outLink = 0;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::DELIVER_LOCAL),
                          static_cast<int>(router.evaluate(localHeader, outLink)));

    // Node 1 evaluateNode directly
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteDecision::DELIVER_LOCAL),
                          static_cast<int>(router.evaluateNode(1, outLink)));
}

// ============================================================================
// 2. Convergence Layer Manager (CLM) Tests
// ============================================================================

void test_clm_adapter_registration(void) {
    ConvergenceLayerManager clm;
    TEST_ASSERT_EQUAL_size_t(0, clm.getAdapterCount());
    TEST_ASSERT_EQUAL_size_t(ConvergenceLayerManager::MAX_ADAPTERS, clm.getCapacity());

    MockConvergenceLayer cla1(1);
    MockConvergenceLayer cla2(2);

    // Register adapter 1 and 2
    TEST_ASSERT_TRUE(clm.registerAdapter(&cla1));
    TEST_ASSERT_TRUE(clm.registerAdapter(&cla2));
    TEST_ASSERT_EQUAL_size_t(2, clm.getAdapterCount());

    TEST_ASSERT_EQUAL_PTR(&cla1, clm.getAdapter(1));
    TEST_ASSERT_EQUAL_PTR(&cla2, clm.getAdapter(2));
    TEST_ASSERT_NULL(clm.getAdapter(3));

    // Reject duplicate link ID
    MockConvergenceLayer duplicateCla(1);
    TEST_ASSERT_FALSE(clm.registerAdapter(&duplicateCla));

    // Unregister
    TEST_ASSERT_TRUE(clm.unregisterAdapter(1));
    TEST_ASSERT_EQUAL_size_t(1, clm.getAdapterCount());
    TEST_ASSERT_NULL(clm.getAdapter(1));
    TEST_ASSERT_EQUAL_PTR(&cla2, clm.getAdapter(2));
}

void test_clm_direct_transmit(void) {
    ConvergenceLayerManager clm;
    MockConvergenceLayer cla(3);
    TEST_ASSERT_TRUE(clm.registerAdapter(&cla));

    ggg::hal::StorageHandle_t testHandle = 0x0123;
    uint8_t testQos = 1;

    // Successful transmit
    TEST_ASSERT_TRUE(clm.transmit(3, testHandle, testQos));
    TEST_ASSERT_EQUAL_size_t(1, cla.transmitCount);
    TEST_ASSERT_EQUAL_UINT16(testHandle, cla.lastTransmittedHandle);
    TEST_ASSERT_EQUAL_UINT8(testQos, cla.lastQos);

    // Transmit on non-existent link
    TEST_ASSERT_FALSE(clm.transmit(99, testHandle, testQos));

    // Transmit invalid handle
    TEST_ASSERT_FALSE(clm.transmit(3, GGG_INVALID_HANDLE, testQos));
}

void test_clm_tick_all(void) {
    ConvergenceLayerManager clm;
    MockConvergenceLayer cla1(1);
    MockConvergenceLayer cla2(2);

    TEST_ASSERT_TRUE(clm.registerAdapter(&cla1));
    TEST_ASSERT_TRUE(clm.registerAdapter(&cla2));

    TEST_ASSERT_EQUAL_size_t(0, cla1.tickCount);
    TEST_ASSERT_EQUAL_size_t(0, cla2.tickCount);

    clm.tickAll();

    TEST_ASSERT_EQUAL_size_t(1, cla1.tickCount);
    TEST_ASSERT_EQUAL_size_t(1, cla2.tickCount);

    clm.tickAll();

    TEST_ASSERT_EQUAL_size_t(2, cla1.tickCount);
    TEST_ASSERT_EQUAL_size_t(2, cla2.tickCount);
}

// ============================================================================
// 3. SystemBus Event-Driven Routing & Transmission Integration Tests
// ============================================================================

void test_clm_route_req_event_routing(void) {
    ggg::system::SystemBus& bus = ggg::system::SystemBus::getInstance();
    bus.reset();

    ggg::hal::RamStorage storage;
    StaticRoutingEngine router;
    router.setLocalEndpoint({ 1, 1 });
    router.addRoute(42, 2); // Destination Node 42 routes to Link 2

    ConvergenceLayerManager clm(&router, &storage);
    TEST_ASSERT_TRUE(clm.init()); // Subscribe to SystemBus

    MockConvergenceLayer cla1(1);
    MockConvergenceLayer cla2(2);
    TEST_ASSERT_TRUE(clm.registerAdapter(&cla1));
    TEST_ASSERT_TRUE(clm.registerAdapter(&cla2));

    // Create a bundle in storage addressed to node 42
    BundleHeader header;
    header.version = 7;
    header.destination = { 42, 1 };
    header.source = { 1, 1 };
    header.reportTo = { 1, 1 };
    header.creationTimestamp = 5000;
    header.lifetime = 600;
    header.setPriority(2); // QoS 2

    const char* payload = "ROUTED_BUNDLE";
    size_t payloadLen = strlen(payload);

    StorageOutputStream outStream(storage, payloadLen + 64);
    TEST_ASSERT_TRUE(CBORSerializer::serializeBundle(header, (const uint8_t*)payload, payloadLen, outStream));
    ggg::hal::StorageHandle_t bundleHandle = outStream.commit();
    TEST_ASSERT_NOT_EQUAL(GGG_INVALID_HANDLE, bundleHandle);

    // Publish MUON_EVT_ROUTE_REQ from BPA
    ggg::system::SystemEvent routeEv = {};
    routeEv.type = MUON_EVT_ROUTE_REQ;
    routeEv.priority = 2;
    routeEv.payload.u32[0] = bundleHandle;
    routeEv.payload.u32[1] = 0; // Trigger routing engine lookup

    TEST_ASSERT_TRUE(bus.publish(routeEv));
    TEST_ASSERT_EQUAL_size_t(1, bus.getPendingCount());

    // Dispatch event to CLM
    TEST_ASSERT_TRUE(bus.dispatchOne());

    // Verify Link 2 received the bundle, while Link 1 did not
    TEST_ASSERT_EQUAL_size_t(0, cla1.transmitCount);
    TEST_ASSERT_EQUAL_size_t(1, cla2.transmitCount);
    TEST_ASSERT_EQUAL_UINT16(bundleHandle, cla2.lastTransmittedHandle);
    TEST_ASSERT_EQUAL_UINT8(2, cla2.lastQos);

    TEST_ASSERT_TRUE(storage.deleteRecord(bundleHandle));
}

void test_clm_route_req_explicit_link(void) {
    ggg::system::SystemBus& bus = ggg::system::SystemBus::getInstance();
    bus.reset();

    ConvergenceLayerManager clm(nullptr, nullptr);
    TEST_ASSERT_TRUE(clm.init());

    MockConvergenceLayer cla1(1);
    MockConvergenceLayer cla2(2);
    TEST_ASSERT_TRUE(clm.registerAdapter(&cla1));
    TEST_ASSERT_TRUE(clm.registerAdapter(&cla2));

    ggg::hal::StorageHandle_t handle = 0xABCD;

    // Publish MUON_EVT_ROUTE_REQ with explicit link ID = 1 in payload.u32[1]
    ggg::system::SystemEvent ev = {};
    ev.type = MUON_EVT_ROUTE_REQ;
    ev.priority = 1;
    ev.payload.u32[0] = handle;
    ev.payload.u32[1] = 1; // Explicit linkId = 1

    TEST_ASSERT_TRUE(bus.publish(ev));
    TEST_ASSERT_TRUE(bus.dispatchOne());

    TEST_ASSERT_EQUAL_size_t(1, cla1.transmitCount);
    TEST_ASSERT_EQUAL_UINT16(handle, cla1.lastTransmittedHandle);
    TEST_ASSERT_EQUAL_size_t(0, cla2.transmitCount);
}

// ============================================================================
// Main Unity Test Runner
// ============================================================================

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_static_routing_explicit_and_updates);
    RUN_TEST(test_static_routing_default_fallback);
    RUN_TEST(test_static_routing_local_delivery);
    RUN_TEST(test_clm_adapter_registration);
    RUN_TEST(test_clm_direct_transmit);
    RUN_TEST(test_clm_tick_all);
    RUN_TEST(test_clm_route_req_event_routing);
    RUN_TEST(test_clm_route_req_explicit_link);

    return UNITY_END();
}
