/**
 * @file StorageStream.cpp
 * @brief Buffered input and output stream wrappers for IStorage blocks.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <muon/bpa/StorageStream.h>
#include <string.h>

namespace muon {
namespace bpa {

// ============================================================================
// StorageInputStream Implementation
// ============================================================================

StorageInputStream::StorageInputStream(ggg::hal::IStorage& storage, ggg::hal::StorageHandle_t handle, size_t startOffset)
    : _storage(storage),
      _handle(handle),
      _offset(startOffset),
      _totalSize(storage.getSize(handle)),
      _cachePos(0),
      _cacheLen(0)
{
    if (_offset > _totalSize) {
        _offset = _totalSize;
    }
}

bool StorageInputStream::refillCache() {
    if (_offset >= _totalSize || _handle == GGG_INVALID_HANDLE) {
        return false;
    }
    size_t remaining = _totalSize - _offset;
    size_t toRead = (remaining < sizeof(_cache)) ? remaining : sizeof(_cache);
    _cacheLen = _storage.readData(_handle, _offset, _cache, toRead);
    _offset += _cacheLen;
    _cachePos = 0;
    return (_cacheLen > 0);
}

size_t StorageInputStream::available() {
    size_t inCache = (_cachePos < _cacheLen) ? (_cacheLen - _cachePos) : 0;
    size_t inStorage = (_offset < _totalSize) ? (_totalSize - _offset) : 0;
    return inCache + inStorage;
}

int StorageInputStream::read() {
    if (_cachePos < _cacheLen) {
        return _cache[_cachePos++];
    }
    if (!refillCache()) {
        return -1;
    }
    return _cache[_cachePos++];
}

void StorageInputStream::flushRX() {
    _cachePos = 0;
    _cacheLen = 0;
}

size_t StorageInputStream::readBytes(uint8_t* buffer, size_t size) {
    if (!buffer || size == 0) {
        return 0;
    }
    size_t bytesRead = 0;
    while (bytesRead < size) {
        if (_cachePos < _cacheLen) {
            size_t take = _cacheLen - _cachePos;
            if (take > size - bytesRead) {
                take = size - bytesRead;
            }
            memcpy(buffer + bytesRead, _cache + _cachePos, take);
            _cachePos += take;
            bytesRead += take;
        } else {
            if (!refillCache()) {
                break;
            }
        }
    }
    return bytesRead;
}

size_t StorageInputStream::skip(size_t length) {
    size_t skipped = 0;
    while (skipped < length) {
        if (_cachePos < _cacheLen) {
            size_t take = _cacheLen - _cachePos;
            if (take > length - skipped) {
                take = length - skipped;
            }
            _cachePos += take;
            skipped += take;
        } else {
            size_t remaining = length - skipped;
            size_t inStorage = (_offset < _totalSize) ? (_totalSize - _offset) : 0;
            if (remaining > sizeof(_cache) && inStorage > 0) {
                size_t jump = (remaining < inStorage) ? remaining : inStorage;
                _offset += jump;
                skipped += jump;
            } else {
                if (!refillCache()) {
                    break;
                }
            }
        }
    }
    return skipped;
}

size_t StorageInputStream::getPosition() const {
    size_t inCache = (_cachePos < _cacheLen) ? (_cacheLen - _cachePos) : 0;
    return _offset - inCache;
}

// ============================================================================
// StorageOutputStream Implementation
// ============================================================================

StorageOutputStream::StorageOutputStream(ggg::hal::IStorage& storage, size_t expectedSize)
    : _storage(storage),
      _handle(storage.beginWrite()),
      _committed(false),
      _aborted(false),
      _cachePos(0),
      _bytesWritten(0)
{
    (void)expectedSize;
}

StorageOutputStream::~StorageOutputStream() {
    if (!_committed && !_aborted && _handle != GGG_INVALID_HANDLE) {
        abort();
    }
}

bool StorageOutputStream::flushCache() {
    if (_cachePos == 0) {
        return true;
    }
    if (_handle == GGG_INVALID_HANDLE || _aborted || _committed) {
        return false;
    }
    size_t written = _storage.writeData(_handle, _cache, _cachePos);
    if (written == _cachePos) {
        _bytesWritten += _cachePos;
        _cachePos = 0;
        return true;
    }
    return false;
}

size_t StorageOutputStream::write(uint8_t b) {
    if (_aborted || _committed || _handle == GGG_INVALID_HANDLE) {
        return 0;
    }
    if (_cachePos >= sizeof(_cache)) {
        if (!flushCache()) {
            return 0;
        }
    }
    _cache[_cachePos++] = b;
    return 1;
}

size_t StorageOutputStream::write(const uint8_t* buffer, size_t size) {
    if (!buffer || size == 0 || _aborted || _committed || _handle == GGG_INVALID_HANDLE) {
        return 0;
    }
    if (!flushCache()) {
        return 0;
    }
    size_t written = _storage.writeData(_handle, buffer, size);
    _bytesWritten += written;
    return written;
}

void StorageOutputStream::flush() {
    flushCache();
}

ggg::hal::StorageHandle_t StorageOutputStream::commit() {
    if (_committed || _aborted || _handle == GGG_INVALID_HANDLE) {
        return GGG_INVALID_HANDLE;
    }
    if (!flushCache()) {
        abort();
        return GGG_INVALID_HANDLE;
    }
    bool ok = _storage.commitWrite(_handle);
    if (ok) {
        ggg::hal::StorageHandle_t res = _handle;
        _committed = true;
        _handle = GGG_INVALID_HANDLE;
        return res;
    } else {
        abort();
        return GGG_INVALID_HANDLE;
    }
}

void StorageOutputStream::abort() {
    if (_aborted || _committed || _handle == GGG_INVALID_HANDLE) {
        return;
    }
    _cachePos = 0;
    _storage.abortWrite(_handle);
    _aborted = true;
    _handle = GGG_INVALID_HANDLE;
}

// ============================================================================
// MemoryInputStream Implementation
// ============================================================================

MemoryInputStream::MemoryInputStream(const uint8_t* buffer, size_t size)
    : _buffer(buffer), _size(size), _pos(0)
{
}

size_t MemoryInputStream::available() {
    return (_pos < _size) ? (_size - _pos) : 0;
}

int MemoryInputStream::read() {
    if (_pos < _size) {
        return _buffer[_pos++];
    }
    return -1;
}

void MemoryInputStream::flushRX() {
    _pos = _size;
}

size_t MemoryInputStream::readBytes(uint8_t* buffer, size_t size) {
    if (!buffer || size == 0 || _pos >= _size) {
        return 0;
    }
    size_t toRead = (_size - _pos < size) ? (_size - _pos) : size;
    memcpy(buffer, _buffer + _pos, toRead);
    _pos += toRead;
    return toRead;
}

size_t MemoryInputStream::skip(size_t length) {
    size_t toSkip = (_size - _pos < length) ? (_size - _pos) : length;
    _pos += toSkip;
    return toSkip;
}

// ============================================================================
// MemoryOutputStream Implementation
// ============================================================================

MemoryOutputStream::MemoryOutputStream(uint8_t* buffer, size_t capacity)
    : _buffer(buffer), _capacity(capacity), _pos(0)
{
}

size_t MemoryOutputStream::write(uint8_t b) {
    if (_pos < _capacity) {
        _buffer[_pos++] = b;
        return 1;
    }
    return 0;
}

size_t MemoryOutputStream::write(const uint8_t* buffer, size_t size) {
    if (!buffer || size == 0 || _pos >= _capacity) {
        return 0;
    }
    size_t toWrite = (_capacity - _pos < size) ? (_capacity - _pos) : size;
    memcpy(_buffer + _pos, buffer, toWrite);
    _pos += toWrite;
    return toWrite;
}

void MemoryOutputStream::flush() {
    // No-op for in-memory buffer
}

} // namespace bpa
} // namespace muon
