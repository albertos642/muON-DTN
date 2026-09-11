/**
 * @file test_main.cpp
 * @brief Integration tests validating GGG framework bus interoperation with muON-DTN.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <unity.h>
#include <stdint.h>
#include <ggg/system/SystemBus.h>
#include <ggg/hal/IStorage.h>
#include "autoconf.h"

void setUp(void) {}
void tearDown(void) {}

class AppStartupListener : public ggg::system::IEventListener {
public:
    bool startupReceived = false;
    uint8_t receivedSource = 0;
    uint8_t receivedPriority = 0;

    void onEvent(const ggg::system::SystemEvent& event) override {
        if (event.type == ggg::system::GGG_EVT_STARTUP) {
            startupReceived = true;
            receivedSource = event.source;
            receivedPriority = event.priority;
        }
    }
};

void test_muon_ggg_integration(void) {
    ggg::system::SystemBus& bus = ggg::system::SystemBus::getInstance();
    bus.reset();

    AppStartupListener listener;
    TEST_ASSERT_TRUE(bus.subscribe(&listener));

    // Simulate the application startup event publication
    ggg::system::SystemEvent startupEvt = {};
    startupEvt.type = ggg::system::GGG_EVT_STARTUP;
    startupEvt.source = 0x01;
    startupEvt.priority = 255;
    startupEvt.payload.u32[0] = 100;

    TEST_ASSERT_TRUE(bus.publish(startupEvt));
    TEST_ASSERT_EQUAL_size_t(1, bus.getPendingCount());

    // Dispatch the event
    TEST_ASSERT_TRUE(bus.dispatchOne());
    TEST_ASSERT_TRUE(listener.startupReceived);
    TEST_ASSERT_EQUAL_UINT8(0x01, listener.receivedSource);
    TEST_ASSERT_EQUAL_UINT8(255, listener.receivedPriority);
}

void test_kconfig_propagation(void) {
#if defined(CONFIG_GGG_SYSTEM_BUS_QUEUE_SIZE)
    TEST_ASSERT_TRUE(CONFIG_GGG_SYSTEM_BUS_QUEUE_SIZE > 0);
#else
    TEST_FAIL_MESSAGE("CONFIG_GGG_SYSTEM_BUS_QUEUE_SIZE not found in consumer application!");
#endif

#if defined(CONFIG_MUON_APP_VERSION)
    TEST_ASSERT_EQUAL_STRING("0.1.0", CONFIG_MUON_APP_VERSION);
#else
    TEST_FAIL_MESSAGE("CONFIG_MUON_APP_VERSION not found!");
#endif
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_muon_ggg_integration);
    RUN_TEST(test_kconfig_propagation);
    return UNITY_END();
}
