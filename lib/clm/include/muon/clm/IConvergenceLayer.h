/**
 * @file IConvergenceLayer.h
 * @brief Abstract interface for physical Convergence Layer adapters.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_CLM_I_CONVERGENCE_LAYER_H
#define MUON_CLM_I_CONVERGENCE_LAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <ggg/hal/IStorage.h>

namespace muon {
namespace clm {

/**
 * @brief Abstract interface for L2.5 Convergence Layer Adapters (e.g. LoRaCL, UARTCL-COBS).
 * Strictly conforms to Specs/muON_API_Specification.md Section 2.
 */
class IConvergenceLayer {
public:
    virtual ~IConvergenceLayer() = default;

    /**
     * @brief Returns unique logical Link ID (used by the static routing table).
     */
    virtual uint8_t getLinkId() const = 0;

    /**
     * @brief Invoked by the CLM when the BPA or routing layer requests transmission.
     * The implementation must extract data via IStorage::readData and trigger PHY TX.
     * @param bundleHandle Storage handle containing the physical serialized bundle.
     * @param qos Service Class (0 = Unreliable / Fire-and-Forget, 1 = Notified / Acknowledged).
     * @return true if accepted for transmission.
     */
    virtual bool transmitBundle(ggg::hal::StorageHandle_t bundleHandle, uint8_t qos) = 0;

    /**
     * @brief Periodic non-blocking tick called by RTOS task to drive state machines,
     * duty cycle budgets, ACK timers, and async fragmentation.
     */
    virtual void tick() = 0;
};

} // namespace clm
} // namespace muon

#endif // MUON_CLM_I_CONVERGENCE_LAYER_H
