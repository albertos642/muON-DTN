/**
 * @file StaticRoutingEngine.h
 * @brief Static routing table with O(1) destination-to-link mapping and default gateway.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_ROUTING_STATIC_ROUTING_ENGINE_H
#define MUON_ROUTING_STATIC_ROUTING_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <muon/routing/IRoutingEngine.h>

#if defined(__has_include)
#if __has_include("autoconf.h")
#include "autoconf.h"
#endif
#endif

#ifndef CONFIG_MUON_ROUTING_MAX_STATIC_ROUTES
#define CONFIG_MUON_ROUTING_MAX_STATIC_ROUTES 8
#endif

namespace muon {
namespace routing {

/**
 * @brief Static routing entry mapping a destination node to an outgoing physical link ID.
 */
struct StaticRouteEntry {
    uint32_t destinationNode;
    uint8_t targetLinkId;
    bool isActive;
};

/**
 * @brief Zero-Malloc static routing engine for low-power space & IoT microcontrollers.
 * Evaluates routing in fast O(1) time based on a statically configured routing table.
 */
class StaticRoutingEngine : public IRoutingEngine {
public:
    static constexpr size_t MAX_ROUTES = CONFIG_MUON_ROUTING_MAX_STATIC_ROUTES;

private:
    bpa::IpnEndpointId _localEid;
    StaticRouteEntry _routes[MAX_ROUTES];
    size_t _routeCount;

    bool _hasDefaultRoute;
    uint8_t _defaultRouteLinkId;

public:
    StaticRoutingEngine();

    void setLocalEndpoint(const bpa::IpnEndpointId& localEid) override;
    const bpa::IpnEndpointId& getLocalEndpoint() const { return _localEid; }

    /**
     * @brief Adds or updates a route towards a specific destination node.
     * @param targetNode Destination node ID (IPN scheme).
     * @param linkId Outgoing Convergence Layer Link ID.
     * @return true if route was added or updated, false if table is full.
     */
    bool addRoute(uint32_t targetNode, uint8_t linkId);

    /**
     * @brief Removes a route towards a destination node.
     * @param targetNode Destination node ID to remove.
     * @return true if route existed and was removed.
     */
    bool removeRoute(uint32_t targetNode);

    /**
     * @brief Sets a default fallback route (gateway of last resort).
     * @param linkId Physical link ID to use for unlisted nodes.
     */
    void setDefaultRoute(uint8_t linkId);

    /**
     * @brief Clears the default route.
     */
    void clearDefaultRoute();

    bool hasDefaultRoute() const { return _hasDefaultRoute; }
    uint8_t getDefaultRoute() const { return _defaultRouteLinkId; }

    /**
     * @brief Clears all entries from the static table.
     */
    void clear();

    size_t getRouteCount() const { return _routeCount; }
    size_t getCapacity() const { return MAX_ROUTES; }

    /**
     * @brief Evaluates an incoming or newly created bundle header.
     */
    RouteDecision evaluate(const bpa::BundleHeader& header, uint8_t& outLinkId) override;

    /**
     * @brief Evaluates a target node number directly.
     */
    RouteDecision evaluateNode(uint32_t destinationNode, uint8_t& outLinkId) override;
};

} // namespace routing
} // namespace muon

#endif // MUON_ROUTING_STATIC_ROUTING_ENGINE_H
