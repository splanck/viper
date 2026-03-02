//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgDeflate.cpp
// Purpose: Self-contained DEFLATE compression and decompression. Ported from
//          src/runtime/io/rt_compress.c with GC dependencies removed.
//
// Key invariants:
//   - All memory management uses malloc/free internally, results returned
//     as std::vector<uint8_t>.
//   - Thread-safe: no global mutable state except lazily-initialized fixed
//     Huffman trees (built once, read-only after init).
//
// Ownership/Lifetime:
//   - Internal buffers are freed before return or on exception.
//   - Returned vectors own their memory.
//
// Links: src/runtime/io/rt_compress.c (original), PkgDeflate.hpp (API)
//
//===----------------------------------------------------------------------===//

#include "PkgDeflate.hpp"

#include <cstdlib>
#include <cstring>

namespace viper::pkg {

//=============================================================================
// Constants
//=============================================================================

static constexpr int kDefaultLevel = 6;
static constexpr int kMinLevel = 1;
static constexpr int kMaxLevel = 9;

static constexpr int kWindowSize = 32768;
static constexpr int kWindowMask = 0x7FFF;
static constexpr int kMaxMatchLen = 258;
static constexpr int kMinMatchLen = 3;
static constexpr int kMaxDistance = 32768;

static constexpr int kMaxBits = 15;
static constexpr int kMaxLitCodes = 286;
static constexpr int kMaxDistCodes = 30;
static constexpr int kMaxCodeLenCodes = 19;

static constexpr int kFixedLitCodes = 288;
static constexpr int kFixedDistCodes = 32;

//=============================================================================
// Bit Stream Reader (for decompression)
//=============================================================================

struct BitReader {
    const uint8_t *data;
    size_t len;
    size_t pos;
    uint32_t buffer;
    int bitsInBuf;

    void init(const uint8_t *d, size_t l)
    {
        data = d;
        len = l;
        pos = 0;
        buffer = 0;
        bitsInBuf = 0;
    }

    bool fill(int n)
    {
        while (bitsInBuf < n) {
            if (pos >= len) {
                if (bitsInBuf > 0) {
                    bitsInBuf = n;
                    return true;
                }
                return false;
            }
            buffer |= static_cast<uint32_t>(data[pos++]) << bitsInBuf;
            bitsInBuf += 8;
        }
        return true;
    }

    uint32_t read(int n)
    {
        if (!fill(n))
            return 0;
        uint32_t val = buffer & ((1U << n) - 1);
        buffer >>= n;
        bitsInBuf -= n;
        return val;
    }

    uint32_t peek(int n)
    {
        fill(n);
        return buffer & ((1U << n) - 1);
    }

    void consume(int n)
    {
        buffer >>= n;
        bitsInBuf -= n;
    }

    void align()
    {
        buffer = 0;
        bitsInBuf = 0;
    }

    bool hasData() const { return pos < len || bitsInBuf > 0; }
};

//=============================================================================
// Bit Stream Writer (for compression)
//=============================================================================

struct BitWriter {
    uint8_t *data;
    size_t capacity;
    size_t len;
    uint32_t buffer;
    int bitsInBuf;

    void init(size_t initialCap)
    {
        capacity = initialCap > 256 ? initialCap : 256;
        data = static_cast<uint8_t *>(std::malloc(capacity));
        if (!data)
            throw DeflateError("deflate: memory allocation failed");
        len = 0;
        buffer = 0;
        bitsInBuf = 0;
    }

    void ensure(size_t need)
    {
        if (len + need > capacity) {
            size_t newCap = capacity * 2;
            if (newCap < len + need)
                newCap = len + need + 256;
            auto *newData = static_cast<uint8_t *>(std::realloc(data, newCap));
            if (!newData)
                throw DeflateError("deflate: out of memory");
            data = newData;
            capacity = newCap;
        }
    }

    void write(uint32_t val, int n)
    {
        buffer |= val << bitsInBuf;
        bitsInBuf += n;
        while (bitsInBuf >= 8) {
            ensure(1);
            data[len++] = buffer & 0xFF;
            buffer >>= 8;
            bitsInBuf -= 8;
        }
    }

    void flush()
    {
        if (bitsInBuf > 0) {
            ensure(1);
            data[len++] = buffer & 0xFF;
            buffer = 0;
            bitsInBuf = 0;
        }
    }

