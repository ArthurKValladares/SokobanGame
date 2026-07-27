#include "engine/render/PngWriter.hpp"

#include "engine/AtomicFile.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <string>

namespace sokoban {
namespace {

// --- Checksums --------------------------------------------------------------

[[nodiscard]] uint32_t crc32Of(
    const std::byte* data, std::size_t size, uint32_t crc = 0xFFFFFFFFu)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> result {};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) != 0u ? 0xEDB88320u ^ (value >> 1)
                                           : value >> 1;
            }
            result[i] = value;
        }
        return result;
    }();

    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ static_cast<uint8_t>(data[i])) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

[[nodiscard]] uint32_t adler32Of(const std::vector<uint8_t>& data)
{
    uint32_t a = 1;
    uint32_t b = 0;
    // 5552 is the largest run that cannot overflow the 32-bit accumulators.
    constexpr std::size_t chunk = 5552;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t end = std::min(offset + chunk, data.size());
        for (; offset < end; ++offset) {
            a += data[offset];
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}

// --- Bit writer (deflate is LSB-first) --------------------------------------

class BitWriter {
public:
    void writeBits(uint32_t value, int count)
    {
        for (int i = 0; i < count; ++i) {
            bitBuffer_ |= ((value >> i) & 1u) << bitCount_;
            if (++bitCount_ == 8) {
                bytes_.push_back(static_cast<uint8_t>(bitBuffer_));
                bitBuffer_ = 0;
                bitCount_ = 0;
            }
        }
    }

    // Huffman codes travel most-significant-bit first, unlike everything else
    // in the stream, so they are reversed into the LSB-first bit order.
    void writeCode(uint32_t code, int length)
    {
        for (int i = length - 1; i >= 0; --i) {
            writeBits((code >> i) & 1u, 1);
        }
    }

    void flush()
    {
        if (bitCount_ > 0) {
            bytes_.push_back(static_cast<uint8_t>(bitBuffer_));
            bitBuffer_ = 0;
            bitCount_ = 0;
        }
    }

    [[nodiscard]] std::vector<uint8_t>& bytes() { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
    uint32_t bitBuffer_ = 0;
    int bitCount_ = 0;
};

// --- Fixed Huffman deflate --------------------------------------------------

void writeFixedLiteral(BitWriter& writer, uint32_t symbol)
{
    // RFC 1951 section 3.2.6.
    if (symbol <= 143) {
        writer.writeCode(0x30 + symbol, 8);
    } else if (symbol <= 255) {
        writer.writeCode(0x190 + (symbol - 144), 9);
    } else if (symbol <= 279) {
        writer.writeCode(symbol - 256, 7);
    } else {
        writer.writeCode(0xC0 + (symbol - 280), 8);
    }
}

constexpr std::array<uint16_t, 29> lengthBase {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258,
};
constexpr std::array<uint8_t, 29> lengthExtra {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0,
};
constexpr std::array<uint16_t, 30> distanceBase {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
constexpr std::array<uint8_t, 30> distanceExtra {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

void writeMatch(BitWriter& writer, int length, int distance)
{
    std::size_t lengthCode = 0;
    while (lengthCode + 1 < lengthBase.size() &&
           lengthBase[lengthCode + 1] <= static_cast<uint16_t>(length)) {
        ++lengthCode;
    }
    writeFixedLiteral(writer, static_cast<uint32_t>(257 + lengthCode));
    writer.writeBits(
        static_cast<uint32_t>(length - lengthBase[lengthCode]),
        lengthExtra[lengthCode]);

    std::size_t distanceCode = 0;
    while (distanceCode + 1 < distanceBase.size() &&
           distanceBase[distanceCode + 1] <= static_cast<uint16_t>(distance)) {
        ++distanceCode;
    }
    // Distance codes use a fixed 5-bit code, also MSB-first.
    writer.writeCode(static_cast<uint32_t>(distanceCode), 5);
    writer.writeBits(
        static_cast<uint32_t>(distance - distanceBase[distanceCode]),
        distanceExtra[distanceCode]);
}

constexpr int minMatch = 3;
constexpr int maxMatch = 258;
constexpr int windowSize = 32768;
// Cap the hash chain walk so pathological inputs cannot make a save crawl.
constexpr int maxChainLength = 64;
constexpr std::size_t hashSize = 1u << 15;

[[nodiscard]] std::size_t hashAt(const std::vector<uint8_t>& data, std::size_t i)
{
    return ((static_cast<std::size_t>(data[i]) << 10) ^
               (static_cast<std::size_t>(data[i + 1]) << 5) ^
               static_cast<std::size_t>(data[i + 2])) &
        (hashSize - 1);
}

[[nodiscard]] std::vector<uint8_t> deflateFixed(const std::vector<uint8_t>& data)
{
    BitWriter writer;
    // Single final block, fixed Huffman: BFINAL=1, BTYPE=01.
    writer.writeBits(1, 1);
    writer.writeBits(1, 2);

    std::vector<int> head(hashSize, -1);
    std::vector<int> previous(data.size(), -1);

    std::size_t position = 0;
    while (position < data.size()) {
        int bestLength = 0;
        int bestDistance = 0;

        if (position + minMatch <= data.size()) {
            const std::size_t bucket = hashAt(data, position);
            int candidate = head[bucket];
            int chain = 0;
            while (candidate >= 0 && chain < maxChainLength) {
                const std::size_t candidatePosition =
                    static_cast<std::size_t>(candidate);
                const std::size_t distance = position - candidatePosition;
                if (distance == 0 || distance > windowSize) {
                    break;
                }
                int length = 0;
                const int limit = static_cast<int>(
                    std::min<std::size_t>(maxMatch, data.size() - position));
                while (length < limit &&
                    data[candidatePosition + static_cast<std::size_t>(length)] ==
                        data[position + static_cast<std::size_t>(length)]) {
                    ++length;
                }
                if (length > bestLength) {
                    bestLength = length;
                    bestDistance = static_cast<int>(distance);
                    if (length >= maxMatch) {
                        break;
                    }
                }
                candidate = previous[candidatePosition];
                ++chain;
            }
        }

        if (bestLength >= minMatch) {
            writeMatch(writer, bestLength, bestDistance);
            // Insert every position the match covers, so later matches can
            // start anywhere inside it.
            for (int i = 0; i < bestLength; ++i) {
                const std::size_t at = position + static_cast<std::size_t>(i);
                if (at + minMatch <= data.size()) {
                    const std::size_t bucket = hashAt(data, at);
                    previous[at] = head[bucket];
                    head[bucket] = static_cast<int>(at);
                }
            }
            position += static_cast<std::size_t>(bestLength);
        } else {
            writeFixedLiteral(writer, data[position]);
            if (position + minMatch <= data.size()) {
                const std::size_t bucket = hashAt(data, position);
                previous[position] = head[bucket];
                head[bucket] = static_cast<int>(position);
            }
            ++position;
        }
    }

    writeFixedLiteral(writer, 256); // end of block
    writer.flush();
    return std::move(writer.bytes());
}

[[nodiscard]] std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> stream;
    // CM=8 (deflate), CINFO=7 (32K window), no preset dictionary, and a check
    // value chosen so the two header bytes are a multiple of 31.
    stream.push_back(0x78);
    stream.push_back(0x01);

    const std::vector<uint8_t> compressed = deflateFixed(data);
    stream.insert(stream.end(), compressed.begin(), compressed.end());

    const uint32_t adler = adler32Of(data);
    stream.push_back(static_cast<uint8_t>((adler >> 24) & 0xFFu));
    stream.push_back(static_cast<uint8_t>((adler >> 16) & 0xFFu));
    stream.push_back(static_cast<uint8_t>((adler >> 8) & 0xFFu));
    stream.push_back(static_cast<uint8_t>(adler & 0xFFu));
    return stream;
}

// --- Scanline filtering -----------------------------------------------------

[[nodiscard]] uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c)
{
    const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    const int pa = std::abs(p - static_cast<int>(a));
    const int pb = std::abs(p - static_cast<int>(b));
    const int pc = std::abs(p - static_cast<int>(c));
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

// Filtering is what makes a splat map compress: smooth noise turns into runs
// of near-zero deltas. Each row picks the filter with the smallest sum of
// absolute differences, the heuristic the PNG spec itself recommends.
[[nodiscard]] std::vector<uint8_t> filterScanlines(
    uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels)
{
    const std::size_t stride = width;
    std::vector<uint8_t> filtered;
    filtered.reserve((stride + 1) * height);

    std::vector<uint8_t> candidate(stride);
    std::vector<uint8_t> best(stride);
    const std::vector<uint8_t> zeroRow(stride, 0);

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * stride;
        const uint8_t* previous =
            y == 0 ? zeroRow.data() : row - stride;

        uint8_t bestType = 0;
        std::size_t bestScore = std::numeric_limits<std::size_t>::max();
        for (uint8_t type = 0; type <= 4; ++type) {
            for (std::size_t x = 0; x < stride; ++x) {
                // One byte per pixel, so the "left" neighbour is x-1.
                const uint8_t left = x >= 1 ? row[x - 1] : 0;
                const uint8_t up = previous[x];
                const uint8_t upLeft = x >= 1 ? previous[x - 1] : 0;
                uint8_t value = 0;
                switch (type) {
                case 0: value = row[x]; break;
                case 1: value = static_cast<uint8_t>(row[x] - left); break;
                case 2: value = static_cast<uint8_t>(row[x] - up); break;
                case 3:
                    value = static_cast<uint8_t>(
                        row[x] - static_cast<uint8_t>(
                            (static_cast<int>(left) + static_cast<int>(up)) / 2));
                    break;
                default:
                    value = static_cast<uint8_t>(
                        row[x] - paethPredictor(left, up, upLeft));
                    break;
                }
                candidate[x] = value;
            }
            std::size_t score = 0;
            for (const uint8_t value : candidate) {
                // Treat bytes as signed deltas: values near 0 and near 255 are
                // both small changes and both compress well.
                score += value < 128 ? value : 256u - value;
            }
            if (score < bestScore) {
                bestScore = score;
                bestType = type;
                best = candidate;
            }
        }

        filtered.push_back(bestType);
        filtered.insert(filtered.end(), best.begin(), best.end());
    }
    return filtered;
}

// --- Chunks -----------------------------------------------------------------

void appendUint32(std::vector<std::byte>& out, uint32_t value)
{
    out.push_back(static_cast<std::byte>((value >> 24) & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::byte>(value & 0xFFu));
}

void appendChunk(
    std::vector<std::byte>& out,
    const char (&tag)[5],
    const std::vector<std::byte>& payload)
{
    appendUint32(out, static_cast<uint32_t>(payload.size()));
    const std::size_t crcStart = out.size();
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>(tag[i]));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    const uint32_t crc =
        crc32Of(out.data() + crcStart, out.size() - crcStart) ^ 0xFFFFFFFFu;
    appendUint32(out, crc);
}

} // namespace

std::vector<std::byte> encodeGrayscalePng(
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& pixels)
{
    if (width == 0 || height == 0) {
        throw std::runtime_error("Cannot encode a PNG with zero dimensions");
    }
    if (pixels.size() != static_cast<std::size_t>(width) * height) {
        throw std::runtime_error(
            "PNG pixel count does not match " + std::to_string(width) + "x" +
            std::to_string(height));
    }

    std::vector<std::byte> png {
        std::byte { 0x89 }, std::byte { 'P' }, std::byte { 'N' },
        std::byte { 'G' }, std::byte { '\r' }, std::byte { '\n' },
        std::byte { 0x1A }, std::byte { '\n' },
    };

    std::vector<std::byte> header;
    appendUint32(header, width);
    appendUint32(header, height);
    header.push_back(std::byte { 8 }); // bit depth
    header.push_back(std::byte { 0 }); // colour type: greyscale
    header.push_back(std::byte { 0 }); // compression: deflate
    header.push_back(std::byte { 0 }); // filter method: adaptive
    header.push_back(std::byte { 0 }); // interlace: none
    appendChunk(png, "IHDR", header);

    const std::vector<uint8_t> compressed =
        zlibCompress(filterScanlines(width, height, pixels));
    std::vector<std::byte> data(compressed.size());
    std::memcpy(data.data(), compressed.data(), compressed.size());
    appendChunk(png, "IDAT", data);

    appendChunk(png, "IEND", {});
    return png;
}

void writeGrayscalePng(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& pixels)
{
    const std::vector<std::byte> png = encodeGrayscalePng(width, height, pixels);

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error(
                "Failed to open for writing: " + temporary.string());
        }
        stream.write(
            reinterpret_cast<const char*>(png.data()),
            static_cast<std::streamsize>(png.size()));
        if (!stream) {
            throw std::runtime_error("Failed to write: " + temporary.string());
        }
    }
    atomicFile::replace(path, temporary);
}

} // namespace sokoban
