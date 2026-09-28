/* Moonstone 2026 - OFS integrity checks. GPL-3.0; see LICENSE.
 * Check reachable filesystem blocks, not unused or protection-track bytes.
 * Input images are immutable and exactly one standard DD ADF in size. */
#ifndef MOON_ADF_VALIDATE_H
#define MOON_ADF_VALIDATE_H

static int ofs_checksum_ok(const uint8_t *block) {
    uint32_t sum = 0;
    for (unsigned i = 0; i < 512; i += 4) sum += ofs_be32(block + i);
    return sum == 0;
}

static int adf_ofs_validate(const uint8_t *adf, long size, char *reason, size_t cap) {
    enum { BLOCKS = ADF_BYTES / 512 };
    uint8_t seen[BLOCKS] = {0};
    uint32_t pending[BLOCKS];
    unsigned count = 0;
#define ADF_BAD(...) do { snprintf(reason, cap, __VA_ARGS__); return 0; } while (0)
#define ADF_PUSH(b) do { uint32_t next_ = (b); if (next_) { \
    if (count == BLOCKS) ADF_BAD("too many directory links"); \
    pending[count++] = next_; } } while (0)
    if (!adf || size != ADF_BYTES) ADF_BAD("incorrect disk size: %ld bytes", size);
    if (memcmp(adf, "DOS", 3)) ADF_BAD("unsupported disk format: no AmigaDOS signature");
    if (adf[3] & 1) ADF_BAD("unsupported FFS filesystem; this game requires OFS");
    const uint8_t *root = adf + 880 * 512;
    if (ofs_be32(root) != 2 || ofs_be32(root + 508) != 1)
        ADF_BAD("invalid OFS root directory at block 880");
    if (!ofs_checksum_ok(root)) ADF_BAD("checksum mismatch in root directory block 880");
    seen[880] = 1;
    for (unsigned i = 0; i < 72; i++) ADF_PUSH(ofs_be32(root + 24 + i * 4));
    while (count) {
        uint32_t header = pending[--count];
        if (header < 2 || header >= BLOCKS) ADF_BAD("directory block %u is outside the disk", header);
        if (seen[header]) ADF_BAD("cyclic or repeated directory block %u", header);
        seen[header] = 1;
        const uint8_t *h = adf + header * 512;
        int32_t kind = (int32_t)ofs_be32(h + 508);
        if (ofs_be32(h) != 2 || (kind != -3 && kind != 2))
            ADF_BAD("invalid OFS file/directory header at block %u", header);
        if (h[432] > 30) ADF_BAD("invalid file name in header block %u", header);
        char name[31];
        memcpy(name, h + 433, h[432]); name[h[432]] = 0;
        if (!ofs_checksum_ok(h)) ADF_BAD("checksum mismatch in header block %u ('%s')", header, name);
        ADF_PUSH(ofs_be32(h + 496)); /* next entry in this directory hash bucket */
        if (kind == 2) {
            for (unsigned i = 0; i < 72; i++) ADF_PUSH(ofs_be32(h + 24 + i * 4));
            continue;
        }
        uint32_t length = ofs_be32(h + 324), got = 0, sequence = 1;
        uint32_t block = ofs_be32(h + 16);
        if (length > ADF_BYTES) ADF_BAD("invalid declared size of '%s': %u bytes", name, length);
        while (got < length) {
            if (block < 2 || block >= BLOCKS || seen[block])
                ADF_BAD("incomplete or cyclic file chain: recovered %u of %u bytes (block %u, '%s')", got, length, block, name);
            seen[block] = 1;
            const uint8_t *d = adf + block * 512;
            uint32_t bytes = ofs_be32(d + 12);
            if (ofs_be32(d) != 8 || !bytes || bytes > 488 || bytes > length - got)
                ADF_BAD("invalid OFS data block %u ('%s'): payload %u bytes", block, name, bytes);
            if (!ofs_checksum_ok(d)) ADF_BAD("checksum mismatch in data block %u ('%s')", block, name);
            if (ofs_be32(d + 4) != header || ofs_be32(d + 8) != sequence)
                ADF_BAD("incorrect file owner or sequence in data block %u ('%s')", block, name);
            got += bytes; sequence++; block = ofs_be32(d + 16);
        }
        if (block) ADF_BAD("file chain continues past the declared size of '%s' (block %u)", name, block);
        /* Long files also have linked extension headers. Their pointer tables
         * are filesystem metadata even though this game's OFS reader streams
         * the next-data links instead. Never interpret unused sectors as these. */
        block = ofs_be32(h + 504);
        while (block) {
            if (block < 2 || block >= BLOCKS || seen[block])
                ADF_BAD("invalid or repeated extension block %u ('%s')", block, name);
            seen[block] = 1;
            const uint8_t *e = adf + block * 512;
            if (ofs_be32(e) != 16 || (int32_t)ofs_be32(e + 508) != -3)
                ADF_BAD("invalid file extension at block %u ('%s')", block, name);
            if (!ofs_checksum_ok(e)) ADF_BAD("checksum mismatch in extension block %u ('%s')", block, name);
            block = ofs_be32(e + 504);
        }
    }
    reason[0] = 0;
    return 1;
#undef ADF_PUSH
#undef ADF_BAD
}
#endif
