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

#if defined(CONFIG_GGG_PLUGIN_BUTTON)
#include <ggg/plugins/ButtonPlugin.h>
#endif

#if defined(CONFIG_MUON_PLUGIN_RTC_DS3231)
#include <muon/plugins/RtcDs3231Plugin.h>
#endif

#if defined(CONFIG_MUON_PLUGIN_OLED_DISPLAY)
#include <muon/plugins/OledDisplayPlugin.h>
#endif

#if defined(CONFIG_MUON_PLUGIN_SENSOR_BME280)
#include <muon/plugins/Bme280Plugin.h>
#endif

#if defined(CONFIG_MUON_PLUGIN_LED_ACTUATOR)
#include <muon/plugins/LedActuatorPlugin.h>
#endif

#if defined(CONFIG_MUON_I2C_SHARED_BUS) && defined(ARDUINO) && !defined(TARGET_NATIVE)
#include <Wire.h>
#endif

#include "autoconf.h"
#include <muon/common/Logger.h>

// ----------------------------------------------------------------------------
// 1. Diagnostic Logging Discipline
// If Node B has UARTCL active on USB Serial, Serial.print is suppressed to protect
// binary COBS frames. Diagnostic logs can optionally route to Serial1.
// ----------------------------------------------------------------------------
#if defined(CONFIG_MUON_DEBUG)
  #if defined(CONFIG_MUON_DEBUG_USE_SERIAL1)
    #define MUON_LOG(x)   Serial1.print(x)
    #define MUON_LOGLN(x) Serial1.println(x)
    static void arduinoLogStr(const char* s) { Serial1.print(s); }
    static void arduinoLogLn(const char* s) { Serial1.println(s); }
    static void arduinoLogU32(uint32_t v) { Serial1.print(v); }
    static void arduinoLogI32(int32_t v) { Serial1.print(v); }
    static void arduinoLogFloat(float v, uint8_t d) { Serial1.print(v, d); }
  #elif defined(CONFIG_MUON_NODE_ROLE_B) && defined(CONFIG_MUON_UART_COBS_ENABLED)
    #define MUON_LOG(x)   do {} while (0)
    #define MUON_LOGLN(x) do {} while (0)
  #else
    #define MUON_LOG(x)   Serial.print(x)
    #define MUON_LOGLN(x) Serial.println(x)
    static void arduinoLogStr(const char* s) { Serial.print(s); }
    static void arduinoLogLn(const char* s) { Serial.println(s); }
    static void arduinoLogU32(uint32_t v) { Serial.print(v); }
    static void arduinoLogI32(int32_t v) { Serial.print(v); }
    static void arduinoLogFloat(float v, uint8_t d) { Serial.print(v, d); }
  #endif
#else
  #define MUON_LOG(x)   do {} while (0)
  #define MUON_LOGLN(x) do {} while (0)
#endif

// ----------------------------------------------------------------------------
// 2. Hardware Stream Adapter for Arduino Serial
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
#if defined(ARDUINO) && !defined(TARGET_NATIVE)
        // On SAMD21 Native USB CDC (Serial), flush() is an unyielding busy-wait (while(head!=tail))
        // that deadlocks FreeRTOS tasks if the host is not actively reading.
        if (&_stream == &Serial) {
            return;
        }
        _stream.flush();
