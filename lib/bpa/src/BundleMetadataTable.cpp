/**
 * @file BundleMetadataTable.cpp
 * @brief Static allocation and management of active bundle metadata records.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <muon/bpa/BundleMetadataTable.h>
#include <string.h>

namespace muon {
namespace bpa {

BundleMetadataTable::BundleMetadataTable() : _count(0) {
    clear();
}

void BundleMetadataTable::clear() {
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        _occupied[i] = false;
        memset(&_entries[i], 0, sizeof(BundleMetadata));
    }
    _count = 0;
}

bool BundleMetadataTable::add(const BundleMetadata& meta) {
    if (_count >= MAX_ENTRIES) {
        return false;
    }
    // Prevent duplicate storage handles
    if (find(meta.storageHandle) != nullptr) {
        return false;
    }

    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        if (!_occupied[i]) {
            _entries[i] = meta;
            _occupied[i] = true;
            _count++;
            return true;
        }
    }
    return false;
}

BundleMetadata* BundleMetadataTable::find(ggg::hal::StorageHandle_t handle) {
    if (handle == GGG_INVALID_HANDLE) {
        return nullptr;
    }
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        if (_occupied[i] && _entries[i].storageHandle == handle) {
            return &_entries[i];
        }
    }
    return nullptr;
}

const BundleMetadata* BundleMetadataTable::find(ggg::hal::StorageHandle_t handle) const {
    if (handle == GGG_INVALID_HANDLE) {
        return nullptr;
    }
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        if (_occupied[i] && _entries[i].storageHandle == handle) {
            return &_entries[i];
        }
    }
    return nullptr;
}

bool BundleMetadataTable::remove(ggg::hal::StorageHandle_t handle) {
    if (handle == GGG_INVALID_HANDLE) {
        return false;
    }
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        if (_occupied[i] && _entries[i].storageHandle == handle) {
            _occupied[i] = false;
            memset(&_entries[i], 0, sizeof(BundleMetadata));
            _count--;
            return true;
        }
    }
    return false;
}

bool BundleMetadataTable::getOldest(uint8_t minPriority, BundleMetadata& outMeta) const {
    int bestIndex = -1;
    uint32_t oldestExpiration = 0xFFFFFFFF;
    uint8_t highestPriority = 0;

    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        if (_occupied[i] && _entries[i].bpPriority >= minPriority) {
            // Prioritize higher priority first, then older expiration time
            if (bestIndex == -1 ||
                _entries[i].bpPriority > highestPriority ||
                (_entries[i].bpPriority == highestPriority && _entries[i].expirationTime < oldestExpiration)) {
                bestIndex = static_cast<int>(i);
                highestPriority = _entries[i].bpPriority;
                oldestExpiration = _entries[i].expirationTime;
            }
        }
    }

    if (bestIndex >= 0) {
        outMeta = _entries[bestIndex];
        return true;
    }
    return false;
}

size_t BundleMetadataTable::purgeExpired(uint32_t currentDtnTime, ggg::hal::IStorage* storage) {
    size_t purged = 0;
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        if (_occupied[i] && _entries[i].expirationTime > 0 && _entries[i].expirationTime <= currentDtnTime) {
            if (storage != nullptr) {
                storage->deleteRecord(_entries[i].storageHandle);
            }
            _occupied[i] = false;
            memset(&_entries[i], 0, sizeof(BundleMetadata));
            _count--;
            purged++;
        }
    }
    return purged;
}

} // namespace bpa
} // namespace muon
