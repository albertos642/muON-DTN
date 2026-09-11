/**
 * @file BundleAgent.h
 * @brief Bundle Protocol Agent managing bundle custody, routing, and lifecycle.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_BPA_BUNDLE_AGENT_H
#define MUON_BPA_BUNDLE_AGENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <ggg/system/SystemBus.h>
#include <ggg/system/IEventListener.h>
#include <ggg/hal/IStorage.h>

#include <muon/bpa/BundleTypes.h>
#include <muon/bpa/MuonEvents.h>
#include <muon/bpa/CBORSerializer.h>
#include <muon/bpa/StorageStream.h>
#include <muon/bpa/BundleMetadataTable.h>
#include <muon/bpa/IRoutingEngine.h>
#include <muon/bpa/ITimeProvider.h>

#if defined(__has_include)
#if __has_include("autoconf.h")
#include "autoconf.h"
#endif
#endif

#ifndef CONFIG_MUON_BPA_MAX_RETRIES
#define CONFIG_MUON_BPA_MAX_RETRIES 3
#endif

namespace muon {
namespace bpa {

/**
 * @brief Central DTN Bundle Protocol Agent (BPA) for muON.
 * Manages bundle lifecycle, store-and-forward operations, routing interaction,
 * and custody without whole-bundle RAM buffering (Zero-Malloc stream-to-storage).
 */
class BundleAgent : public ggg::system::IEventListener {
private:
    ggg::hal::IStorage* _storage;
    ITimeProvider* _timeProvider;
    routing::IRoutingEngine* _router;

    IpnEndpointId _localEid;
    uint64_t _currentSequenceNumber;
    BundleMetadataTable _metaTable;

    // Event handlers
    void handleRxReady(ggg::hal::StorageHandle_t bundleHandle);
    void handleTxSuccess(ggg::hal::StorageHandle_t sessionHandle);
    void handleTxFailure(ggg::hal::StorageHandle_t sessionHandle);

public:
    BundleAgent(ggg::hal::IStorage* storage,
                ITimeProvider* timeProvider,
                routing::IRoutingEngine* router = nullptr);

    /**
     * @brief Initializes the BPA and subscribes to the GGG SystemBus.
     * @param localEid Local node and service identifier (IPN scheme).
     * @return true if successfully initialized and registered to SystemBus.
     */
    bool init(const IpnEndpointId& localEid);

    /**
     * @brief SystemBus callback invoked whenever an event is dispatched.
     */
    void onEvent(const ggg::system::SystemEvent& event) override;

    /**
     * @brief API for local application plugins to transmit data.
     * Encodes Primary Block and Payload directly into IStorage without intermediate RAM buffers,
     * registers metadata, and publishes MUON_EVT_ROUTE_REQ on SystemBus.
     * @param destination Target node and service EID.
     * @param payloadData Raw data bytes to transmit.
     * @param length Payload length in bytes.
     * @param priority BP priority (0=Bulk, 1=Normal, 2=Expedited).
     * @param lifetimeSec Bundle validity lifetime in seconds.
     * @return true if bundle was successfully created, stored, and enqueued.
     */
    bool sendLocalData(const IpnEndpointId& destination,
                       const uint8_t* payloadData,
                       size_t length,
                       uint8_t priority,
                       uint32_t lifetimeSec);

    /**
     * @brief API for local application plugins when payload is already present in an IStorage record.
     */
    bool sendLocalDataFromStorage(const IpnEndpointId& destination,
                                  ggg::hal::StorageHandle_t payloadHandle,
                                  uint8_t priority,
                                  uint32_t lifetimeSec);

    /**
     * @brief Periodic maintenance tick (purges expired bundles, checks retries).
     */
    void tick();

    const IpnEndpointId& getLocalEid() const { return _localEid; }
    BundleMetadataTable& getMetadataTable() { return _metaTable; }
    const BundleMetadataTable& getMetadataTable() const { return _metaTable; }
};

} // namespace bpa
} // namespace muon

#endif // MUON_BPA_BUNDLE_AGENT_H
