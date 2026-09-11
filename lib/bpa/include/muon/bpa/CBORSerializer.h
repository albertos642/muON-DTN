/**
 * @file CBORSerializer.h
 * @brief RFC 8949 CBOR stream serializer and deserializer for BPv7 bundles.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_BPA_CBOR_SERIALIZER_H
#define MUON_BPA_CBOR_SERIALIZER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ggg/hal/IStream.h>
#include <ggg/hal/IStorage.h>
#include <muon/bpa/BundleTypes.h>

namespace muon {
namespace bpa {

/**
 * @brief Stream-based, Zero-Malloc CBOR (RFC 8949) serialization and parsing engine.
 * Specifically crafted for BPv7 (RFC 9171) operating over constrained embedded systems.
 * Reads and writes solely through ggg::hal::IInputStream and ggg::hal::IOutputStream.
 */
class CBORSerializer {
public:
    CBORSerializer() = default;

    // ========================================================================
    // RFC 8949 Primitive Encoders (Major Types)
    // ========================================================================

    // Encodes a major type and unsigned value (auto big-endian conversion)
    static bool encodeTypeAndValue(ggg::hal::IOutputStream& stream, uint8_t majorType, uint64_t value);

    // Major Type 0: Unsigned Integer
    static bool encodeUnsignedInteger(ggg::hal::IOutputStream& stream, uint64_t value);

    // Major Type 2: Byte String header
    static bool encodeByteStringHeader(ggg::hal::IOutputStream& stream, size_t length);

    // Major Type 2: Byte String with inline buffer write
    static bool encodeByteString(ggg::hal::IOutputStream& stream, const uint8_t* data, size_t length);

    // Major Type 4: Array header
    static bool encodeArrayHeader(ggg::hal::IOutputStream& stream, size_t numElements);

    // Encodes an RFC 9171 IPN Endpoint Identifier: [2, [nodeNbr, serviceNbr]]
    static bool encodeEID(ggg::hal::IOutputStream& stream, const IpnEndpointId& eid);

    // Encodes an RFC 9171 Creation Timestamp: [timestamp, sequenceNumber]
    static bool encodeCreationTimestamp(ggg::hal::IOutputStream& stream, uint64_t timestamp, uint64_t seqNo);

    // ========================================================================
    // RFC 8949 Primitive Decoders (Major Types)
    // ========================================================================

    // Reads initial byte, decomposing into major type and additional info
    static bool decodeInitialByte(ggg::hal::IInputStream& stream, uint8_t& outMajorType, uint8_t& outAdditionalInfo);

    // Reads and decodes a value with an expected major type
    static bool decodeTypeAndValue(ggg::hal::IInputStream& stream, uint8_t expectedMajorType, uint64_t& outValue);

    // Major Type 0: Unsigned Integer
    static bool decodeUnsignedInteger(ggg::hal::IInputStream& stream, uint64_t& outValue);

    // Major Type 4: Array header
    static bool decodeArrayHeader(ggg::hal::IInputStream& stream, size_t& outNumElements, bool& outIndefinite);

    // Major Type 2: Byte String header
    static bool decodeByteStringHeader(ggg::hal::IInputStream& stream, size_t& outLength, bool& outIndefinite);

    // Decodes an RFC 9171 IPN Endpoint Identifier: [2, [nodeNbr, serviceNbr]]
    static bool decodeEID(ggg::hal::IInputStream& stream, IpnEndpointId& outEid);

    // Decodes an RFC 9171 Creation Timestamp: [timestamp, sequenceNumber]
    static bool decodeCreationTimestamp(ggg::hal::IInputStream& stream, uint64_t& outTimestamp, uint64_t& outSeqNo);

    // Consumes and discards a single complete CBOR data item from stream (Zero-Malloc)
    static bool skipCborItem(ggg::hal::IInputStream& stream, int initialByte = -1);

    // ========================================================================
    // BPv7 High-Level Bundle Operations (RFC 9171)
    // ========================================================================

    /**
     * @brief Serializes a full BPv7 Bundle (Primary Block + Payload Block) to an output stream.
     * @param header The Primary Block fields.
     * @param payloadData Raw application data buffer.
     * @param payloadLength Length of payload data in bytes.
     * @param stream Destination output stream.
     * @return true on success.
     */
    static bool serializeBundle(const BundleHeader& header,
                                const uint8_t* payloadData,
                                size_t payloadLength,
                                ggg::hal::IOutputStream& stream);

    /**
     * @brief Serializes a full BPv7 Bundle by streaming payload data directly from an IStorage record.
     * Chunked reading ensures zero large RAM buffer allocations.
     * @param header The Primary Block fields.
     * @param payloadHandle Storage handle containing the raw payload.
     * @param storage The storage backend instance.
     * @param stream Destination output stream.
     * @return true on success.
     */
    static bool serializeBundleFromStorage(const BundleHeader& header,
                                          ggg::hal::StorageHandle_t payloadHandle,
                                          ggg::hal::IStorage& storage,
                                          ggg::hal::IOutputStream& stream);

    /**
     * @brief Deserializes Primary Block and locates Payload Block from an incoming stream.
     * Stops right at the start of payload data bytes without buffering them in RAM.
     * @param stream Input stream positioned at the start of the CBOR Bundle.
     * @param outHeader Populated with the Primary Block fields.
     * @param outPayloadLength Populated with the payload length in bytes.
     * @return true if bundle header was successfully validated.
     */
    static bool deserializeBundleHeader(ggg::hal::IInputStream& stream,
                                        BundleHeader& outHeader,
                                        size_t& outPayloadLength);
};

} // namespace bpa
} // namespace muon

#endif // MUON_BPA_CBOR_SERIALIZER_H
