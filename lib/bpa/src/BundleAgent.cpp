/**
 * @file BundleAgent.cpp
 * @brief Implementation of the Bundle Protocol Agent state machine and event dispatcher.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <muon/bpa/BundleAgent.h>

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
    if (!_storage || !_timeProvider) {
        return false;
    }
    if (_metaTable.isFull()) {
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
        return false;
    }

    if (!CBORSerializer::serializeBundle(header, payloadData, length, outStream)) {
        outStream.abort();
        return false;
    }

    ggg::hal::StorageHandle_t bundleHandle = outStream.commit();
    if (bundleHandle == GGG_INVALID_HANDLE) {
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
        _storage->deleteRecord(bundleHandle);
        return false;
    }

    // Notify SystemBus that a bundle is ready for routing and transmission
    ggg::system::SystemEvent ev = {};
    ev.type = muon::events::MUON_EVT_ROUTE_REQ;
    ev.source = 0x01; // BPA source ID
    ev.priority = priority;
    ev.payload.u32[0] = bundleHandle;

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

    // Stream-parse the CBOR header from storage (Zero-Malloc)
    StorageInputStream inStream(*_storage, bundleHandle);
    BundleHeader header;
    size_t payloadLength = 0;

    if (!CBORSerializer::deserializeBundleHeader(inStream, header, payloadLength)) {
        // Corrupt or invalid BPv7 bundle: drop record from storage
        _storage->deleteRecord(bundleHandle);
        return;
    }

    // Check bundle expiration
    uint32_t now = _timeProvider->getDtnTimestamp();
    if (header.lifetime > 0 && now > (header.creationTimestamp + header.lifetime)) {
        // Expired bundle: purge and notify
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
    meta.expirationTime = static_cast<uint32_t>(header.creationTimestamp + header.lifetime);
    meta.bpPriority = header.getPriority();
    meta.localRetryCount = 0;
    meta.localDynamicPriority = 0;

    _metaTable.add(meta);

    // Evaluate delivery or forwarding
    bool deliverLocal = (header.destination == _localEid);
    if (!deliverLocal && _router != nullptr) {
        uint8_t linkId = 0;
        routing::RouteDecision decision = _router->evaluate(header, linkId);
        if (decision == routing::RouteDecision::DELIVER_LOCAL) {
            deliverLocal = true;
        } else if (decision == routing::RouteDecision::DROP) {
            _metaTable.remove(bundleHandle);
            _storage->deleteRecord(bundleHandle);
            return;
        }
    }

    if (deliverLocal) {
        // Notify local application plugin
        ggg::system::SystemEvent delivEv = {};
        delivEv.type = muon::events::MUON_EVT_BUNDLE_DELIVERED;
        delivEv.source = 0x01;
        delivEv.priority = header.getPriority();
        delivEv.payload.u32[0] = bundleHandle;
        ggg::system::SystemBus::getInstance().publish(delivEv);
    } else {
        // Forwarding or storing
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
        return;
    }

    meta->localRetryCount++;
    if (meta->localRetryCount >= CONFIG_MUON_BPA_MAX_RETRIES) {
        // Exceeded maximum retry count: drop bundle
        _metaTable.remove(sessionHandle);
        if (_storage != nullptr) {
            _storage->deleteRecord(sessionHandle);
        }
    } else {
        // Apply congestion backoff offset
        if (meta->localDynamicPriority > -10) {
            meta->localDynamicPriority--;
        }
    }
}

void BundleAgent::tick() {
    if (_timeProvider != nullptr && _storage != nullptr) {
        uint32_t now = _timeProvider->getDtnTimestamp();
        _metaTable.purgeExpired(now, _storage);
    }
}

} // namespace bpa
} // namespace muon
