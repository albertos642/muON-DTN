/**
 * @file BundleMetadataTable.h
 * @brief Static O(1) table indexing active bundle metadata and transmission priorities.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_BPA_BUNDLE_METADATA_TABLE_H
#define MUON_BPA_BUNDLE_METADATA_TABLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ggg/hal/IStorage.h>
#include <muon/bpa/BundleTypes.h>

#if defined(__has_include)
#if __has_include("autoconf.h")
#include "autoconf.h"
#endif
#endif

#ifndef CONFIG_MUON_BPA_MAX_BUNDLE_METADATA
#define CONFIG_MUON_BPA_MAX_BUNDLE_METADATA 16
#endif

namespace muon {
namespace bpa {

/**
 * @brief Zero-Malloc static table for tracking in-flight and stored bundle metadata.
 * Sized statically at compile time via CONFIG_MUON_BPA_MAX_BUNDLE_METADATA.
 */
class BundleMetadataTable {
public:
    static constexpr size_t MAX_ENTRIES = CONFIG_MUON_BPA_MAX_BUNDLE_METADATA;

private:
    BundleMetadata _entries[MAX_ENTRIES];
    bool _occupied[MAX_ENTRIES];
    size_t _count;

public:
    BundleMetadataTable();

    // Adds a metadata entry to the table. Fails if full or duplicate handle.
    bool add(const BundleMetadata& meta);

    // Finds metadata by storage handle
    BundleMetadata* find(ggg::hal::StorageHandle_t handle);
    const BundleMetadata* find(ggg::hal::StorageHandle_t handle) const;

    // Removes an entry by handle
    bool remove(ggg::hal::StorageHandle_t handle);

    // Retrieves the oldest active bundle matching or exceeding a given priority
    bool getOldest(uint8_t minPriority, BundleMetadata& outMeta) const;

    // Purges expired bundles whose expirationTime <= currentDtnTime
    size_t purgeExpired(uint32_t currentDtnTime, ggg::hal::IStorage* storage = nullptr);

    size_t count() const { return _count; }
    size_t capacity() const { return MAX_ENTRIES; }
    bool isFull() const { return _count >= MAX_ENTRIES; }
    void clear();
};

} // namespace bpa
} // namespace muon

#endif // MUON_BPA_BUNDLE_METADATA_TABLE_H
