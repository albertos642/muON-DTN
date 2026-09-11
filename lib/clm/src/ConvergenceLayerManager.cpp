/**
 * @file ConvergenceLayerManager.cpp
 * @brief Implementation of Convergence Layer adapter registration and dispatch.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <muon/clm/ConvergenceLayerManager.h>

namespace muon {
namespace clm {

ConvergenceLayerManager::ConvergenceLayerManager(routing::IRoutingEngine* router,
                                                 ggg::hal::IStorage* storage)
    : _adapterCount(0),
      _router(router),
      _storage(storage)
{
    for (size_t i = 0; i < MAX_ADAPTERS; ++i) {
        _adapters[i] = nullptr;
    }
}

bool ConvergenceLayerManager::init() {
    return ggg::system::SystemBus::getInstance().subscribe(this);
}

bool ConvergenceLayerManager::registerAdapter(IConvergenceLayer* adapter) {
    if (!adapter || _adapterCount >= MAX_ADAPTERS) {
        return false;
    }

    uint8_t linkId = adapter->getLinkId();
    // Check for duplicate Link ID
    if (getAdapter(linkId) != nullptr) {
        return false;
    }

    for (size_t i = 0; i < MAX_ADAPTERS; ++i) {
        if (_adapters[i] == nullptr) {
            _adapters[i] = adapter;
            _adapterCount++;
            return true;
        }
    }
    return false;
}

bool ConvergenceLayerManager::unregisterAdapter(uint8_t linkId) {
    for (size_t i = 0; i < MAX_ADAPTERS; ++i) {
        if (_adapters[i] != nullptr && _adapters[i]->getLinkId() == linkId) {
            _adapters[i] = nullptr;
            _adapterCount--;
            return true;
        }
    }
    return false;
}

IConvergenceLayer* ConvergenceLayerManager::getAdapter(uint8_t linkId) const {
    for (size_t i = 0; i < MAX_ADAPTERS; ++i) {
        if (_adapters[i] != nullptr && _adapters[i]->getLinkId() == linkId) {
            return _adapters[i];
        }
    }
    return nullptr;
}

bool ConvergenceLayerManager::transmit(uint8_t linkId, ggg::hal::StorageHandle_t bundleHandle, uint8_t qos) {
    if (bundleHandle == GGG_INVALID_HANDLE) {
        return false;
    }
    IConvergenceLayer* adapter = getAdapter(linkId);
    if (!adapter) {
        return false;
    }
    return adapter->transmitBundle(bundleHandle, qos);
}

void ConvergenceLayerManager::tickAll() {
    for (size_t i = 0; i < MAX_ADAPTERS; ++i) {
        if (_adapters[i] != nullptr) {
            _adapters[i]->tick();
        }
    }
}

void ConvergenceLayerManager::onEvent(const ggg::system::SystemEvent& event) {
    if (event.type == muon::events::MUON_EVT_ROUTE_REQ) {
        ggg::hal::StorageHandle_t bundleHandle = static_cast<ggg::hal::StorageHandle_t>(event.payload.u32[0]);
        if (bundleHandle == GGG_INVALID_HANDLE) {
            return;
        }

        uint8_t qos = event.priority;

        // Case 1: Explicit target link ID provided in payload.u32[1]
        if (event.payload.u32[1] != 0) {
            uint8_t linkId = static_cast<uint8_t>(event.payload.u32[1] & 0xFF);
            transmit(linkId, bundleHandle, qos);
            return;
        }

        // Case 2: Routing evaluation via IRoutingEngine and IStorage
        if (_router != nullptr && _storage != nullptr) {
            bpa::StorageInputStream inStream(*_storage, bundleHandle);
            bpa::BundleHeader header;
            size_t payloadLength = 0;

            if (bpa::CBORSerializer::deserializeBundleHeader(inStream, header, payloadLength)) {
                uint8_t targetLinkId = 0;
                routing::RouteDecision decision = _router->evaluate(header, targetLinkId);

                if (decision == routing::RouteDecision::FORWARD_DIRECT) {
                    transmit(targetLinkId, bundleHandle, qos);
                }
            }
        }
    }
}

} // namespace clm
} // namespace muon
