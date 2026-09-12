/**
 * @file ITimeProvider.h
 * @brief Abstraction for queryable DTN network epoch timestamps.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_BPA_I_TIME_PROVIDER_H
#define MUON_BPA_I_TIME_PROVIDER_H

#include <stdint.h>

namespace muon {
namespace bpa {

/**
 * @brief Abstract time authority interface for DTN operations.
 * DTN timestamp is defined as seconds since 2000-01-01 00:00:00 UTC (RFC 9171).
 */
class ITimeProvider {
public:
    virtual ~ITimeProvider() = default;

    /**
     * @brief Returns current DTN timestamp in seconds.
     */
    virtual uint32_t getDtnTimestamp() const = 0;

    /**
     * @brief Checks if time authority has an accurate/calibrated time reference.
     */
    virtual bool isAuthoritative() const {
        return false;
    }
};

} // namespace bpa
} // namespace muon

#endif // MUON_BPA_I_TIME_PROVIDER_H