#endif
    }

    size_t available() override {
        return static_cast<size_t>(_stream.available());
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
// 3. Static Memory Infrastructure (Zero-Malloc)
// ----------------------------------------------------------------------------
#if defined(CONFIG_MUON_STORAGE_BACKEND_SPI_FLASH) || defined(CONFIG_GGG_STORAGE_FLASH_SPI)
static ggg::plugins::SpiFlashStorage g_storage;
#else
static ggg::hal::RamStorage g_storage;
#endif
static muon::routing::StaticRoutingEngine g_router;

#if defined(CONFIG_MUON_PLUGIN_RTC_DS3231)
#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
static muon::plugins::I2cRtcHardwareHal g_rtcHal;
#else
static muon::plugins::MockRtcHardwareHal g_rtcHal;
#endif
static muon::plugins::RtcDs3231Plugin g_rtcPlugin(&g_rtcHal);
static muon::bpa::ITimeProvider* g_timeProvider = &g_rtcPlugin;
#else
class ArduinoTimeProvider : public muon::bpa::ITimeProvider {
public:
    uint32_t getDtnTimestamp() const override {
        return static_cast<uint32_t>(millis() / 1000);
    }
};
static ArduinoTimeProvider g_defaultTimeProvider;
static muon::bpa::ITimeProvider* g_timeProvider = &g_defaultTimeProvider;
#endif

static muon::bpa::BundleAgent g_bundleAgent(&g_storage, g_timeProvider, &g_router);
static muon::clm::ConvergenceLayerManager g_clm(&g_router, &g_storage);

#if defined(CONFIG_MUON_PLUGIN_OLED_DISPLAY)
#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
#if defined(CONFIG_MUON_OLED_HAS_RESET_PIN) && (CONFIG_MUON_OLED_PIN_RESET >= 0)
static muon::plugins::U8g2OledRenderer g_oledRenderer(CONFIG_MUON_OLED_I2C_ADDRESS, CONFIG_MUON_OLED_PIN_RESET);
#else
static muon::plugins::U8g2OledRenderer g_oledRenderer(CONFIG_MUON_OLED_I2C_ADDRESS, -1);
#endif
#else
static muon::plugins::MockOledRenderer g_oledRenderer;
#endif
static muon::plugins::OledDisplayPlugin g_oledPlugin(
    &g_oledRenderer, 
    &g_storage, 
    CONFIG_MUON_OLED_APP_SERVICE_ID, 
    CONFIG_MUON_OLED_REFRESH_RATE_HZ
);
#endif

#if defined(CONFIG_MUON_PLUGIN_SENSOR_BME280)
#if defined(ARDUINO) && !defined(TARGET_NATIVE) && !defined(GGG_TARGET_NATIVE)
static muon::plugins::AdafruitBme280Driver g_bmeDriver;
#else
static muon::plugins::MockBme280Driver g_bmeDriver;
#endif
static muon::plugins::Bme280Plugin g_bmePlugin(
    &g_bmeDriver,
    &g_bundleAgent,
    CONFIG_MUON_BME280_I2C_ADDRESS,
    CONFIG_MUON_BME280_TRIGGER_EVENT_ID,
    CONFIG_MUON_BME280_TRIGGER_CODE,
    CONFIG_MUON_BME280_DEST_NODE,
    CONFIG_MUON_BME280_DEST_SERVICE,
    CONFIG_MUON_BME280_BUNDLE_PRIORITY,
    CONFIG_MUON_BME280_BUNDLE_LIFETIME_SEC
);
#endif

#if defined(CONFIG_GGG_PLUGIN_BUTTON)
static ggg::plugins::ButtonConfig g_buttonConfig = {
    .pin = static_cast<uint32_t>(CONFIG_GGG_BUTTON_PIN),
    .pullMode = ggg::plugins::ButtonPullMode::PULL_UP,
    .activeLow = true,
    .activeEdge = ggg::plugins::ButtonActiveEdge::EDGE_FALLING,
    .sampleIntervalMs = 5,
    .debounceThreshold = 6,
    .eventId = CONFIG_GGG_BUTTON_EVENT_ID,
    .eventCode = CONFIG_GGG_BUTTON_EVENT_CODE,
    .moduleId = 0x01
};
static ggg::plugins::ButtonPlugin g_buttonPlugin(g_buttonConfig);
#endif

#if defined(CONFIG_MUON_PLUGIN_LED_ACTUATOR)
static muon::plugins::LedActuatorPlugin g_ledPlugin(
    CONFIG_MUON_ACTUATOR_PIN,
    CONFIG_MUON_ACTUATOR_SERVICE_ID,
    &g_bundleAgent,
    &g_storage
);
#endif

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
    static_cast<float>(CONFIG_MUON_LORA_FREQ_MHZ),
    static_cast<uint8_t>(CONFIG_MUON_LORA_SF),
    static_cast<float>(CONFIG_MUON_LORA_BW_KHZ),
    static_cast<uint8_t>(CONFIG_MUON_LORA_CR),
    static_cast<int8_t>(CONFIG_MUON_LORA_TX_POWER_DBM),
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

// Deferred interrupt flag for thread-safe DIO0 servicing in task context
static volatile bool g_loraInterruptPending = false;

static void loraDio0ISR() {
    g_loraInterruptPending = true;
}

static uint32_t getArduinoMillis() {
    return millis();
}

// ----------------------------------------------------------------------------
// 4. Application Listener: Displays DTN status and updates OLED telemetry
// ----------------------------------------------------------------------------
class AppEventListener : public ggg::system::IEventListener {
public:
    void onEvent(const ggg::system::SystemEvent& event) override {
        if (event.type == 0x0100 || event.type == ggg::system::GGG_EVT_APP_TRIGGER) {
            MUON_LOG(F("[Button] Press event detected (code="));
            MUON_LOG(event.payload.u32[0]);
            MUON_LOGLN(F(")! Sampling BME280 & creating bundle..."));
            digitalWrite(LED_BUILTIN, HIGH);
        } else if (event.type == muon::events::MUON_EVT_ROUTE_REQ) {
            MUON_LOG(F("[BPA] Bundle created & enqueued (Handle: "));
            MUON_LOG(event.payload.u32[0]);
            MUON_LOGLN(F("), routing request dispatched to CLM"));
        } else if (event.type == muon::events::MUON_EVT_RX_READY) {
            MUON_LOG(F("[muON] Received Bundle Handle: "));
            MUON_LOGLN(event.payload.u32[0]);

            // Flash LED_BUILTIN on RX activity
            digitalWrite(LED_BUILTIN, HIGH);

#if defined(CONFIG_MUON_PLUGIN_OLED_DISPLAY)
            g_oledPlugin.updateRfTelemetry(static_cast<int16_t>(g_loraModem.getRSSI()),
                                           static_cast<int8_t>(g_loraModem.getSNR()));
#endif
        } else if (event.type == muon::events::MUON_EVT_TX_SUCCESS) {
            MUON_LOG(F("[muON] TX Success! Bundle "));
            MUON_LOG(event.payload.u32[0]);
            MUON_LOGLN(F(" successfully sent over LoRa."));
            digitalWrite(LED_BUILTIN, LOW);
        } else if (event.type == muon::events::MUON_EVT_TX_FAILURE) {
            MUON_LOG(F("[muON] TX Failure on Bundle "));
            MUON_LOG(event.payload.u32[0]);
            MUON_LOGLN(F(" (NACK or ACK Timeout)."));
            digitalWrite(LED_BUILTIN, LOW);
        }
    }
};

static AppEventListener g_appListener;

// ----------------------------------------------------------------------------
// 5. FreeRTOS Tasks
// ----------------------------------------------------------------------------
static void SystemBusTask(void *pvParameters) {
    (void)pvParameters;
    ggg::system::SystemBus::getInstance().processEventsTask();
}

static void ClmTickTask(void *pvParameters) {
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10); // 100 Hz polling
    uint16_t secondCounter = 0;

    while (true) {
        if (g_loraInterruptPending || digitalRead(CONFIG_MUON_LORA_PIN_DIO0) == HIGH) {
            g_loraInterruptPending = false;
            g_loraModem.handleInterrupt();
        }

#if defined(CONFIG_GGG_PLUGIN_BUTTON)
        g_buttonPlugin.tick();
#endif

        g_clm.tickAll();

#if defined(CONFIG_MUON_PLUGIN_OLED_DISPLAY)
        g_oledPlugin.tick(millis());
#endif

        secondCounter++;
        if (secondCounter >= 100) {
            secondCounter = 0;
            g_bundleAgent.tick();
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

static void AppTask(void *pvParameters) {
    (void)pvParameters;
    uint32_t uptimeSec = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uptimeSec++;

#if defined(CONFIG_MUON_NODE_ROLE_A)
        if (uptimeSec % 5 == 0) {
            MUON_LOG(F("[Node A] Uptime: "));
            MUON_LOG(uptimeSec);
            MUON_LOGLN(F("s | EID ipn:1.1 | Press Button on Pin 5 to sample & send bundle"));
        }
#endif
    }
}

// ----------------------------------------------------------------------------
// 6. System Setup
// ----------------------------------------------------------------------------
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

#if defined(CONFIG_MUON_DEBUG_USE_SERIAL1)
    Serial1.begin(115200);
#endif

    // Initialize USB Serial (non-blocking, battery safe)
    Serial.begin(115200);

#if !defined(CONFIG_MUON_NODE_ROLE_B) || !defined(CONFIG_MUON_UART_COBS_ENABLED)
    // On Native USB CDC (Feather M0), wait up to 3000 ms for Serial Monitor to open.
    // If running on battery without PC, times out after 3s and proceeds safely.
    uint32_t startWait = millis();
    while (!Serial && (millis() - startWait < 3000)) {
        delay(10);
    }
#else
    delay(200);
#endif

#if defined(CONFIG_MUON_DEBUG)
  #if defined(CONFIG_MUON_DEBUG_USE_SERIAL1)
    muon::log::Logger::setSinks(arduinoLogStr, arduinoLogLn, arduinoLogU32, arduinoLogI32, arduinoLogFloat);
  #elif !defined(CONFIG_MUON_NODE_ROLE_B) || !defined(CONFIG_MUON_UART_COBS_ENABLED)
    muon::log::Logger::setSinks(arduinoLogStr, arduinoLogLn, arduinoLogU32, arduinoLogI32, arduinoLogFloat);
  #endif
#endif

    // Fast 3-blink startup sequence to give immediate visual confirmation of boot
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(60);
        digitalWrite(LED_BUILTIN, LOW);
        delay(60);
    }

    MUON_LOGLN(F("=========================================="));
    MUON_LOGLN(F(" muON-DTN: microcontroller Overlay Network"));
    MUON_LOGLN(F("=========================================="));

    // 1. Initialise SystemBus and Storage
    ggg::system::SystemBus::getInstance().init();
    ggg::system::SystemBus::getInstance().subscribe(&g_appListener);
    g_storage.begin();

#if defined(CONFIG_MUON_I2C_SHARED_BUS) && defined(ARDUINO) && !defined(TARGET_NATIVE)
    Wire.begin();
#if defined(CONFIG_MUON_I2C_CLOCK_SPEED)
    Wire.setClock(CONFIG_MUON_I2C_CLOCK_SPEED);
#endif
#endif

#if defined(CONFIG_MUON_PLUGIN_RTC_DS3231)
    if (g_rtcPlugin.begin()) {
        MUON_LOGLN(F("[RTC] DS3231 initialized successfully."));
    } else {
        MUON_LOGLN(F("[RTC] WARNING: DS3231 not detected on I2C bus; using internal timer fallback."));
    }
#endif

#if defined(CONFIG_MUON_PLUGIN_OLED_DISPLAY)
#if defined(CONFIG_MUON_NODE_ROLE_A)
    g_oledPlugin.setRoleString("Node A (1.1)");
#else
    g_oledPlugin.setRoleString("Node B (2.1)");
#endif
    if (g_oledPlugin.begin()) {
        MUON_LOGLN(F("[OLED] Display initialized successfully."));
    }
#endif

#if defined(CONFIG_MUON_PLUGIN_SENSOR_BME280)
    if (g_bmePlugin.begin()) {
        if (g_bmePlugin.isSensorDetected()) {
            MUON_LOGLN(F("[BME280] Sensor detected on I2C bus (forced mode)."));
        } else {
            MUON_LOGLN(F("[BME280] Sensor not responding on I2C (0x76); running in fallback simulation mode."));
        }
    }
#endif

#if defined(CONFIG_GGG_PLUGIN_BUTTON)
    if (g_buttonPlugin.begin()) {
        MUON_LOGLN(F("[Button] Pushbutton plugin active on Pin 5 (active-low pullup)."));
    }
#endif

#if defined(CONFIG_MUON_PLUGIN_LED_ACTUATOR)
    if (g_ledPlugin.begin()) {
        MUON_LOGLN(F("[Actuator] LED Actuator plugin registered (service 2)."));
    }
#endif

    // 2. Initialise Network Routing and BPA
#if defined(CONFIG_MUON_NODE_ROLE_A)
    // Node A (1.1): all traffic for Node 2 or Node 3 routes via LoRaCL
    g_router.setLocalEndpoint(muon::bpa::IpnEndpointId{1, 1});
    g_router.addRoute(2, CONFIG_MUON_LORA_LINK_ID);
    g_router.addRoute(3, CONFIG_MUON_LORA_LINK_ID);
    g_router.setDefaultRoute(CONFIG_MUON_LORA_LINK_ID);
#else
    // Node B (2.1): traffic for Node 1 routes via LoRaCL; traffic for Node 3 (PC) routes via UARTCL
    g_router.setLocalEndpoint(muon::bpa::IpnEndpointId{2, 1});
    g_router.addRoute(1, CONFIG_MUON_LORA_LINK_ID);
    g_router.addRoute(3, CONFIG_MUON_UART_COBS_LINK_ID);
    g_router.setDefaultRoute(CONFIG_MUON_UART_COBS_LINK_ID);
#endif

    g_bundleAgent.init(g_router.getLocalEndpoint());

    // 3. Register Convergence Layers
    g_loraCl.setTimeProvider(getArduinoMillis);
    g_clm.registerAdapter(&g_loraCl);
    g_clm.registerAdapter(&g_uartCl);
    g_clm.init();

    // 4. Initialise Hardware Modem
    pinMode(CONFIG_MUON_LORA_PIN_DIO0, INPUT);
    attachInterrupt(digitalPinToInterrupt(CONFIG_MUON_LORA_PIN_DIO0), loraDio0ISR, RISING);

    if (g_loraCl.begin()) {
        MUON_LOGLN(F("[LoRa] SX1276 initialized successfully."));
    } else {
        MUON_LOGLN(F("[LoRa] ERROR: Failed to initialize SX1276 modem!"));
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
        384,
        nullptr,
        2,
        nullptr
    );

    xTaskCreate(
        AppTask,
        "AppTask",
        256,
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

    MUON_LOGLN(F("[RTOS] Starting FreeRTOS Scheduler..."));
    vTaskStartScheduler();
}

void loop() {
    // Empty - FreeRTOS handles execution
}
