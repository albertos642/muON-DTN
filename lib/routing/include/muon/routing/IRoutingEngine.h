/**
 * @file IRoutingEngine.h
 * @brief Forwarding header for the IRoutingEngine interface.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_ROUTING_I_ROUTING_ENGINE_H
#define MUON_ROUTING_I_ROUTING_ENGINE_H

#include <stdint.h>
#include <muon/bpa/BundleTypes.h>

namespace muon {
namespace routing {

/**
 * @brief Routing decision result for incoming and outgoing bundles.
 */
enum class RouteDecision {
    DELIVER_LOCAL,   ///< The bundle is addressed to this local node/service
    FORWARD_DIRECT,  ///< A next-hop link is available; forward bundle immediately
    STORE_FOR_LATER, ///< No link currently available; retain bundle in local custody
    DROP             ///< Invalid destination, hop count exceeded, or loop detected
};

/**
 * @brief Abstract interface to the DTN routing engine.
 */
class IRoutingEngine {
public:
    virtual ~IRoutingEngine() = default;

    /**
     * @brief Informs the routing engine of this node's local EID.
     */
    virtual void setLocalEndpoint(const bpa::IpnEndpointId& localEid) = 0;

    /**
     * @brief Decides routing action for a bundle by inspecting its Primary Block header.
     * @param header The Primary Block header.
     * @param outLinkId Set to the target link ID if decision is FORWARD_DIRECT.
     * @return The routing decision enum.
     */
    virtual RouteDecision evaluate(const bpa::BundleHeader& header, uint8_t& outLinkId) = 0;

    /**
     * @brief Decides routing action solely based on the destination node number.
     * @param destinationNode Destination node ID (IPN scheme).
     * @param outLinkId Set to the target link ID if decision is FORWARD_DIRECT.
     * @return The routing decision enum.
     */
    virtual RouteDecision evaluateNode(uint32_t destinationNode, uint8_t& outLinkId) = 0;
};

} // namespace routing
} // namespace muon

#endif // MUON_ROUTING_I_ROUTING_ENGINE_H
