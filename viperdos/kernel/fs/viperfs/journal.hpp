//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../../lib/spinlock.hpp"
#include "../cache.hpp"
#include "format.hpp"

namespace fs::viperfs {

/**
 * @file journal.hpp
 * @brief Write-ahead logging (journaling) for ViperFS.
 *
 * @details
 * The journal provides crash consistency for metadata operations. Before any
 * metadata blocks are modified in-place, they are first written to the journal
 * along with a transaction record. Only after the transaction is committed can
 * the actual blocks be updated.
 *
 * On mount, the journal is replayed to recover any uncommitted transactions,
 * restoring the filesystem to a consistent state.
 *
 * Transaction flow:
 * 1. Call begin() to start a new transaction
 * 2. Call log_block() for each block to be modified
 * 3. Call commit() to finalize the transaction
 * 4. Modified blocks can now be written to their final locations
 *
 * The journal uses a circular buffer of blocks, allowing multiple transactions
 * to be in flight if needed (though current implementation is single-threaded).
 */

/**
 * @brief Active transaction handle.
 *
 * @details
 * Tracks the state of an in-progress transaction including which blocks
 * have been logged and the current write position in the journal.
 */
struct Transaction {
    u64 sequence;                            // Transaction sequence number
    u8 num_blocks;                           // Number of blocks logged so far
    u64 blocks[MAX_JOURNAL_BLOCKS];          // Block numbers logged
    u8 data[MAX_JOURNAL_BLOCKS][BLOCK_SIZE]; // Block data copies
    bool active;                             // Transaction is active
};

/**
 * @brief Filesystem journal manager.
 *
 * @details
 * Manages the write-ahead log for crash-consistent metadata updates.
 * The journal is stored at a fixed location on disk (after the data blocks)
 * and consists of:
 * - A header block with journal state
 * - Transaction records with block data
 * - Commit records marking transaction boundaries
 */
class Journal {
  public:
    /**
     * @brief Initialize the journal for a filesystem.
     *
     * @param journal_start Block number where journal begins.
     * @param num_blocks Number of blocks allocated for journal.
     * @return true on success, false on failure.
     */
    bool init(u64 journal_start, u64 num_blocks);

    /**
     * @brief Replay any committed but incomplete transactions.
     *
     * @details
     * Called during mount to recover from crashes. Scans the journal for
     * committed transactions and replays their blocks to their final
     * destinations.
     *
     * @return true if replay succeeded or no replay needed.
     */
    bool replay();

    /**
     * @brief Begin a new transaction.
     *
     * @details
     * Allocates space in the journal for a new transaction. Only one
     * transaction can be active at a time in the current implementation.
     *
     * @return Pointer to transaction handle, or nullptr if journal is full.
     */
    Transaction *begin();

    /**
     * @brief Log a block to the current transaction.
     *
     * @details
     * Records the block's current contents before modification. The block
     * data is copied to the transaction buffer and will be written to the
     * journal on commit.
     *
     * @param txn Active transaction.
     * @param block_num Block number being modified.
     * @param data Current block data (before modification).
     * @return true on success, false if transaction is full.
     */
    bool log_block(Transaction *txn, u64 block_num, const void *data);

    /**
     * @brief Commit a transaction to the journal.
     *
     * @details
     * Writes all logged blocks to the journal, then writes a commit record.
     * After this call returns successfully, the transaction is durable and
     * the actual blocks can be modified in-place.
     *
     * @param txn Transaction to commit.
     * @return true on success, false on I/O error.
     */
    bool commit(Transaction *txn);

    /**
     * @brief Abort a transaction.
     *
     * @details
     * Discards a transaction without committing. The logged blocks are not
     * written to the journal.
     *
     * @param txn Transaction to abort.
     */
    void abort(Transaction *txn);

    /**
     * @brief Mark a committed transaction as complete.
     *
     * @details
     * Called after the actual blocks have been written to their final
     * locations. Allows the journal space to be reclaimed.
     *
     * @param txn Transaction to complete.
     * @return true on success.
     */
    bool complete(Transaction *txn);

    /**
     * @brief Sync the journal header to disk.
     */
    void sync();

    /**
     * @brief Check if journaling is enabled.
     */
    bool is_enabled() const {
        return enabled_;
    }

    /**
     * @brief Get current transaction sequence number.
     */
    u64 sequence() const {
        return header_.sequence;
    }

  private:
    JournalHeader header_;    // In-memory copy of journal header
    u64 journal_start_;       // First block of journal area
    u64 num_blocks_;          // Total journal blocks
    bool enabled_{false};     // Journal is initialized
    Transaction current_txn_; // Current active transaction

    // Thread safety: protects transaction operations
    // This lock is held during begin/commit/abort to ensure only one
    // transaction is active at a time and header updates are atomic
    mutable Spinlock txn_lock_;

    /**
     * @brief Calculate simple checksum for block data.
     */
    u64 checksum(const void *data, usize len);

    /**
     * @brief Read the journal header from disk.
     */
    bool read_header();

    /**
     * @brief Write the journal header to disk.
     */
    bool write_header();

    /**
     * @brief Write transaction data to journal.
     */
    bool write_transaction(Transaction *txn, u64 *journal_pos);

    /**
     * @brief Write commit record to journal.
     */
    bool write_commit(Transaction *txn, u64 journal_pos);

    /**
     * @brief Replay a single transaction.
     */
    bool replay_transaction(u64 journal_pos);

    /**
     * @brief Abort without acquiring lock (caller must hold txn_lock_).
     */
    void abort_unlocked(Transaction *txn);

    // Replay helper methods
    bool verify_commit_record(u64 commit_block_num, u32 expected_seq);
    bool verify_transaction_checksums(u64 block_num, const JournalTransaction *txn);
    void apply_transaction_blocks(u64 block_num, const JournalTransaction *txn);
    bool validate_transaction_header(const JournalTransaction *txn, u64 pos);
};

/**
 * @brief Get the global journal instance.
 */
Journal &journal();

/**
 * @brief Initialize the global journal.
 *
 * @param journal_start Block number where journal begins.
 * @param num_blocks Number of blocks for journal.
 * @return true on success.
 */
bool journal_init(u64 journal_start, u64 num_blocks);

} // namespace fs::viperfs
