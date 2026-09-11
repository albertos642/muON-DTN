/**
 * @file ConvergenceLayerManager.h
 * @brief Convergence Layer Manager coordinating L2.5 link adapters and event routing.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_CLM_CONVERGENCE_LAYER_MANAGER_H
#define MUON_CLM_CONVERGENCE_LAYER_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <ggg/system/SystemBus.h>
#include <ggg/system/IEventListener.h>
#include <ggg/hal/IStorage.h>

#include <muon/clm/IConvergenceLayer.h>
#include <muon/bpa/MuonEvents.h>
#include <muon/bpa/BundleTypes.h>
#include <muon/bpa/StorageStream.h>
#include <muon/bpa/CBORSerializer.h>
#include <muon/routing/IRoutingEngine.h>

#if defined(__has_include)
#if __has_include("autoconf.h")
#include "autoconf.h"
#endif
#endif

#ifndef CONFIG_MUON_CLM_MAX_ADAPTERS
#define CONFIG_MUON_CLM_MAX_ADAPTERS 4
#endif

namespace muon {
namespace clm {

/**
 * @brief Convergence Layer Manager (CLM) orchestrates physical L2.5 Convergence Layer Adapters.
 * Strictly Zero-Malloc: manages adapters in fixed static array and handles transmission
 * using StorageHandle_t identifiers.
 */
class ConvergenceLayerManager : public ggg::system::IEventListener {
public:
    static constexpr size_t MAX_ADAPTERS = CONFIG_MUON_CLM_MAX_ADAPTERS;

private:
    IConvergenceLayer* _adapters[MAX_ADAPTERS];
    size_t _adapterCount;

    routing::IRoutingEngine* _router;
    ggg::hal::IStorage* _storage;

public:
    ConvergenceLayerManager(routing::IRoutingEngine* router = nullptr,
                            ggg::hal::IStorage* storage = nullptr);

    /**
     * @brief Subscribes the CLM to the GGG SystemBus.
     */
    bool init();

    /**
     * @brief Registers a physical Convergence Layer Adapter.
     * @param adapter Pointer to the adapter instance.
     * @return true if registered successfully, false if table full or link ID conflict.
     */
    bool registerAdapter(IConvergenceLayer* adapter);

    /**
     * @brief Unregisters an adapter by its link ID.
     */
    bool unregisterAdapter(uint8_t linkId);

    /**
     * @brief Retrieves the adapter corresponding to a specific link ID.
     */
    IConvergenceLayer* getAdapter(uint8_t linkId) const;

    /**
     * @brief Directly dispatches a bundle handle to a specific physical link.
     * @param linkId Target physical interface ID.
     * @param bundleHandle Storage handle of the serialized bundle.
     * @param qos Service Class (0 = Unreliable, 1 = Notified).
     * @return true if accepted for transmission by the underlying adapter.
     */
    bool transmit(uint8_t linkId, ggg::hal::StorageHandle_t bundleHandle, uint8_t qos = 0);

    /**
     * @brief Calls tick() on all registered convergence layer adapters.
     * Invoked periodically by RTOS task.
     */
    void tickAll();

    /**
     * @brief SystemBus event listener callback.
     * Handles MUON_EVT_ROUTE_REQ by evaluating destination and dispatching to target link.
     */
    void onEvent(const ggg::system::SystemEvent& event) override;

    size_t getAdapterCount() const { return _adapterCount; }
    size_t getCapacity() const { return MAX_ADAPTERS; }

    void setRoutingEngine(routing::IRoutingEngine* router) { _router = router; }
    void setStorage(ggg::hal::IStorage* storage) { _storage = storage; }
};

} // namespace clm
} // namespace muon

#endif // MUON_CLM_CONVERGENCE_LAYER_MANAGER_H
