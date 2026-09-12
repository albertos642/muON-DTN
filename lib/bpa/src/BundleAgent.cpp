/**
 * @file BundleAgent.cpp
 * @brief Implementation of the Bundle Protocol Agent state machine and event dispatcher.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <muon/bpa/BundleAgent.h>
#include <muon/common/Logger.h>

namespace muon {
namespace bpa {

BundleAgent::BundleAgent(ggg::hal::IStorage* storage,
                         ITimeProvider* timeProvider,
                         routing::IRoutingEngine* router)
    : _storage(storage),
      _timeProvider(timeProvider),
      _router(router),
      _localEid({0, 0}),
      _currentSequenceNumber(0)
{
}

bool BundleAgent::init(const IpnEndpointId& localEid) {
    _localEid = localEid;
    if (_router != nullptr) {
        _router->setLocalEndpoint(_localEid);
    }
    return ggg::system::SystemBus::getInstance().subscribe(this);
}

void BundleAgent::onEvent(const ggg::system::SystemEvent& event) {
    switch (event.type) {
        case muon::events::MUON_EVT_RX_READY: {
            ggg::hal::StorageHandle_t bundleHandle = static_cast<ggg::hal::StorageHandle_t>(event.payload.u32[0]);
            handleRxReady(bundleHandle);
            break;
        }

        case muon::events::MUON_EVT_TX_SUCCESS: {
            ggg::hal::StorageHandle_t sessionHandle = static_cast<ggg::hal::StorageHandle_t>(event.payload.u32[0]);
            handleTxSuccess(sessionHandle);
            break;
        }

        case muon::events::MUON_EVT_TX_FAILURE: {
            ggg::hal::StorageHandle_t sessionHandle = static_cast<ggg::hal::StorageHandle_t>(event.payload.u32[0]);
            handleTxFailure(sessionHandle);
            break;
        }

        default:
            break;
    }
}

bool BundleAgent::sendLocalData(const IpnEndpointId& destination,
                                const uint8_t* payloadData,
                                size_t length,
                                uint8_t priority,
                                uint32_t lifetimeSec)
{
    MUON_LOG_STR("[BPA] sendLocalData: Dest=ipn:");
    MUON_LOG_U32(destination.nodeNbr);
    MUON_LOG_STR(".");
    MUON_LOG_U32(destination.serviceNbr);
    MUON_LOG_STR(", PayloadLen=");
    MUON_LOG_U32(length);
    MUON_LOG_STR(" B, Priority=");
    MUON_LOG_U32(priority);
    MUON_LOG_STR(", Lifetime=");
    MUON_LOG_U32(lifetimeSec);
    MUON_LOG_LN("s");

    if (!_storage || !_timeProvider) {
        MUON_LOG_LN("[BPA] ERROR: Storage or TimeProvider null!");
        return false;
    }
    if (_metaTable.isFull()) {
        MUON_LOG_LN("[BPA] ERROR: Metadata table full!");
        return false; // Storage metadata pool saturated
    }

    uint32_t now = _timeProvider->getDtnTimestamp();

    BundleHeader header;
    header.version = 7;
    header.controlFlags = 0;
    header.setPriority(priority);
    header.destination = destination;
    header.source = _localEid;
    header.reportTo = _localEid;
    header.creationTimestamp = now;
    header.sequenceNumber = _currentSequenceNumber++;
    header.lifetime = lifetimeSec;

    // Estimate total serialized bundle size: header (~48 bytes) + payload bytes
    size_t expectedSize = length + 64;
    StorageOutputStream outStream(*_storage, expectedSize);
    if (!outStream.isValid()) {
        MUON_LOG_LN("[BPA] ERROR: Failed to allocate storage output stream!");
        return false;
    }

    if (!CBORSerializer::serializeBundle(header, payloadData, length, outStream)) {
        MUON_LOG_LN("[BPA] ERROR: CBOR serialization failed!");
        outStream.abort();
        return false;
    }

    ggg::hal::StorageHandle_t bundleHandle = outStream.commit();
    if (bundleHandle == GGG_INVALID_HANDLE) {
        MUON_LOG_LN("[BPA] ERROR: Failed to commit bundle to storage!");
        return false;
    }

    // Register metadata in O(1) static custody table
    BundleMetadata meta;
    meta.storageHandle = bundleHandle;
    meta.destNode = destination.nodeNbr;
    meta.destService = destination.serviceNbr;
    meta.expirationTime = now + lifetimeSec;
    meta.bpPriority = priority;
    meta.localRetryCount = 0;
    meta.localDynamicPriority = 0;

    if (!_metaTable.add(meta)) {
        MUON_LOG_LN("[BPA] ERROR: Failed to add metadata entry!");
        _storage->deleteRecord(bundleHandle);
        return false;
    }

    MUON_LOG_STR("[BPA] Bundle stored (Handle: ");
    MUON_LOG_U32(bundleHandle);
    MUON_LOG_STR(", Size: ");
    MUON_LOG_U32(_storage->getSize(bundleHandle));
    MUON_LOG_STR(" B). Custody registered (Active: ");
    MUON_LOG_U32(_metaTable.count());
    MUON_LOG_LN(")");

    // Notify SystemBus that a bundle is ready for routing and transmission
    ggg::system::SystemEvent ev = {};
    ev.type = muon::events::MUON_EVT_ROUTE_REQ;
    ev.source = 0x01; // BPA source ID
    ev.priority = priority;
    ev.payload.u32[0] = bundleHandle;

    MUON_LOG_STR("[BPA] Publishing MUON_EVT_ROUTE_REQ for Handle ");
    MUON_LOG_U32(bundleHandle);
    MUON_LOG_LN("");

    ggg::system::SystemBus::getInstance().publish(ev);
    return true;
}

bool BundleAgent::sendLocalDataFromStorage(const IpnEndpointId& destination,
                                          ggg::hal::StorageHandle_t payloadHandle,
                                          uint8_t priority,
                                          uint32_t lifetimeSec)
{
    if (!_storage || !_timeProvider || payloadHandle == GGG_INVALID_HANDLE) {
        return false;
    }
    if (_metaTable.isFull()) {
        return false;
    }

    size_t payloadSize = _storage->getSize(payloadHandle);
    uint32_t now = _timeProvider->getDtnTimestamp();

    BundleHeader header;
    header.version = 7;
    header.controlFlags = 0;
    header.setPriority(priority);
    header.destination = destination;
    header.source = _localEid;
    header.reportTo = _localEid;
    header.creationTimestamp = now;
    header.sequenceNumber = _currentSequenceNumber++;
    header.lifetime = lifetimeSec;

    StorageOutputStream outStream(*_storage, payloadSize + 64);
    if (!outStream.isValid()) {
        return false;
    }

    if (!CBORSerializer::serializeBundleFromStorage(header, payloadHandle, *_storage, outStream)) {
        outStream.abort();
        return false;
    }

    ggg::hal::StorageHandle_t bundleHandle = outStream.commit();
    if (bundleHandle == GGG_INVALID_HANDLE) {
        return false;
    }

    BundleMetadata meta;
    meta.storageHandle = bundleHandle;
    meta.destNode = destination.nodeNbr;
    meta.destService = destination.serviceNbr;
    meta.expirationTime = now + lifetimeSec;
    meta.bpPriority = priority;
    meta.localRetryCount = 0;
    meta.localDynamicPriority = 0;

    if (!_metaTable.add(meta)) {
        _storage->deleteRecord(bundleHandle);
        return false;
    }

    ggg::system::SystemEvent ev = {};
    ev.type = muon::events::MUON_EVT_ROUTE_REQ;
    ev.source = 0x01;
    ev.priority = priority;
    ev.payload.u32[0] = bundleHandle;

    ggg::system::SystemBus::getInstance().publish(ev);
    return true;
}

void BundleAgent::handleRxReady(ggg::hal::StorageHandle_t bundleHandle) {
    if (bundleHandle == GGG_INVALID_HANDLE || !_storage || !_timeProvider) {
        return;
    }

    MUON_LOG_STR("[BPA] Handling RX_READY: Handle ");
    MUON_LOG_U32(bundleHandle);
    MUON_LOG_LN("");

    // Stream-parse the CBOR header from storage (Zero-Malloc)
    StorageInputStream inStream(*_storage, bundleHandle);
    BundleHeader header;
    size_t payloadLength = 0;

    if (!CBORSerializer::deserializeBundleHeader(inStream, header, payloadLength)) {
        MUON_LOG_STR("[BPA] ERROR: Corrupt BPv7 bundle header on Handle ");
        MUON_LOG_U32(bundleHandle);
        MUON_LOG_LN("! Deleting from storage.");
        _storage->deleteRecord(bundleHandle);
        return;
    }

    MUON_LOG_STR("[BPA] Ingress bundle: Dest=ipn:");
    MUON_LOG_U32(header.destination.nodeNbr);
    MUON_LOG_STR(".");
    MUON_LOG_U32(header.destination.serviceNbr);
    MUON_LOG_STR(", Src=ipn:");
    MUON_LOG_U32(header.source.nodeNbr);
    MUON_LOG_STR(".");
    MUON_LOG_U32(header.source.serviceNbr);
    MUON_LOG_STR(", PayloadLen=");
    MUON_LOG_U32(payloadLength);
    MUON_LOG_LN(" B");

    // Evaluate delivery or forwarding
    bool deliverLocal = (header.destination == _localEid);
    if (!deliverLocal && _router != nullptr) {
        uint8_t linkId = 0;
        routing::RouteDecision decision = _router->evaluate(header, linkId);
        if (decision == routing::RouteDecision::DELIVER_LOCAL) {
            deliverLocal = true;
        } else if (decision == routing::RouteDecision::DROP) {
            MUON_LOG_STR("[BPA] Router requested DROP for Handle ");
            MUON_LOG_U32(bundleHandle);
            MUON_LOG_LN("");
            _storage->deleteRecord(bundleHandle);
            return;
        }
    }

    // Evaluate timestamp harmonization for forwarded bundles from uncalibrated/skewed IoT nodes
    uint32_t now = _timeProvider->getDtnTimestamp();
    bool isExpired = false;

    if (!deliverLocal) {
        bool hasAuthoritativeClock = (_timeProvider->isAuthoritative() || now >= 1000000UL);
        bool needsHarmonization = false;

        if (hasAuthoritativeClock) {
            if (header.creationTimestamp == 0 || header.creationTimestamp < 1000000UL) {
                needsHarmonization = true;
            } else if (now > (header.creationTimestamp + header.lifetime)) {
                // Sensor node clock skewed or expired relative to authoritative gateway
                needsHarmonization = true;
            }
        }

        if (needsHarmonization) {
            MUON_LOG_STR("[BPA] Harmonizing IoT timestamp for Handle ");
            MUON_LOG_U32(bundleHandle);
            MUON_LOG_STR(" (Old TS: ");
            MUON_LOG_U32(header.creationTimestamp);
            MUON_LOG_STR(" -> New TS: ");
            MUON_LOG_U32(now);
            MUON_LOG_LN(")");

            header.creationTimestamp = now;
            if (header.lifetime < 60) {
                header.lifetime = 3600;
            }

            StorageOutputStream outStream(*_storage, payloadLength + 64);
            if (outStream.isValid()) {
                if (CBORSerializer::reserializeBundleWithNewHeader(header, inStream, payloadLength, outStream)) {
                    ggg::hal::StorageHandle_t newHandle = outStream.commit();
                    if (newHandle != GGG_INVALID_HANDLE) {
                        _storage->deleteRecord(bundleHandle);
                        bundleHandle = newHandle;
                    } else {
                        outStream.abort();
                    }
                } else {
                    outStream.abort();
                }
            }
        } else if (header.creationTimestamp > 0 && header.lifetime > 0) {
            bool bothEpoch = (now >= 1000000UL && header.creationTimestamp >= 1000000UL);
            bool bothUptime = (now < 1000000UL && header.creationTimestamp < 1000000UL);
            if (bothEpoch || bothUptime) {
                if (now > (header.creationTimestamp + header.lifetime)) {
                    isExpired = true;
                }
            }
        }
    } else {
        // Local destination: evaluate expiration standard
        if (header.creationTimestamp > 0 && header.lifetime > 0) {
            bool bothEpoch = (now >= 1000000UL && header.creationTimestamp >= 1000000UL);
            bool bothUptime = (now < 1000000UL && header.creationTimestamp < 1000000UL);
            if (bothEpoch || bothUptime) {
                if (now > (header.creationTimestamp + header.lifetime)) {
                    isExpired = true;
                }
            }
        }
    }

    if (isExpired) {
        MUON_LOG_STR("[BPA] Bundle Handle ");
        MUON_LOG_U32(bundleHandle);
        MUON_LOG_LN(" has EXPIRED! Purging from storage.");
        _storage->deleteRecord(bundleHandle);

        ggg::system::SystemEvent expEv = {};
        expEv.type = muon::events::MUON_EVT_BUNDLE_EXPIRED;
        expEv.source = 0x01;
        expEv.priority = header.getPriority();
        expEv.payload.u32[0] = bundleHandle;
        ggg::system::SystemBus::getInstance().publish(expEv);
        return;
    }

    // Track bundle in metadata custody table
    BundleMetadata meta;
    meta.storageHandle = bundleHandle;
    meta.destNode = header.destination.nodeNbr;
    meta.destService = header.destination.serviceNbr;
    meta.expirationTime = (header.creationTimestamp > 0) ? static_cast<uint32_t>(header.creationTimestamp + header.lifetime) : 0;
    meta.bpPriority = header.getPriority();
    meta.localRetryCount = 0;
    meta.localDynamicPriority = 0;

    _metaTable.add(meta);

    if (deliverLocal) {
        MUON_LOG_STR("[BPA] Delivering Bundle Handle ");
        MUON_LOG_U32(bundleHandle);
        MUON_LOG_STR(" to local endpoint Service ");
        MUON_LOG_U32(header.destination.serviceNbr);
        MUON_LOG_LN("");

        ggg::system::SystemEvent delivEv = {};
        delivEv.type = muon::events::MUON_EVT_BUNDLE_DELIVERED;
        delivEv.source = 0x01;
        delivEv.priority = header.getPriority();
        delivEv.payload.u32[0] = bundleHandle;
        delivEv.payload.u32[1] = (static_cast<uint32_t>(header.destination.nodeNbr) << 16) |
                                 (static_cast<uint32_t>(header.destination.serviceNbr) & 0xFFFF);
        ggg::system::SystemBus::getInstance().publish(delivEv);
    } else {
        MUON_LOG_STR("[BPA] Forwarding Bundle Handle ");
        MUON_LOG_U32(bundleHandle);
        MUON_LOG_LN(" -> Publishing MUON_EVT_ROUTE_REQ");

        ggg::system::SystemEvent routeEv = {};
        routeEv.type = muon::events::MUON_EVT_ROUTE_REQ;
        routeEv.source = 0x01;
        routeEv.priority = header.getPriority();
        routeEv.payload.u32[0] = bundleHandle;
        ggg::system::SystemBus::getInstance().publish(routeEv);
    }
}

void BundleAgent::handleTxSuccess(ggg::hal::StorageHandle_t sessionHandle) {
    if (sessionHandle == GGG_INVALID_HANDLE) {
        return;
    }
    MUON_LOG_STR("[BPA] TX_SUCCESS confirmed for Handle ");
    MUON_LOG_U32(sessionHandle);
    MUON_LOG_LN(". Releasing custody and deleting storage record.");

    // Custody transfer / unacknowledged send completed: free storage and metadata
    _metaTable.remove(sessionHandle);
    if (_storage != nullptr) {
        _storage->deleteRecord(sessionHandle);
    }
}

void BundleAgent::handleTxFailure(ggg::hal::StorageHandle_t sessionHandle) {
    if (sessionHandle == GGG_INVALID_HANDLE) {
        return;
    }
    BundleMetadata* meta = _metaTable.find(sessionHandle);
    if (meta == nullptr) {
        MUON_LOG_STR("[BPA] TX_FAILURE for untracked Handle ");
        MUON_LOG_U32(sessionHandle);
        MUON_LOG_LN("");
        return;
    }

    meta->localRetryCount++;
    MUON_LOG_STR("[BPA] TX_FAILURE for Handle ");
    MUON_LOG_U32(sessionHandle);
    MUON_LOG_STR(". Retry: ");
    MUON_LOG_U32(meta->localRetryCount);
    MUON_LOG_STR("/");
    MUON_LOG_U32(CONFIG_MUON_BPA_MAX_RETRIES);
    MUON_LOG_LN("");

    if (meta->localRetryCount >= CONFIG_MUON_BPA_MAX_RETRIES) {
        MUON_LOG_STR("[BPA] Maximum retries exceeded for Handle ");
        MUON_LOG_U32(sessionHandle);
        MUON_LOG_LN("! Dropping bundle and purging from storage.");
        _metaTable.remove(sessionHandle);
        if (_storage != nullptr) {
            _storage->deleteRecord(sessionHandle);
        }
    } else {
        // Apply congestion backoff offset
        if (meta->localDynamicPriority > -10) {
            meta->localDynamicPriority--;
        }

        MUON_LOG_STR("[BPA] Bundle Handle ");
        MUON_LOG_U32(sessionHandle);
        MUON_LOG_STR(" kept in custody (DynamicPriority=");
        MUON_LOG_I32(meta->localDynamicPriority);
        MUON_LOG_LN(").");
    }
}

void BundleAgent::tick() {
    if (_timeProvider != nullptr && _storage != nullptr) {
        uint32_t now = _timeProvider->getDtnTimestamp();
        _metaTable.purgeExpired(now, _storage);
    }
}

void BundleAgent::consumeDeliveredBundle(ggg::hal::StorageHandle_t handle) {
    if (handle == GGG_INVALID_HANDLE) {
        return;
    }
    _metaTable.remove(handle);
    if (_storage != nullptr) {
        _storage->deleteRecord(handle);
    }
}

} // namespace bpa
} // namespace muon
