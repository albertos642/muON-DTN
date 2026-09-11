/**
 * @file StaticRoutingEngine.cpp
 * @brief Implementation of static routing table lookups and route management.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <muon/routing/StaticRoutingEngine.h>

namespace muon {
namespace routing {

StaticRoutingEngine::StaticRoutingEngine()
    : _localEid({0, 0}),
      _routeCount(0),
      _hasDefaultRoute(false),
      _defaultRouteLinkId(0)
{
    clear();
}

void StaticRoutingEngine::clear() {
    for (size_t i = 0; i < MAX_ROUTES; ++i) {
        _routes[i].destinationNode = 0;
        _routes[i].targetLinkId = 0;
        _routes[i].isActive = false;
    }
    _routeCount = 0;
    _hasDefaultRoute = false;
    _defaultRouteLinkId = 0;
}

void StaticRoutingEngine::setLocalEndpoint(const bpa::IpnEndpointId& localEid) {
    _localEid = localEid;
}

bool StaticRoutingEngine::addRoute(uint32_t targetNode, uint8_t linkId) {
    // Check if an existing route towards this node already exists (update)
    for (size_t i = 0; i < MAX_ROUTES; ++i) {
        if (_routes[i].isActive && _routes[i].destinationNode == targetNode) {
            _routes[i].targetLinkId = linkId;
            return true;
        }
    }

    if (_routeCount >= MAX_ROUTES) {
        return false; // Table full
    }

    // Insert in first free slot
    for (size_t i = 0; i < MAX_ROUTES; ++i) {
        if (!_routes[i].isActive) {
            _routes[i].destinationNode = targetNode;
            _routes[i].targetLinkId = linkId;
            _routes[i].isActive = true;
            _routeCount++;
            return true;
        }
    }
    return false;
}

bool StaticRoutingEngine::removeRoute(uint32_t targetNode) {
    for (size_t i = 0; i < MAX_ROUTES; ++i) {
        if (_routes[i].isActive && _routes[i].destinationNode == targetNode) {
            _routes[i].isActive = false;
            _routes[i].destinationNode = 0;
            _routes[i].targetLinkId = 0;
            _routeCount--;
            return true;
        }
    }
    return false;
}

void StaticRoutingEngine::setDefaultRoute(uint8_t linkId) {
    _defaultRouteLinkId = linkId;
    _hasDefaultRoute = true;
}

void StaticRoutingEngine::clearDefaultRoute() {
    _hasDefaultRoute = false;
    _defaultRouteLinkId = 0;
}

RouteDecision StaticRoutingEngine::evaluate(const bpa::BundleHeader& header, uint8_t& outLinkId) {
    // 1. Local delivery check
    if (header.destination == _localEid ||
        (header.destination.nodeNbr == _localEid.nodeNbr && _localEid.nodeNbr != 0))
    {
        return RouteDecision::DELIVER_LOCAL;
    }

    // 2. Query node routing table
    return evaluateNode(header.destination.nodeNbr, outLinkId);
}

RouteDecision StaticRoutingEngine::evaluateNode(uint32_t destinationNode, uint8_t& outLinkId) {
    // Local destination check
    if (destinationNode == _localEid.nodeNbr && _localEid.nodeNbr != 0) {
        return RouteDecision::DELIVER_LOCAL;
    }

    // Search explicit routes
    for (size_t i = 0; i < MAX_ROUTES; ++i) {
        if (_routes[i].isActive && _routes[i].destinationNode == destinationNode) {
            outLinkId = _routes[i].targetLinkId;
            return RouteDecision::FORWARD_DIRECT;
        }
    }

    // Fallback to default route if configured
    if (_hasDefaultRoute) {
        outLinkId = _defaultRouteLinkId;
        return RouteDecision::FORWARD_DIRECT;
    }

    // No route found: store in custody for opportunistic forwarding
    return RouteDecision::STORE_FOR_LATER;
}

} // namespace routing
} // namespace muon
