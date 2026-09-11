/**
 * @file MuonEvents.h
 * @brief Event IDs and payload contracts for DTN subsystem events on SystemBus.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_BPA_MUON_EVENTS_H
#define MUON_BPA_MUON_EVENTS_H

#include <ggg/system/SystemEvent.h>

namespace muon {
namespace events {

/**
 * @brief muON SystemBus Event Extension IDs (starting at 0x1000).
 * Strictly conforms to Specs/muON_API_Specification.md Section 1.
 * 
 * Inter-layer bundle transfer strictly passes StorageHandle_t in payload.u32[0].
 * No raw packet payload bytes are ever allocated or passed in events.
 */

// Convergence Layer validated an RX frame into IStorage. payload.u32[0] = StorageHandle_t
constexpr ggg::system::EventId_t MUON_EVT_RX_READY    = 0x1001;

// Convergence Layer successfully completed bundle transmission. payload.u32[0] = StorageHandle_t
constexpr ggg::system::EventId_t MUON_EVT_TX_SUCCESS  = 0x1002;

// Convergence Layer reports failure (NACK or Timeout). payload.u32[0] = StorageHandle_t
constexpr ggg::system::EventId_t MUON_EVT_TX_FAILURE  = 0x1003;

// BPA requests routing decision for bundle in storage. payload.u32[0] = StorageHandle_t
constexpr ggg::system::EventId_t MUON_EVT_ROUTE_REQ   = 0x1004;

// BPA delivered bundle locally to an application plugin. payload.u32[0] = StorageHandle_t
constexpr ggg::system::EventId_t MUON_EVT_BUNDLE_DELIVERED = 0x1005;

// Bundle lifetime expired and was purged from storage. payload.u32[0] = StorageHandle_t
constexpr ggg::system::EventId_t MUON_EVT_BUNDLE_EXPIRED   = 0x1006;

} // namespace events
} // namespace muon

#endif // MUON_BPA_MUON_EVENTS_H
