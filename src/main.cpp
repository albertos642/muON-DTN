/**
 * @file main.cpp
 * @brief Application entry point orchestrating node roles (Node A/Node B) and DTN testbed.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>

#include <ggg/system/SystemBus.h>
#if defined(CONFIG_MUON_STORAGE_BACKEND_SPI_FLASH) || defined(CONFIG_GGG_STORAGE_FLASH_SPI)
#include <ggg/plugins/SpiFlashStorage.h>
#else
#include <ggg/hal/RamStorage.h>
#endif
#include <ggg/hal/IStream.h>

#include <muon/bpa/MuonEvents.h>
#include <muon/bpa/BundleAgent.h>
#include <muon/bpa/BundleMetadataTable.h>
#include <muon/bpa/CBORSerializer.h>
#include <muon/clm/ConvergenceLayerManager.h>
#include <muon/routing/StaticRoutingEngine.h>

#include <muon/lora/LoRaConvergenceLayer.h>
#include <muon/lora/RadioLibLoRaModem.h>
#include <muon/uartcobs/UartCobsConvergenceLayer.h>

#include "autoconf.h"

// ----------------------------------------------------------------------------
// 1. Hardware Stream Adapter for Arduino Serial
// ----------------------------------------------------------------------------
class ArduinoStreamLink : public ggg::hal::IInputStream, public ggg::hal::IOutputStream {
private:
    Stream& _stream;
public:
    explicit ArduinoStreamLink(Stream& stream) : _stream(stream) {}

    size_t write(uint8_t b) override {
        return _stream.write(b);
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        return _stream.write(buffer, size);
    }

    void flush() override {
        _stream.flush();
    }

    size_t available() override {
        return (size_t)_stream.available();
    }

    int read() override {
        return _stream.read();
    }

    void flushRX() override {
        while (_stream.available()) {
            _stream.read();
        }
    }
};

// ----------------------------------------------------------------------------
// 2. Static Memory Infrastructure (Zero-Malloc)
// ----------------------------------------------------------------------------
#if defined(CONFIG_MUON_STORAGE_BACKEND_SPI_FLASH) || defined(CONFIG_GGG_STORAGE_FLASH_SPI)
static ggg::plugins::SpiFlashStorage g_storage;
#else
static ggg::hal::RamStorage g_storage;
#endif
static muon::routing::StaticRoutingEngine g_router;

class ArduinoTimeProvider : public muon::bpa::ITimeProvider {
public:
    uint32_t getDtnTimestamp() const override {
        return (uint32_t)(millis() / 1000);
    }
};

static ArduinoTimeProvider g_timeProvider;
static muon::bpa::BundleAgent g_bundleAgent(&g_storage, &g_timeProvider, &g_router);
static muon::clm::ConvergenceLayerManager g_clm(&g_router, &g_storage);

// Stream adapter for Serial1 (Hardware UART) or Serial (USB CDC)
static ArduinoStreamLink g_uartStream(Serial);
static muon::uartcobs::UartCobsConvergenceLayer g_uartCl(
    CONFIG_MUON_UART_COBS_LINK_ID,
    &g_uartStream,
    &g_uartStream,
    &g_storage,
    true
);

// LoRa Modem (Adafruit Feather M0: CS=8, DIO0=3, RST=4)
static muon::lora::RadioLibLoRaModem g_loraModem(
    CONFIG_MUON_LORA_PIN_CS,
    CONFIG_MUON_LORA_PIN_DIO0,
    CONFIG_MUON_LORA_PIN_RESET
);

static muon::lora::LoRaConfig g_loraConfig = {
    (float)CONFIG_MUON_LORA_FREQ_MHZ,
    (uint8_t)CONFIG_MUON_LORA_SF,
    (float)CONFIG_MUON_LORA_BW_KHZ,
    (uint8_t)CONFIG_MUON_LORA_CR,
    (int8_t)CONFIG_MUON_LORA_TX_POWER_DBM,
    0x12, // Sync Word for private DTN network
    8     // Preamble length
};

static muon::lora::LoRaConvergenceLayer g_loraCl(
    CONFIG_MUON_LORA_LINK_ID,
    &g_loraModem,
    &g_storage,
    g_loraConfig,
    true,
    CONFIG_MUON_LORA_REASSEMBLY_TIMEOUT_MS,
    CONFIG_MUON_LORA_ACK_TIMEOUT_MS
);

// DIO0 ISR
static void loraDio0ISR() {
    g_loraModem.handleInterrupt();
}

static uint32_t getArduinoMillis() {
    return millis();
}

// ----------------------------------------------------------------------------
// 3. Application Listener: Displays DTN status and toggles LED
// ----------------------------------------------------------------------------
class AppEventListener : public ggg::system::IEventListener {
public:
    void onEvent(const ggg::system::SystemEvent& event) override {
        if (event.type == muon::events::MUON_EVT_RX_READY) {
            digitalWrite(LED_BUILTIN, HIGH);
            ggg::hal::StorageHandle_t h = (ggg::hal::StorageHandle_t)event.payload.u32[0];
            size_t sz = g_storage.getSize(h);
            Serial.print(F("[muON] Received Bundle Handle: "));
            Serial.print(h);
            Serial.print(F(" ("));
            Serial.print(sz);
            Serial.println(F(" bytes)"));

            // Read payload and print to Serial
            char buf[64];
            size_t toRead = sz < sizeof(buf) - 1 ? sz : sizeof(buf) - 1;
            g_storage.readData(h, 0, (uint8_t*)buf, toRead);
            buf[toRead] = '\0';
            Serial.print(F("[muON] Payload: "));
            Serial.println(buf);

            delay(50);
            digitalWrite(LED_BUILTIN, LOW);
        } else if (event.type == muon::events::MUON_EVT_TX_SUCCESS) {
            Serial.println(F("[muON] TX Success"));
        } else if (event.type == muon::events::MUON_EVT_TX_FAILURE) {
            Serial.println(F("[muON] TX Failure (NACK/Timeout)"));
        }
    }
};

static AppEventListener g_appListener;

// ----------------------------------------------------------------------------
// 4. FreeRTOS Tasks
// ----------------------------------------------------------------------------
static void SystemBusTask(void *pvParameters) {
    (void)pvParameters;
    ggg::system::SystemBus::getInstance().processEventsTask();
}

static void ClmTickTask(void *pvParameters) {
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10); // 100 Hz polling

    while (true) {
        g_clm.tickAll();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

static void AppTask(void *pvParameters) {
    (void)pvParameters;

#if defined(CONFIG_MUON_NODE_ROLE_A)
    // Node A (Sensor Source): Transmits periodic telemetry bundle every 10 seconds
    uint32_t counter = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        counter++;

        Serial.print(F("[Node A] Generating Telemetry Bundle #"));
        Serial.println(counter);

        digitalWrite(LED_BUILTIN, HIGH);

        char jsonPayload[64];
        snprintf(jsonPayload, sizeof(jsonPayload), "{\"node\":1,\"seq\":%lu,\"status\":\"OK\"}", (unsigned long)counter);

        ggg::hal::StorageHandle_t h = g_storage.beginWrite();
        if (h != GGG_INVALID_HANDLE) {
            g_storage.writeData(h, (const uint8_t*)jsonPayload, strlen(jsonPayload));
            g_storage.commitWrite(h);

            // Forward directly over LoRa (Link 0) with Notified QoS (1)
            g_clm.transmit(CONFIG_MUON_LORA_LINK_ID, h, 1);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(LED_BUILTIN, LOW);
    }
#else
    // Node B (Gateway / Echo): Stays in listen mode
    Serial.println(F("[Node B] Listening for incoming LoRa DTN bundles..."));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}

// ----------------------------------------------------------------------------
// 5. System Setup
// ----------------------------------------------------------------------------
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    Serial.begin(115200);
    // Short wait for serial console if connected
    delay(1000);

    Serial.println(F("=========================================="));
    Serial.println(F(" muON-DTN: Micro Interplanetary Overlay   "));
    Serial.println(F("=========================================="));

    // 1. Initialise SystemBus and Storage
    ggg::system::SystemBus::getInstance().init();
    ggg::system::SystemBus::getInstance().subscribe(&g_appListener);
    g_storage.begin();

    // 2. Initialise Network Routing
    g_router.setLocalEndpoint(muon::bpa::IpnEndpointId{CONFIG_MUON_LOCAL_NODE_ID, 1});
    g_router.addRoute(CONFIG_MUON_REMOTE_NODE_ID, CONFIG_MUON_LORA_LINK_ID);
    g_router.setDefaultRoute(CONFIG_MUON_UART_COBS_LINK_ID);

    // 3. Register Convergence Layers
    g_loraCl.setTimeProvider(getArduinoMillis);
    g_clm.registerAdapter(&g_loraCl);
    g_clm.registerAdapter(&g_uartCl);
    g_clm.init();

    // 4. Initialise Hardware Modem
    pinMode(CONFIG_MUON_LORA_PIN_DIO0, INPUT);
    attachInterrupt(digitalPinToInterrupt(CONFIG_MUON_LORA_PIN_DIO0), loraDio0ISR, RISING);

    if (g_loraCl.begin()) {
        Serial.println(F("[LoRa] SX1276 initialized successfully."));
    } else {
        Serial.println(F("[LoRa] ERROR: Failed to initialize SX1276 modem!"));
    }

    // 5. Create FreeRTOS Tasks
    xTaskCreate(
        SystemBusTask,
        "SysBus",
        CONFIG_GGG_SYSTEM_BUS_TASK_STACK_SIZE,
        nullptr,
        CONFIG_GGG_SYSTEM_BUS_TASK_PRIORITY,
        nullptr
    );

    xTaskCreate(
        ClmTickTask,
        "ClmTick",
        256,
        nullptr,
        2,
        nullptr
    );

    xTaskCreate(
        AppTask,
        "AppTask",
        512,
        nullptr,
        1,
        nullptr
    );

    // 6. Signal system startup
    ggg::system::SystemEvent ev = {};
    ev.type = ggg::system::GGG_EVT_STARTUP;
    ev.source = 0x01;
    ev.priority = 255;
    ggg::system::SystemBus::getInstance().publish(ev);

    Serial.println(F("[RTOS] Starting FreeRTOS Scheduler..."));
    vTaskStartScheduler();
}

void loop() {
    // Empty - FreeRTOS handles execution
}
