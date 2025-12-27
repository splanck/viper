/**
 * @file journal.cpp
 * @brief Write-ahead logging (journaling) implementation for ViperFS.
 *
 * @details
 * Implements the journal manager for crash-consistent metadata updates.
 * The journal provides:
 * - Transaction-based metadata updates
 * - Replay on mount for crash recovery
 * - Write-ahead logging to ensure consistency
 *
 * Journal layout on disk:
 * [Header block] [Transaction 1 descriptor] [Block data...] [Commit] [Transaction 2...]
 *
 * Each transaction uses 1 + num_blocks + 1 journal blocks:
 * - 1 transaction descriptor block
 * - num_blocks data blocks
 * - 1 commit record block
 */

#include "journal.hpp"
#include "../../console/serial.hpp"
#include "../cache.hpp"

namespace fs::viperfs
{

// Global journal instance
static Journal g_journal;

Journal &journal()
{
    return g_journal;
}

bool journal_init(u64 journal_start, u64 num_blocks)
{
    return g_journal.init(journal_start, num_blocks);
}

u64 Journal::checksum(const void *data, usize len)
{
    const u8 *bytes = static_cast<const u8 *>(data);
    u64 sum = 0;
    for (usize i = 0; i < len; i++)
    {
        sum = (sum << 5) + sum + bytes[i]; // Simple hash
    }
    return sum;
}

bool Journal::read_header()
{
    CacheBlock *block = cache().get(journal_start_);
    if (!block)
    {
        serial::puts("[journal] Failed to read header block\n");
        return false;
    }

    const JournalHeader *hdr = reinterpret_cast<const JournalHeader *>(block->data);
    header_ = *hdr;
    cache().release(block);

    return true;
}

bool Journal::write_header()
{
    CacheBlock *block = cache().get(journal_start_);
    if (!block)
    {
        serial::puts("[journal] Failed to get header block for write\n");
        return false;
    }

    JournalHeader *hdr = reinterpret_cast<JournalHeader *>(block->data);
    *hdr = header_;
    block->dirty = true;
    cache().release(block);
    cache().sync_block(block);

    return true;
}

bool Journal::init(u64 journal_start, u64 num_blocks)
{
    if (num_blocks < 4)
    {
        serial::puts("[journal] Journal too small (need at least 4 blocks)\n");
        return false;
    }

    journal_start_ = journal_start;
    num_blocks_ = num_blocks;

    // Try to read existing journal header
    CacheBlock *block = cache().get(journal_start);
    if (!block)
    {
        serial::puts("[journal] Failed to read journal area\n");
        return false;
    }

    const JournalHeader *existing = reinterpret_cast<const JournalHeader *>(block->data);

    if (existing->magic == JOURNAL_MAGIC && existing->version == 1)
    {
        // Existing valid journal - load it
        header_ = *existing;
        cache().release(block);

        serial::puts("[journal] Found existing journal (seq=");
        serial::put_dec(header_.sequence);
        serial::puts(")\n");
    }
    else
    {
        // Initialize new journal
        cache().release(block);

        header_.magic = JOURNAL_MAGIC;
        header_.version = 1;
        header_.sequence = 0;
        header_.start_block = journal_start + 1; // Data area starts after header
        header_.num_blocks = num_blocks - 1;     // Minus header block
        header_.head = 0;
        header_.tail = 0;

        if (!write_header())
        {
            return false;
        }

        serial::puts("[journal] Initialized new journal (");
        serial::put_dec(num_blocks);
        serial::puts(" blocks)\n");
    }

    current_txn_.active = false;
    enabled_ = true;

    return true;
}

bool Journal::replay()
{
    if (!enabled_)
        return true;

    serial::puts("[journal] Checking for transactions to replay...\n");

    // Scan journal for committed but not completed transactions
    u64 pos = header_.head;
    u64 replayed = 0;

    while (pos != header_.tail && pos < header_.num_blocks)
    {
        u64 block_num = header_.start_block + pos;
        CacheBlock *block = cache().get(block_num);
        if (!block)
        {
            break;
        }

        const JournalTransaction *txn = reinterpret_cast<const JournalTransaction *>(block->data);

        // Check if this is a valid transaction
        if (txn->magic != JOURNAL_MAGIC || txn->state == txn_state::TXN_INVALID)
        {
            cache().release(block);
            break;
        }

        // Only replay committed transactions
        if (txn->state == txn_state::TXN_COMMITTED)
        {
            serial::puts("[journal] Replaying transaction seq=");
            serial::put_dec(txn->sequence);
            serial::puts("\n");

            // Replay each block in the transaction
            for (u8 i = 0; i < txn->num_blocks && i < MAX_JOURNAL_BLOCKS; i++)
            {
                u64 data_block = block_num + 1 + i;
                u64 dest_block = txn->blocks[i].block_num;

                CacheBlock *src = cache().get(data_block);
                if (!src)
                {
                    continue;
                }

                CacheBlock *dst = cache().get_for_write(dest_block);
                if (!dst)
                {
                    cache().release(src);
                    continue;
                }

                // Copy data to destination
                for (usize j = 0; j < BLOCK_SIZE; j++)
                {
                    dst->data[j] = src->data[j];
                }
                dst->dirty = true;

                cache().release(src);
                cache().release(dst);
            }

            replayed++;
        }

        // Move to next transaction
        // Transaction uses: 1 descriptor + num_blocks data + 1 commit = num_blocks + 2
        pos += txn->num_blocks + 2;
        cache().release(block);
    }

    if (replayed > 0)
    {
        serial::puts("[journal] Replayed ");
        serial::put_dec(replayed);
        serial::puts(" transaction(s)\n");

        // Sync replayed data
        cache().sync();

        // Clear journal
        header_.head = 0;
        header_.tail = 0;
        write_header();
    }
    else
    {
        serial::puts("[journal] No transactions to replay\n");
    }

    return true;
}

Transaction *Journal::begin()
{
    if (!enabled_)
        return nullptr;

    if (current_txn_.active)
    {
        serial::puts("[journal] Transaction already active\n");
        return nullptr;
    }

    current_txn_.sequence = header_.sequence++;
    current_txn_.num_blocks = 0;
    current_txn_.active = true;

    return &current_txn_;
}

bool Journal::log_block(Transaction *txn, u64 block_num, const void *data)
{
    if (!txn || !txn->active)
        return false;

    if (txn->num_blocks >= MAX_JOURNAL_BLOCKS)
    {
        serial::puts("[journal] Transaction full\n");
        return false;
    }

    // Check if this block is already logged in this transaction
    for (u8 i = 0; i < txn->num_blocks; i++)
    {
        if (txn->blocks[i] == block_num)
        {
            // Update existing entry
            const u8 *src = static_cast<const u8 *>(data);
            for (usize j = 0; j < BLOCK_SIZE; j++)
            {
                txn->data[i][j] = src[j];
            }
            return true;
        }
    }

    // Add new block to transaction
    u8 idx = txn->num_blocks;
    txn->blocks[idx] = block_num;

    const u8 *src = static_cast<const u8 *>(data);
    for (usize i = 0; i < BLOCK_SIZE; i++)
    {
        txn->data[idx][i] = src[i];
    }

    txn->num_blocks++;
    return true;
}

bool Journal::write_transaction(Transaction *txn, u64 *journal_pos)
{
    if (!txn || txn->num_blocks == 0)
        return false;

    // Check if there's enough space in journal
    u64 space_needed = txn->num_blocks + 2; // descriptor + data + commit
    u64 available = header_.num_blocks - header_.tail;

    if (available < space_needed)
    {
        // Journal is full - need to wrap or compact
        // For now, reset the journal (simple approach)
        header_.head = 0;
        header_.tail = 0;
    }

    *journal_pos = header_.tail;
    u64 block_num = header_.start_block + header_.tail;

    // Write transaction descriptor
    CacheBlock *desc_block = cache().get_for_write(block_num);
    if (!desc_block)
        return false;

    JournalTransaction *desc = reinterpret_cast<JournalTransaction *>(desc_block->data);
    desc->magic = JOURNAL_MAGIC;
    desc->state = txn_state::TXN_ACTIVE;
    desc->num_blocks = txn->num_blocks;
    desc->sequence = txn->sequence;
    desc->timestamp = 0; // TODO: add timestamp

    for (u8 i = 0; i < txn->num_blocks; i++)
    {
        desc->blocks[i].block_num = txn->blocks[i];
        desc->blocks[i].checksum = checksum(txn->data[i], BLOCK_SIZE);
    }

    desc_block->dirty = true;
    cache().sync_block(desc_block);
    cache().release(desc_block);

    // Write data blocks
    for (u8 i = 0; i < txn->num_blocks; i++)
    {
        CacheBlock *data_block = cache().get_for_write(block_num + 1 + i);
        if (!data_block)
            return false;

        for (usize j = 0; j < BLOCK_SIZE; j++)
        {
            data_block->data[j] = txn->data[i][j];
        }
        data_block->dirty = true;
        cache().sync_block(data_block);
        cache().release(data_block);
    }

    header_.tail += txn->num_blocks + 1; // descriptor + data blocks

    return true;
}

bool Journal::write_commit(Transaction *txn, u64 journal_pos)
{
    u64 commit_block = header_.start_block + header_.tail;

    CacheBlock *block = cache().get_for_write(commit_block);
    if (!block)
        return false;

    JournalCommit *commit = reinterpret_cast<JournalCommit *>(block->data);
    commit->magic = JOURNAL_MAGIC;
    commit->sequence = txn->sequence;
    commit->checksum = 0; // TODO: calculate full transaction checksum

    block->dirty = true;
    cache().sync_block(block);
    cache().release(block);

    header_.tail++;

    // Update transaction state to committed
    u64 desc_block_num = header_.start_block + journal_pos;
    CacheBlock *desc_block = cache().get_for_write(desc_block_num);
    if (desc_block)
    {
        JournalTransaction *desc = reinterpret_cast<JournalTransaction *>(desc_block->data);
        desc->state = txn_state::TXN_COMMITTED;
        desc_block->dirty = true;
        cache().sync_block(desc_block);
        cache().release(desc_block);
    }

    return true;
}

bool Journal::commit(Transaction *txn)
{
    if (!txn || !txn->active)
        return false;

    if (txn->num_blocks == 0)
    {
        // Empty transaction - just close it
        txn->active = false;
        return true;
    }

    u64 journal_pos = 0;

    // Write transaction data to journal
    if (!write_transaction(txn, &journal_pos))
    {
        serial::puts("[journal] Failed to write transaction\n");
        abort(txn);
        return false;
    }

    // Write commit record
    if (!write_commit(txn, journal_pos))
    {
        serial::puts("[journal] Failed to write commit\n");
        abort(txn);
        return false;
    }

    // Update and sync header
    write_header();

    txn->active = false;

    return true;
}

void Journal::abort(Transaction *txn)
{
    if (txn)
    {
        txn->active = false;
        txn->num_blocks = 0;
    }
}

bool Journal::complete(Transaction *txn)
{
    // Mark transaction as complete (can be reclaimed)
    // In this simple implementation, we don't track completion separately
    // The journal is reset when it fills up
    (void)txn;
    return true;
}

void Journal::sync()
{
    if (enabled_)
    {
        write_header();
    }
}

} // namespace fs::viperfs
