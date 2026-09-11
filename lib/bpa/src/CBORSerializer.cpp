/**
 * @file CBORSerializer.cpp
 * @brief RFC 8949 encoding and decoding primitives with stream-based processing.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#include <muon/bpa/CBORSerializer.h>

namespace muon {
namespace bpa {

// ============================================================================
// Internal Helpers
// ============================================================================

static bool readExactBytes(ggg::hal::IInputStream& stream, uint8_t* buffer, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        int b = stream.read();
        if (b < 0) {
            return false;
        }
        buffer[i] = static_cast<uint8_t>(b);
    }
    return true;
}

// ============================================================================
// RFC 8949 Primitive Encoders
// ============================================================================

bool CBORSerializer::encodeTypeAndValue(ggg::hal::IOutputStream& stream, uint8_t majorType, uint64_t value) {
    uint8_t mtShifted = static_cast<uint8_t>((majorType & 0x07) << 5);

    if (value < 24) {
        return (stream.write(mtShifted | static_cast<uint8_t>(value)) == 1);
    } else if (value <= 0xFFULL) {
        if (stream.write(mtShifted | 24) != 1) return false;
        return (stream.write(static_cast<uint8_t>(value)) == 1);
    } else if (value <= 0xFFFFULL) {
        if (stream.write(mtShifted | 25) != 1) return false;
        if (stream.write(static_cast<uint8_t>((value >> 8) & 0xFF)) != 1) return false;
        return (stream.write(static_cast<uint8_t>(value & 0xFF)) == 1);
    } else if (value <= 0xFFFFFFFFULL) {
        if (stream.write(mtShifted | 26) != 1) return false;
        for (int i = 24; i >= 0; i -= 8) {
            if (stream.write(static_cast<uint8_t>((value >> i) & 0xFF)) != 1) return false;
        }
        return true;
    } else {
        if (stream.write(mtShifted | 27) != 1) return false;
        for (int i = 56; i >= 0; i -= 8) {
            if (stream.write(static_cast<uint8_t>((value >> i) & 0xFF)) != 1) return false;
        }
        return true;
    }
}

bool CBORSerializer::encodeUnsignedInteger(ggg::hal::IOutputStream& stream, uint64_t value) {
    return encodeTypeAndValue(stream, 0, value);
}

bool CBORSerializer::encodeByteStringHeader(ggg::hal::IOutputStream& stream, size_t length) {
    return encodeTypeAndValue(stream, 2, static_cast<uint64_t>(length));
}

bool CBORSerializer::encodeByteString(ggg::hal::IOutputStream& stream, const uint8_t* data, size_t length) {
    if (!encodeByteStringHeader(stream, length)) {
        return false;
    }
    if (length > 0 && data != nullptr) {
        return (stream.write(data, length) == length);
    }
    return true;
}

bool CBORSerializer::encodeArrayHeader(ggg::hal::IOutputStream& stream, size_t numElements) {
    return encodeTypeAndValue(stream, 4, static_cast<uint64_t>(numElements));
}

bool CBORSerializer::encodeEID(ggg::hal::IOutputStream& stream, const IpnEndpointId& eid) {
    // RFC 9171: IPN Scheme EID = Array[2] { 2 (ipn code), Array[2] { nodeNbr, serviceNbr } }
    if (!encodeArrayHeader(stream, 2)) return false;
    if (!encodeUnsignedInteger(stream, 2)) return false; // IPN scheme ID = 2
    if (!encodeArrayHeader(stream, 2)) return false;
    if (!encodeUnsignedInteger(stream, eid.nodeNbr)) return false;
    if (!encodeUnsignedInteger(stream, eid.serviceNbr)) return false;
    return true;
}

bool CBORSerializer::encodeCreationTimestamp(ggg::hal::IOutputStream& stream, uint64_t timestamp, uint64_t seqNo) {
    // RFC 9171: Creation Timestamp = Array[2] { creationTime, sequenceNumber }
    if (!encodeArrayHeader(stream, 2)) return false;
    if (!encodeUnsignedInteger(stream, timestamp)) return false;
    if (!encodeUnsignedInteger(stream, seqNo)) return false;
    return true;
}

// ============================================================================
// RFC 8949 Primitive Decoders
// ============================================================================

bool CBORSerializer::decodeInitialByte(ggg::hal::IInputStream& stream, uint8_t& outMajorType, uint8_t& outAdditionalInfo) {
    int b = stream.read();
    if (b < 0) {
        return false;
    }
    outMajorType = static_cast<uint8_t>((b >> 5) & 0x07);
    outAdditionalInfo = static_cast<uint8_t>(b & 0x1F);
    return true;
}

bool CBORSerializer::decodeTypeAndValue(ggg::hal::IInputStream& stream, uint8_t expectedMajorType, uint64_t& outValue) {
    uint8_t majorType = 0;
    uint8_t addInfo = 0;
    if (!decodeInitialByte(stream, majorType, addInfo)) {
        return false;
    }
    if (majorType != expectedMajorType) {
        return false;
    }

    if (addInfo < 24) {
        outValue = addInfo;
        return true;
    } else if (addInfo == 24) {
        int b = stream.read();
        if (b < 0) return false;
        outValue = static_cast<uint8_t>(b);
        return true;
    } else if (addInfo == 25) {
        uint8_t buf[2];
        if (!readExactBytes(stream, buf, 2)) return false;
        outValue = (static_cast<uint64_t>(buf[0]) << 8) | buf[1];
        return true;
    } else if (addInfo == 26) {
        uint8_t buf[4];
        if (!readExactBytes(stream, buf, 4)) return false;
        outValue = (static_cast<uint64_t>(buf[0]) << 24) |
                   (static_cast<uint64_t>(buf[1]) << 16) |
                   (static_cast<uint64_t>(buf[2]) << 8)  |
                   static_cast<uint64_t>(buf[3]);
        return true;
    } else if (addInfo == 27) {
        uint8_t buf[8];
        if (!readExactBytes(stream, buf, 8)) return false;
        outValue = 0;
        for (int i = 0; i < 8; ++i) {
            outValue = (outValue << 8) | buf[i];
        }
        return true;
    }
    // 28..31 are reserved or indefinite for integers
    return false;
}

bool CBORSerializer::decodeUnsignedInteger(ggg::hal::IInputStream& stream, uint64_t& outValue) {
    return decodeTypeAndValue(stream, 0, outValue);
}

bool CBORSerializer::decodeArrayHeader(ggg::hal::IInputStream& stream, size_t& outNumElements, bool& outIndefinite) {
    uint8_t majorType = 0;
    uint8_t addInfo = 0;
    if (!decodeInitialByte(stream, majorType, addInfo)) {
        return false;
    }
    if (majorType != 4) {
        return false;
    }

    if (addInfo == 31) {
        outIndefinite = true;
        outNumElements = 0;
        return true;
    }

    outIndefinite = false;
    if (addInfo < 24) {
        outNumElements = addInfo;
        return true;
    } else if (addInfo == 24) {
        int b = stream.read();
        if (b < 0) return false;
        outNumElements = static_cast<uint8_t>(b);
        return true;
    } else if (addInfo == 25) {
        uint8_t buf[2];
        if (!readExactBytes(stream, buf, 2)) return false;
        outNumElements = (static_cast<size_t>(buf[0]) << 8) | buf[1];
        return true;
    } else if (addInfo == 26) {
        uint8_t buf[4];
        if (!readExactBytes(stream, buf, 4)) return false;
        outNumElements = (static_cast<size_t>(buf[0]) << 24) |
                         (static_cast<size_t>(buf[1]) << 16) |
                         (static_cast<size_t>(buf[2]) << 8)  |
                         static_cast<size_t>(buf[3]);
        return true;
    } else if (addInfo == 27) {
        uint8_t buf[8];
        if (!readExactBytes(stream, buf, 8)) return false;
        uint64_t val = 0;
        for (int i = 0; i < 8; ++i) {
            val = (val << 8) | buf[i];
        }
        outNumElements = static_cast<size_t>(val);
        return true;
    }
    return false;
}

bool CBORSerializer::decodeByteStringHeader(ggg::hal::IInputStream& stream, size_t& outLength, bool& outIndefinite) {
    uint8_t majorType = 0;
    uint8_t addInfo = 0;
    if (!decodeInitialByte(stream, majorType, addInfo)) {
        return false;
    }
    if (majorType != 2) {
        return false;
    }

    if (addInfo == 31) {
        outIndefinite = true;
        outLength = 0;
        return true;
    }

    outIndefinite = false;
    if (addInfo < 24) {
        outLength = addInfo;
        return true;
    } else if (addInfo == 24) {
        int b = stream.read();
        if (b < 0) return false;
        outLength = static_cast<uint8_t>(b);
        return true;
    } else if (addInfo == 25) {
        uint8_t buf[2];
        if (!readExactBytes(stream, buf, 2)) return false;
        outLength = (static_cast<size_t>(buf[0]) << 8) | buf[1];
        return true;
    } else if (addInfo == 26) {
        uint8_t buf[4];
        if (!readExactBytes(stream, buf, 4)) return false;
        outLength = (static_cast<size_t>(buf[0]) << 24) |
                    (static_cast<size_t>(buf[1]) << 16) |
                    (static_cast<size_t>(buf[2]) << 8)  |
                    static_cast<size_t>(buf[3]);
        return true;
    } else if (addInfo == 27) {
        uint8_t buf[8];
        if (!readExactBytes(stream, buf, 8)) return false;
        uint64_t val = 0;
        for (int i = 0; i < 8; ++i) {
            val = (val << 8) | buf[i];
        }
        outLength = static_cast<size_t>(val);
        return true;
    }
    return false;
}

bool CBORSerializer::decodeEID(ggg::hal::IInputStream& stream, IpnEndpointId& outEid) {
    size_t outerLen = 0;
    bool indef = false;
    if (!decodeArrayHeader(stream, outerLen, indef) || outerLen != 2) {
        return false;
    }

    uint64_t scheme = 0;
    if (!decodeUnsignedInteger(stream, scheme) || scheme != 2) {
        return false; // Only IPN scheme (2) supported
    }

    size_t innerLen = 0;
    if (!decodeArrayHeader(stream, innerLen, indef) || innerLen != 2) {
        return false;
    }

    uint64_t node = 0;
    uint64_t service = 0;
    if (!decodeUnsignedInteger(stream, node)) return false;
    if (!decodeUnsignedInteger(stream, service)) return false;

    outEid.nodeNbr = static_cast<uint32_t>(node);
    outEid.serviceNbr = static_cast<uint32_t>(service);
    return true;
}

bool CBORSerializer::decodeCreationTimestamp(ggg::hal::IInputStream& stream, uint64_t& outTimestamp, uint64_t& outSeqNo) {
    size_t len = 0;
    bool indef = false;
    if (!decodeArrayHeader(stream, len, indef) || len != 2) {
        return false;
    }
    if (!decodeUnsignedInteger(stream, outTimestamp)) return false;
    if (!decodeUnsignedInteger(stream, outSeqNo)) return false;
    return true;
}

bool CBORSerializer::skipCborItem(ggg::hal::IInputStream& stream, int initialByte) {
    uint8_t major = 0;
    uint8_t add = 0;
    if (initialByte >= 0) {
        major = static_cast<uint8_t>((initialByte >> 5) & 0x07);
        add = static_cast<uint8_t>(initialByte & 0x1F);
    } else {
        if (!decodeInitialByte(stream, major, add)) {
            return false;
        }
    }

    // Special break code (0xFF)
    if (major == 7 && add == 31) {
        return true;
    }

    uint64_t value = 0;
    if (add < 24) {
        value = add;
    } else if (add == 24) {
        int b = stream.read();
        if (b < 0) return false;
        value = static_cast<uint8_t>(b);
    } else if (add == 25) {
        uint8_t b[2];
        if (!readExactBytes(stream, b, 2)) return false;
        value = (static_cast<uint64_t>(b[0]) << 8) | b[1];
    } else if (add == 26) {
        uint8_t b[4];
        if (!readExactBytes(stream, b, 4)) return false;
        value = (static_cast<uint64_t>(b[0]) << 24) |
                (static_cast<uint64_t>(b[1]) << 16) |
                (static_cast<uint64_t>(b[2]) << 8)  |
                static_cast<uint64_t>(b[3]);
    } else if (add == 27) {
        uint8_t b[8];
        if (!readExactBytes(stream, b, 8)) return false;
        for (int i = 0; i < 8; ++i) {
            value = (value << 8) | b[i];
        }
    } else if (add == 31) {
        // Indefinite length handled per major type
    } else {
        return false; // Reserved
    }

    switch (major) {
        case 0: // uint
        case 1: // nint
        case 7: // simple/float
            return true;

        case 2: // bstr
        case 3: // tstr
            if (add == 31) {
                // Indefinite length chunks
                while (true) {
                    int next = stream.read();
                    if (next < 0) return false;
                    if (next == 0xFF) break; // "break" stop code
                    uint8_t chunkMajor = static_cast<uint8_t>((next >> 5) & 0x07);
                    uint8_t chunkAdd = static_cast<uint8_t>(next & 0x1F);
                    if (chunkMajor != major) return false;
                    uint64_t chunkLen = 0;
                    if (chunkAdd < 24) chunkLen = chunkAdd;
                    else if (chunkAdd == 24) { int b = stream.read(); if (b < 0) return false; chunkLen = static_cast<uint8_t>(b); }
                    else if (chunkAdd == 25) { uint8_t b[2]; if (!readExactBytes(stream, b, 2)) return false; chunkLen = (static_cast<uint64_t>(b[0]) << 8) | b[1]; }
                    else if (chunkAdd == 26) { uint8_t b[4]; if (!readExactBytes(stream, b, 4)) return false; chunkLen = (static_cast<uint64_t>(b[0]) << 24) | (static_cast<uint64_t>(b[1]) << 16) | (static_cast<uint64_t>(b[2]) << 8) | b[3]; }
                    else return false;
                    for (uint64_t i = 0; i < chunkLen; ++i) {
                        if (stream.read() < 0) return false;
                    }
                }
                return true;
            } else {
                for (uint64_t i = 0; i < value; ++i) {
                    if (stream.read() < 0) return false;
                }
                return true;
            }

        case 4: // array
            if (add == 31) {
                while (true) {
                    int p = stream.read();
                    if (p < 0) return false;
                    if (p == 0xFF) break;
                    if (!skipCborItem(stream, p)) return false;
                }
                return true;
            } else {
                for (uint64_t i = 0; i < value; ++i) {
                    if (!skipCborItem(stream)) return false;
                }
                return true;
            }

        case 5: // map
            if (add == 31) {
                while (true) {
                    int p = stream.read();
                    if (p < 0) return false;
                    if (p == 0xFF) break;
                    if (!skipCborItem(stream, p)) return false; // key
                    if (!skipCborItem(stream)) return false;    // value
                }
                return true;
            } else {
                for (uint64_t i = 0; i < value * 2; ++i) {
                    if (!skipCborItem(stream)) return false;
                }
                return true;
            }

        case 6: // semantic tag
            return skipCborItem(stream);

        default:
            return false;
    }
}

// ============================================================================
// BPv7 Bundle High-Level Operations (RFC 9171)
// ============================================================================

bool CBORSerializer::serializeBundle(const BundleHeader& header,
                                    const uint8_t* payloadData,
                                    size_t payloadLength,
                                    ggg::hal::IOutputStream& stream)
{
    // A BPv7 bundle is a CBOR array of blocks: [PrimaryBlock, PayloadBlock, ...]
    if (!encodeArrayHeader(stream, 2)) return false;

    // ------------------------------------------------------------------------
    // Primary Block Serialization
    // ------------------------------------------------------------------------
    uint8_t primaryElemCount = header.isFragment() ? 10 : 8;
    if (header.crcType != 0) {
        primaryElemCount += 1;
    }
    if (!encodeArrayHeader(stream, primaryElemCount)) return false;

    // 1. Version (RFC 9171 requires BP version 7)
    if (!encodeUnsignedInteger(stream, header.version)) return false;

    // 2. Bundle Processing Control Flags
    if (!encodeUnsignedInteger(stream, header.controlFlags)) return false;

    // 3. CRC Type
    if (!encodeUnsignedInteger(stream, header.crcType)) return false;

    // 4. Destination EID
    if (!encodeEID(stream, header.destination)) return false;

    // 5. Source EID
    if (!encodeEID(stream, header.source)) return false;

    // 6. Report-To EID
    if (!encodeEID(stream, header.reportTo)) return false;

    // 7. Creation Timestamp
    if (!encodeCreationTimestamp(stream, header.creationTimestamp, header.sequenceNumber)) return false;

    // 8. Lifetime
    if (!encodeUnsignedInteger(stream, header.lifetime)) return false;

    // Fragmentation fields if IS_FRAGMENT is active
    if (header.isFragment()) {
        if (!encodeUnsignedInteger(stream, header.fragmentOffset)) return false;
        if (!encodeUnsignedInteger(stream, header.totalAppDataLength)) return false;
    }

    // Optional Primary Block CRC field (if crcType != 0, write dummy/crc here)
    if (header.crcType != 0) {
        if (!encodeUnsignedInteger(stream, 0)) return false;
    }

    // ------------------------------------------------------------------------
    // Payload Block Serialization (Block Type 1)
    // ------------------------------------------------------------------------
    // Elements: [blockType, blockNbr, blockControlFlags, crcType, payloadData]
    if (!encodeArrayHeader(stream, 5)) return false;

    if (!encodeUnsignedInteger(stream, 1)) return false; // Block Type: 1 (Payload Block)
    if (!encodeUnsignedInteger(stream, 1)) return false; // Block Number: 1
    if (!encodeUnsignedInteger(stream, 0)) return false; // Block Control Flags: 0
    if (!encodeUnsignedInteger(stream, 0)) return false; // CRC Type: 0 (No CRC)

    // Payload ByteString
    if (!encodeByteString(stream, payloadData, payloadLength)) return false;

    stream.flush();
    return true;
}

bool CBORSerializer::serializeBundleFromStorage(const BundleHeader& header,
                                                ggg::hal::StorageHandle_t payloadHandle,
                                                ggg::hal::IStorage& storage,
                                                ggg::hal::IOutputStream& stream)
{
    size_t payloadLength = storage.getSize(payloadHandle);
    if (!encodeArrayHeader(stream, 2)) return false;

    // Primary Block
    uint8_t primaryElemCount = header.isFragment() ? 10 : 8;
    if (header.crcType != 0) primaryElemCount += 1;
    if (!encodeArrayHeader(stream, primaryElemCount)) return false;

    if (!encodeUnsignedInteger(stream, header.version)) return false;
    if (!encodeUnsignedInteger(stream, header.controlFlags)) return false;
    if (!encodeUnsignedInteger(stream, header.crcType)) return false;
    if (!encodeEID(stream, header.destination)) return false;
    if (!encodeEID(stream, header.source)) return false;
    if (!encodeEID(stream, header.reportTo)) return false;
    if (!encodeCreationTimestamp(stream, header.creationTimestamp, header.sequenceNumber)) return false;
    if (!encodeUnsignedInteger(stream, header.lifetime)) return false;

    if (header.isFragment()) {
        if (!encodeUnsignedInteger(stream, header.fragmentOffset)) return false;
        if (!encodeUnsignedInteger(stream, header.totalAppDataLength)) return false;
    }
    if (header.crcType != 0) {
        if (!encodeUnsignedInteger(stream, 0)) return false;
    }

    // Payload Block
    if (!encodeArrayHeader(stream, 5)) return false;
    if (!encodeUnsignedInteger(stream, 1)) return false;
    if (!encodeUnsignedInteger(stream, 1)) return false;
    if (!encodeUnsignedInteger(stream, 0)) return false;
    if (!encodeUnsignedInteger(stream, 0)) return false;

    if (!encodeByteStringHeader(stream, payloadLength)) return false;

    // Stream payload from storage in 32-byte chunks (Zero-Malloc)
    uint8_t chunk[32];
    size_t remaining = payloadLength;
    size_t offset = 0;
    while (remaining > 0) {
        size_t toRead = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
        size_t r = storage.readData(payloadHandle, offset, chunk, toRead);
        if (r != toRead) return false;
        if (stream.write(chunk, toRead) != toRead) return false;
        offset += toRead;
        remaining -= toRead;
    }

    stream.flush();
    return true;
}

bool CBORSerializer::deserializeBundleHeader(ggg::hal::IInputStream& stream,
                                            BundleHeader& outHeader,
                                            size_t& outPayloadLength)
{
    size_t totalBlocks = 0;
    bool bundleIndefinite = false;
    if (!decodeArrayHeader(stream, totalBlocks, bundleIndefinite)) {
        return false;
    }
    if (!bundleIndefinite && totalBlocks < 2) {
        return false;
    }

    // ------------------------------------------------------------------------
    // Parse Primary Block
    // ------------------------------------------------------------------------
    size_t primaryElemCount = 0;
    bool primaryIndef = false;
    if (!decodeArrayHeader(stream, primaryElemCount, primaryIndef) || primaryElemCount < 8) {
        return false;
    }

    // 1. Version
    uint64_t ver = 0;
    if (!decodeUnsignedInteger(stream, ver) || ver != 7) {
        return false; // Only BPv7 is supported
    }
    outHeader.version = static_cast<uint8_t>(ver);

    // 2. Bundle Processing Control Flags
    if (!decodeUnsignedInteger(stream, outHeader.controlFlags)) return false;

    // 3. CRC Type
    if (!decodeUnsignedInteger(stream, outHeader.crcType)) return false;

    // 4. Destination EID
    if (!decodeEID(stream, outHeader.destination)) return false;

    // 5. Source EID
    if (!decodeEID(stream, outHeader.source)) return false;

    // 6. Report-To EID
    if (!decodeEID(stream, outHeader.reportTo)) return false;

    // 7. Creation Timestamp
    if (!decodeCreationTimestamp(stream, outHeader.creationTimestamp, outHeader.sequenceNumber)) return false;

    // 8. Lifetime
    if (!decodeUnsignedInteger(stream, outHeader.lifetime)) return false;

    size_t readSoFar = 8;

    // Optional Fragmentation Fields
    if (outHeader.isFragment()) {
        if (!decodeUnsignedInteger(stream, outHeader.fragmentOffset)) return false;
        if (!decodeUnsignedInteger(stream, outHeader.totalAppDataLength)) return false;
        readSoFar += 2;
    }

    // Discard any remaining fields in Primary Block (e.g. CRC field)
    while (readSoFar < primaryElemCount) {
        if (!skipCborItem(stream)) return false;
        readSoFar++;
    }

    // ------------------------------------------------------------------------
    // Locate and Parse Payload Block (Block Type 1)
    // ------------------------------------------------------------------------
    size_t b = 1;
    while (bundleIndefinite || b < totalBlocks) {
        int nextByte = stream.read();
        if (nextByte < 0) return false;

        // If outer bundle array is indefinite (0x9F), 0xFF denotes the Break stop code
        if (bundleIndefinite && nextByte == 0xFF) {
            break;
        }

        // Decode the block array header starting with nextByte
        uint8_t majorType = static_cast<uint8_t>((nextByte >> 5) & 0x07);
        uint8_t addInfo = static_cast<uint8_t>(nextByte & 0x1F);
        if (majorType != 4) {
            return false;
        }

        size_t blockElems = 0;
        if (addInfo < 24) {
            blockElems = addInfo;
        } else if (addInfo == 24) {
            int bVal = stream.read();
            if (bVal < 0) return false;
            blockElems = static_cast<uint8_t>(bVal);
        } else if (addInfo == 25) {
            uint8_t buf[2];
            if (!readExactBytes(stream, buf, 2)) return false;
            blockElems = (static_cast<size_t>(buf[0]) << 8) | buf[1];
        } else {
            return false;
        }

        if (blockElems < 5) {
            return false;
        }

        uint64_t blockType = 0;
        if (!decodeUnsignedInteger(stream, blockType)) return false;

        if (blockType == 1) {
            // Payload Block Found!
            uint64_t blockNbr = 0;
            uint64_t blockFlags = 0;
            uint64_t blockCrcType = 0;

            if (!decodeUnsignedInteger(stream, blockNbr)) return false;
            if (!decodeUnsignedInteger(stream, blockFlags)) return false;
            if (!decodeUnsignedInteger(stream, blockCrcType)) return false;

            // Optional Block CRC
            if (blockCrcType != 0) {
                if (!skipCborItem(stream)) return false;
            }

            // Payload ByteString Header
            bool payloadIndef = false;
            if (!decodeByteStringHeader(stream, outPayloadLength, payloadIndef)) return false;

            // Successfully positioned at the exact boundary of the payload bytes!
            return true;
        } else {
            // Extension Block: skip remaining elements
            for (size_t elem = 1; elem < blockElems; ++elem) {
                if (!skipCborItem(stream)) return false;
            }
        }
        b++;
    }

    return false; // No payload block discovered
}

} // namespace bpa
} // namespace muon