    void writeBytes(const uint8_t *src, size_t srcLen)
    {
        ensure(srcLen);
        std::memcpy(data + len, src, srcLen);
        len += srcLen;
    }

    void free()
    {
        std::free(data);
        data = nullptr;
        capacity = 0;
        len = 0;
    }

    /// @brief Move contents to vector and free internal buffer.
    std::vector<uint8_t> toVector()
    {
        std::vector<uint8_t> result(data, data + len);
        free();
        return result;
    }
};

//=============================================================================
// Huffman Tree
//=============================================================================

struct HuffmanTree {
    int maxCode;
    uint16_t *symbols;
    int tableBits;
    size_t tableSize;

    HuffmanTree() : maxCode(0), symbols(nullptr), tableBits(0), tableSize(0) {}
    ~HuffmanTree() { std::free(symbols); }

    HuffmanTree(const HuffmanTree &) = delete;
    HuffmanTree &operator=(const HuffmanTree &) = delete;

    bool build(const uint8_t *lengths, int numCodes)
    {
        int blCount[kMaxBits + 1] = {};
        for (int i = 0; i < numCodes; i++) {
            if (lengths[i] > kMaxBits)
                return false;
            blCount[lengths[i]]++;
        }
        blCount[0] = 0;

        uint16_t nextCode[kMaxBits + 1];
        uint16_t code = 0;
        for (int bits = 1; bits <= kMaxBits; bits++) {
            code = (code + blCount[bits - 1]) << 1;
            nextCode[bits] = code;
        }

        int maxLen = 0;
        for (int i = 0; i < numCodes; i++) {
            if (lengths[i] > maxLen)
                maxLen = lengths[i];
        }
        if (maxLen == 0)
            maxLen = 1;

        tableBits = maxLen;
        tableSize = 1U << tableBits;
        maxCode = numCodes;

        symbols = static_cast<uint16_t *>(
            std::calloc(tableSize + numCodes * 2, sizeof(uint16_t)));
        if (!symbols)
            return false;

        for (int i = 0; i < numCodes; i++) {
            if (lengths[i] == 0)
                continue;

            uint16_t symCode = nextCode[lengths[i]]++;
            int len = lengths[i];

            if (len <= tableBits) {
                uint16_t revCode = 0;
                for (int b = 0; b < len; b++) {
                    if (symCode & (1 << b))
                        revCode |= 1 << (len - 1 - b);
                }
                int fill = 1 << (tableBits - len);
                for (int j = 0; j < fill; j++) {
                    int idx = revCode | (j << len);
                    symbols[idx] = static_cast<uint16_t>((len << 12) | i);
                }
            }
        }
        return true;
    }

