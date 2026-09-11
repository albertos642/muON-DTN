/**
 * @file BundleTypes.h
 * @brief Protocol data types, IPN Endpoint Identifiers, and BPv7 bundle structures.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_BPA_BUNDLE_TYPES_H
#define MUON_BPA_BUNDLE_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ggg/hal/IStorage.h>

namespace muon {
namespace bpa {

/**
 * @brief Endpoint ID based on IPN scheme (RFC 9171).
 * Avoids dynamic string handling for constrained embedded microcontrollers.
 */
struct IpnEndpointId {
    uint32_t nodeNbr;
    uint32_t serviceNbr;

    bool operator==(const IpnEndpointId& other) const {
        return (nodeNbr == other.nodeNbr) && (serviceNbr == other.serviceNbr);
    }

    bool operator!=(const IpnEndpointId& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Bundle Processing Control Flags (RFC 9171 Section 4.2.2)
 */
enum class BundleFlags : uint64_t {
    NONE                    = 0,
    IS_FRAGMENT             = (1ULL << 0),
    ADMIN_RECORD            = (1ULL << 1),
    DONT_FRAGMENT           = (1ULL << 2),
    REQ_STATUS_RECEPTION    = (1ULL << 14),
    REQ_STATUS_FORWARDING   = (1ULL << 15),
    REQ_STATUS_DELIVERY     = (1ULL << 16),
    REQ_STATUS_DELETION     = (1ULL << 17)
};

/**
 * @brief Bundle Priority Classes (RFC 9171, bits 7-8 of controlFlags)
 */
enum class BundlePriority : uint8_t {
    BULK       = 0,
    NORMAL     = 1,
    EXPEDITED  = 2,
    RESERVED   = 3
};

/**
 * @brief Primary Block data structure for BPv7 (RFC 9171).
 * Compact, fixed-size representation without dynamic memory allocations.
 */
struct BundleHeader {
    uint8_t version = 7;             ///< BP version (BPv7 = 7)
    uint64_t controlFlags = 0;       ///< Processing flags and priority mask
    uint64_t crcType = 0;            ///< 0 = No CRC, 1 = CRC-16, 2 = CRC-32

    IpnEndpointId destination = {0, 0};
    IpnEndpointId source = {0, 0};
    IpnEndpointId reportTo = {0, 0};

    uint64_t creationTimestamp = 0;  ///< DTN Time (seconds since 2000-01-01)
    uint64_t sequenceNumber = 0;     ///< Sequence number unique per node/timestamp
    uint64_t lifetime = 0;           ///< Lifetime in seconds (RFC 9171)

    // Fragmentation fields (present only when (controlFlags & IS_FRAGMENT) != 0)
    uint64_t fragmentOffset = 0;
    uint64_t totalAppDataLength = 0;

    bool isFragment() const {
        return (controlFlags & static_cast<uint64_t>(BundleFlags::IS_FRAGMENT)) != 0;
    }

    uint8_t getPriority() const {
        return static_cast<uint8_t>((controlFlags >> 7) & 0x03);
    }

    void setPriority(uint8_t prio) {
        controlFlags &= ~(0x03ULL << 7);
        controlFlags |= (static_cast<uint64_t>(prio & 0x03) << 7);
    }
};

/**
 * @brief In-memory indexing metadata for bundle custody and lifecycle management.
 * Strictly conforms to Specs/muON_API_Specification.md Section 3.
 */
struct BundleMetadata {
    ggg::hal::StorageHandle_t storageHandle; ///< Reference to physical serialized payload in Flash/RAM

    // IPN Naming Scheme (Destination)
    uint32_t destNode;
    uint32_t destService;

    // Lifetime and Priority Management
    uint32_t expirationTime;    ///< Calculated local DTN Epoch (Creation + Lifetime)
    uint8_t  bpPriority;        ///< 0=Bulk, 1=Normal, 2=Expedited (from Primary Block)

    // Local Convergence Layer Metadata (Not transmitted in CBOR wire format)
    uint8_t  localRetryCount;       ///< Transmission failure counter
    int8_t   localDynamicPriority;  ///< Dynamic priority offset for congestion backoff
};

} // namespace bpa
} // namespace muon

#endif // MUON_BPA_BUNDLE_TYPES_H
