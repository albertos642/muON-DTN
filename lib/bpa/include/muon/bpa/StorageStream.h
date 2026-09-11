/**
 * @file StorageStream.h
 * @brief Stream adapters wrapping IStorage for zero-malloc CBOR serialization.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_BPA_STORAGE_STREAM_H
#define MUON_BPA_STORAGE_STREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ggg/hal/IStream.h>
#include <ggg/hal/IStorage.h>

namespace muon {
namespace bpa {

/**
 * @brief Zero-Malloc adapter: reads sequentially from an IStorage record via IInputStream.
 * Employs a small stack/member cache to minimize IStorage::readData call overhead.
 */
class StorageInputStream : public ggg::hal::IInputStream {
private:
    ggg::hal::IStorage& _storage;
    ggg::hal::StorageHandle_t _handle;
    size_t _offset;
    size_t _totalSize;

    uint8_t _cache[32];
    size_t _cachePos;
    size_t _cacheLen;

    bool refillCache();

public:
    StorageInputStream(ggg::hal::IStorage& storage, ggg::hal::StorageHandle_t handle, size_t startOffset = 0);

    // ggg::hal::IInputStream interface
    size_t available() override;
    int read() override;
    void flushRX() override;

    // Stream helper methods
    size_t readBytes(uint8_t* buffer, size_t size);
    size_t skip(size_t length);
    size_t getPosition() const;
    size_t getTotalSize() const { return _totalSize; }
    ggg::hal::StorageHandle_t getHandle() const { return _handle; }
};

/**
 * @brief Zero-Malloc adapter: writes stream output directly into a transactional IStorage record.
 */
class StorageOutputStream : public ggg::hal::IOutputStream {
private:
    ggg::hal::IStorage& _storage;
    ggg::hal::StorageHandle_t _handle;
    bool _committed;
    bool _aborted;

    uint8_t _cache[32];
    size_t _cachePos;
    size_t _bytesWritten;

    bool flushCache();

public:
    StorageOutputStream(ggg::hal::IStorage& storage, size_t expectedSize = 0);
    ~StorageOutputStream() override;

    // ggg::hal::IOutputStream interface
    size_t write(uint8_t b) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    void flush() override;

    // Transaction controls
    bool isValid() const { return _handle != GGG_INVALID_HANDLE && !_aborted && !_committed; }
    ggg::hal::StorageHandle_t commit();
    void abort();
    size_t getBytesWritten() const { return _bytesWritten + _cachePos; }
};

/**
 * @brief In-memory byte stream adapter implementing ggg::hal::IInputStream (useful for tests/headers).
 */
class MemoryInputStream : public ggg::hal::IInputStream {
private:
    const uint8_t* _buffer;
    size_t _size;
    size_t _pos;

public:
    MemoryInputStream(const uint8_t* buffer, size_t size);

    size_t available() override;
    int read() override;
    void flushRX() override;

    size_t readBytes(uint8_t* buffer, size_t size);
    size_t skip(size_t length);
    size_t getPosition() const { return _pos; }
};

/**
 * @brief In-memory byte stream adapter implementing ggg::hal::IOutputStream (useful for tests/headers).
 */
class MemoryOutputStream : public ggg::hal::IOutputStream {
private:
    uint8_t* _buffer;
    size_t _capacity;
    size_t _pos;

public:
    MemoryOutputStream(uint8_t* buffer, size_t capacity);

    size_t write(uint8_t b) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    void flush() override;

    size_t getBytesWritten() const { return _pos; }
    size_t getCapacity() const { return _capacity; }
};

} // namespace bpa
} // namespace muon

#endif // MUON_BPA_STORAGE_STREAM_H