    int decode(BitReader &br) const
    {
        if (!br.fill(tableBits))
            return -1;
        uint32_t bits = br.peek(tableBits);
        uint16_t entry = symbols[bits];
        if (entry == 0)
            return -1;
        int len = entry >> 12;
        int symbol = entry & 0xFFF;
        if (len == 0)
            return -1;
        br.consume(len);
        return symbol;
    }
};

//=============================================================================
// Length and Distance Tables
//=============================================================================

static const int kLengthExtraBits[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

static const int kLengthBase[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

static const int kDistExtraBits[30] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static const int kDistBase[30] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

static const int kCodeLengthOrder[kMaxCodeLenCodes] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

//=============================================================================
// Fixed Huffman Trees (lazy init, thread-safe enough for our use)
//=============================================================================

static HuffmanTree *sFixedLitTree = nullptr;
static HuffmanTree *sFixedDistTree = nullptr;

static void initFixedTrees()
{
    if (sFixedLitTree)
        return;

    // Literal/length code lengths (RFC 1951 section 3.2.6)
    auto *lit = new HuffmanTree();
    uint8_t litLengths[kFixedLitCodes];
    for (int i = 0; i <= 143; i++) litLengths[i] = 8;
    for (int i = 144; i <= 255; i++) litLengths[i] = 9;
    for (int i = 256; i <= 279; i++) litLengths[i] = 7;
    for (int i = 280; i <= 287; i++) litLengths[i] = 8;
    lit->build(litLengths, kFixedLitCodes);

    // Distance code lengths (all 5 bits)
    auto *dist = new HuffmanTree();
    uint8_t distLengths[kFixedDistCodes];
    for (int i = 0; i < kFixedDistCodes; i++) distLengths[i] = 5;
    dist->build(distLengths, kFixedDistCodes);

    sFixedLitTree = lit;
    sFixedDistTree = dist;
}

//=============================================================================
// Output Buffer (for decompression)
//=============================================================================

static constexpr size_t kInflateMaxOutput = 256u * 1024u * 1024u;

struct OutputBuffer {
    uint8_t *data;
    size_t len;
    size_t capacity;

    void init(size_t initialCap)
    {
        capacity = initialCap > 256 ? initialCap : 256;
        data = static_cast<uint8_t *>(std::malloc(capacity));
        if (!data)
            throw DeflateError("inflate: memory allocation failed");
        len = 0;
    }

    void ensure(size_t need)
    {
        if (len + need > kInflateMaxOutput)
            throw DeflateError("inflate: output exceeds 256 MB limit");
        if (len + need > capacity) {
            size_t newCap = capacity * 2;
            if (newCap < len + need)
                newCap = len + need + 256;
            if (newCap > kInflateMaxOutput)
                newCap = kInflateMaxOutput;
            auto *newData = static_cast<uint8_t *>(std::realloc(data, newCap));
            if (!newData)
                throw DeflateError("inflate: out of memory");
            data = newData;
            capacity = newCap;
        }
    }

    void putByte(uint8_t b)
    {
        ensure(1);
        data[len++] = b;
    }

    void copyBack(int distance, int length)
    {
        ensure(length);
        size_t src = len - distance;
        for (int i = 0; i < length; i++)
            data[len++] = data[src++];
    }

    std::vector<uint8_t> toVector()
    {
        std::vector<uint8_t> result(data, data + len);
        std::free(data);
        data = nullptr;
        return result;
    }

    void free()
    {
        std::free(data);
        data = nullptr;
    }
};

//=============================================================================
// DEFLATE Decompression
//=============================================================================

static bool inflateStored(BitReader &br, OutputBuffer &out)
{
    br.align();
    if (br.pos + 4 > br.len)
        return false;

    uint16_t blockLen = br.data[br.pos] | (br.data[br.pos + 1] << 8);
    uint16_t nlen = br.data[br.pos + 2] | (br.data[br.pos + 3] << 8);
    br.pos += 4;

    if ((blockLen ^ nlen) != 0xFFFF)
        return false;
    if (br.pos + blockLen > br.len)
        return false;

    out.ensure(blockLen);
    std::memcpy(out.data + out.len, br.data + br.pos, blockLen);
    out.len += blockLen;
    br.pos += blockLen;
    return true;
}

static bool inflateHuffman(BitReader &br, OutputBuffer &out,
                           const HuffmanTree &litTree,
                           const HuffmanTree &distTree)
{
    while (true) {
        int sym = litTree.decode(br);
        if (sym < 0)
            return false;

        if (sym < 256) {
            out.putByte(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            return true;
        } else if (sym <= 285) {
            int lenIdx = sym - 257;
            int length = kLengthBase[lenIdx];
            int extra = kLengthExtraBits[lenIdx];
            if (extra > 0)
                length += br.read(extra);

            int distSym = distTree.decode(br);
            if (distSym < 0 || distSym >= 30)
                return false;

            int distance = kDistBase[distSym];
            extra = kDistExtraBits[distSym];
            if (extra > 0)
                distance += br.read(extra);

            if (distance > static_cast<int>(out.len))
                return false;

            out.copyBack(distance, length);
        } else {
            return false;
        }
    }
}

static bool inflateDynamic(BitReader &br, OutputBuffer &out)
{
    int hlit = br.read(5) + 257;
    int hdist = br.read(5) + 1;
    int hclen = br.read(4) + 4;

    if (hlit > kMaxLitCodes || hdist > kMaxDistCodes)
        return false;

    uint8_t clLengths[kMaxCodeLenCodes] = {};
    for (int i = 0; i < hclen; i++)
        clLengths[kCodeLengthOrder[i]] = br.read(3);

    HuffmanTree clTree;
    if (!clTree.build(clLengths, kMaxCodeLenCodes))
        return false;

    uint8_t lengths[kMaxLitCodes + kMaxDistCodes] = {};
    int totalCodes = hlit + hdist;
    int i = 0;

    while (i < totalCodes) {
        int sym = clTree.decode(br);
        if (sym < 0)
            return false;

        if (sym < 16) {
            lengths[i++] = sym;
        } else if (sym == 16) {
            if (i == 0)
                return false;
            int repeat = br.read(2) + 3;
            uint8_t prev = lengths[i - 1];
            while (repeat-- > 0 && i < totalCodes)
                lengths[i++] = prev;
        } else if (sym == 17) {
            int repeat = br.read(3) + 3;
            while (repeat-- > 0 && i < totalCodes)
                lengths[i++] = 0;
        } else if (sym == 18) {
            int repeat = br.read(7) + 11;
            while (repeat-- > 0 && i < totalCodes)
                lengths[i++] = 0;
        } else {
            return false;
        }
    }

    HuffmanTree litTree, distTree;
    if (!litTree.build(lengths, hlit))
        return false;
    if (!distTree.build(lengths + hlit, hdist))
        return false;

    return inflateHuffman(br, out, litTree, distTree);
}

static std::vector<uint8_t> inflateData(const uint8_t *data, size_t len)
{
    initFixedTrees();

    BitReader br;
    br.init(data, len);

    OutputBuffer out;
    out.init(len * 4);

    bool lastBlock = false;
    while (!lastBlock) {
        if (!br.hasData()) {
            out.free();
            throw DeflateError("inflate: unexpected end of data");
        }

        lastBlock = br.read(1);
        int blockType = br.read(2);

        bool ok = false;
        switch (blockType) {
        case 0: ok = inflateStored(br, out); break;
        case 1: ok = inflateHuffman(br, out, *sFixedLitTree, *sFixedDistTree); break;
        case 2: ok = inflateDynamic(br, out); break;
        default:
            out.free();
            throw DeflateError("inflate: invalid block type");
        }

        if (!ok) {
            out.free();
            throw DeflateError("inflate: invalid compressed data");
        }
    }

    return out.toVector();
}

//=============================================================================
// DEFLATE Compression
//=============================================================================

// LZ77 hash table
struct LZ77State {
    int *head;
    int *prev;

    static constexpr int kHashBits = 15;
    static constexpr int kHashSize = 1 << kHashBits;
    static constexpr int kHashMask = kHashSize - 1;
    static constexpr int kNil = -1;

    void init()
    {
        head = static_cast<int *>(std::malloc(kHashSize * sizeof(int)));
        prev = static_cast<int *>(std::malloc(kWindowSize * sizeof(int)));
        for (int i = 0; i < kHashSize; i++) head[i] = kNil;
        for (int i = 0; i < kWindowSize; i++) prev[i] = kNil;
    }

    void free()
    {
        std::free(head);
        std::free(prev);
    }

    static int computeHash(const uint8_t *d)
    {
        return ((d[0] << 10) ^ (d[1] << 5) ^ d[2]) & kHashMask;
    }

    int findMatch(const uint8_t *data, size_t pos, size_t len,
                  int maxChain, int *matchDist) const
    {
        if (pos + kMinMatchLen > len)
            return 0;

        int hash = computeHash(data + pos);
        int chainPos = head[hash];
        int bestLen = kMinMatchLen - 1;
        int bestDist = 0;

        int limit = static_cast<int>(pos) - kMaxDistance;
        if (limit < 0)
            limit = 0;

        while (chainPos >= limit && maxChain-- > 0) {
            int matchLen = 0;
            size_t a = pos, b = chainPos;
            size_t maxLen = len - pos;
            if (maxLen > kMaxMatchLen)
                maxLen = kMaxMatchLen;

            while (matchLen < static_cast<int>(maxLen) && data[a] == data[b]) {
                matchLen++;
                a++;
                b++;
            }

            if (matchLen > bestLen) {
                bestLen = matchLen;
                bestDist = static_cast<int>(pos - chainPos);
                if (bestLen >= kMaxMatchLen)
                    break;
            }

            chainPos = prev[chainPos & kWindowMask];
        }

        *matchDist = bestDist;
        return bestLen >= kMinMatchLen ? bestLen : 0;
    }

    void updateHash(const uint8_t *data, size_t pos)
    {
        int hash = computeHash(data + pos);
        prev[pos & kWindowMask] = head[hash];
        head[hash] = static_cast<int>(pos);
    }
};

static int getLengthCode(int length)
{
    for (int i = 0; i < 29; i++) {
        if (i == 28)
            return 285;
        if (length < kLengthBase[i + 1])
            return 257 + i;
    }
    return 285;
}

static int getDistCode(int dist)
{
    for (int i = 0; i < 30; i++) {
        if (i == 29 || dist < kDistBase[i + 1])
            return i;
    }
    return 29;
}

/// @brief Write bits in reverse order (LSB first as required by DEFLATE)
static void writeCode(BitWriter &bw, uint16_t code, int len)
{
    uint16_t rev = 0;
    for (int i = 0; i < len; i++) {
        if (code & (1 << i))
            rev |= 1 << (len - 1 - i);
    }
    bw.write(rev, len);
}

/// @brief Compress using stored blocks (no compression)
static void deflateStored(BitWriter &bw, const uint8_t *data, size_t len)
{
    if (len == 0) {
        bw.write(1, 1); // BFINAL = 1
        bw.write(0, 2); // BTYPE = stored
        bw.flush();
        bw.write(0x00, 8);
        bw.write(0x00, 8);
        bw.write(0xFF, 8);
        bw.write(0xFF, 8);
        return;
    }

    size_t pos = 0;
    while (pos < len) {
        size_t blockLen = len - pos;
        if (blockLen > 65535)
            blockLen = 65535;

        bool last = (pos + blockLen >= len);
        bw.write(last ? 1 : 0, 1); // BFINAL
        bw.write(0, 2);            // BTYPE = stored
        bw.flush();

        uint16_t nlen = ~static_cast<uint16_t>(blockLen);
        bw.write(blockLen & 0xFF, 8);
        bw.write((blockLen >> 8) & 0xFF, 8);
        bw.write(nlen & 0xFF, 8);
        bw.write((nlen >> 8) & 0xFF, 8);

        bw.flush();
        bw.writeBytes(data + pos, blockLen);
        pos += blockLen;
    }
}

/// @brief Compress using fixed Huffman codes with LZ77
static void deflateFixed(BitWriter &bw, const uint8_t *data, size_t len,
                         int level)
{
    initFixedTrees();

    LZ77State lz;
    lz.init();

    int maxChain = 4 << level;

    bw.write(1, 1); // BFINAL = 1
    bw.write(1, 2); // BTYPE = fixed Huffman

    size_t pos = 0;
    while (pos < len) {
        int matchDist = 0;
        int matchLen = 0;

        if (pos + kMinMatchLen <= len)
            matchLen = lz.findMatch(data, pos, len, maxChain, &matchDist);

        if (matchLen >= kMinMatchLen) {
            int lenCode = getLengthCode(matchLen);
            int lenIdx = lenCode - 257;

            if (lenCode <= 279)
                writeCode(bw, lenCode - 256, 7);
            else
                writeCode(bw, 0xC0 + (lenCode - 280), 8);

            if (kLengthExtraBits[lenIdx] > 0)
                bw.write(matchLen - kLengthBase[lenIdx], kLengthExtraBits[lenIdx]);

            int distCode = getDistCode(matchDist);
            writeCode(bw, distCode, 5);

            if (kDistExtraBits[distCode] > 0)
                bw.write(matchDist - kDistBase[distCode], kDistExtraBits[distCode]);

            for (int i = 0; i < matchLen; i++) {
                if (pos + i + kMinMatchLen <= len)
                    lz.updateHash(data, pos + i);
            }
            pos += matchLen;
        } else {
            uint8_t lit = data[pos];
            if (lit <= 143)
                writeCode(bw, 0x30 + lit, 8);
            else
                writeCode(bw, 0x190 + (lit - 144), 9);

            if (pos + kMinMatchLen <= len)
                lz.updateHash(data, pos);
            pos++;
        }
    }

    // End of block (code 256, 7 bits)
    writeCode(bw, 0, 7);

    lz.free();
}

static std::vector<uint8_t> deflateData(const uint8_t *data, size_t len,
                                        int level)
{
    if (level < kMinLevel) level = kMinLevel;
    if (level > kMaxLevel) level = kMaxLevel;

    BitWriter bw;
    bw.init(len);

    if (len <= 64 || level == 1)
        deflateStored(bw, data, len);
    else
        deflateFixed(bw, data, len, level);

    bw.flush();
    return bw.toVector();
}

//=============================================================================
// Public API
//=============================================================================

std::vector<uint8_t> deflate(const uint8_t *data, size_t len, int level)
{
    return deflateData(data, len, level);
}

std::vector<uint8_t> inflate(const uint8_t *data, size_t len)
{
    return inflateData(data, len);
}

} // namespace viper::pkg
