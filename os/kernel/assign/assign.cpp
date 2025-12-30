/**
 * @file assign.cpp
 * @brief Implementation of the Assign name-to-directory mapping system.
 *
 * @details
 * Implements the v0.2.0 Assign subsystem declared in `assign.hpp`. The assign
 * table provides a simple mapping from logical names (e.g. `SYS`) to directory
 * inodes that can be used as the base for path traversal.
 *
 * The implementation uses:
 * - A fixed-size table of @ref AssignEntry entries.
 * - Case-insensitive name matching.
 * - Optional multi-directory assigns implemented as a linked chain of entries.
 *
 * When resolving paths, fresh DirObject or FileObject handles are created
 * and inserted into the caller's cap_table.
 */

#include "assign.hpp"
#include "../cap/rights.hpp"
#include "../cap/table.hpp"
#include "../console/console.hpp"
#include "../fs/viperfs/format.hpp"
#include "../fs/viperfs/viperfs.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../kobj/dir.hpp"
#include "../kobj/file.hpp"
#include "../lib/str.hpp"
#include "../viper/viper.hpp"

namespace viper::assign
{

// Global assign table
static AssignEntry assign_table[MAX_ASSIGNS];
static int assign_count = 0;

// Use lib::strlen and lib::strncpy for string operations

/**
 * @brief Case-insensitive ASCII string equality.
 *
 * @details
 * Compares two NUL-terminated strings using ASCII case folding for A–Z. This
 * is sufficient for assign names, which are intended to be short identifiers.
 *
 * @param a First string.
 * @param b Second string.
 * @return `true` if strings are equal ignoring ASCII case, otherwise `false`.
 */
static bool str_eq_nocase(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

// Find assign by name (case-insensitive)
/**
 * @brief Find an active assign entry by name.
 *
 * @details
 * Scans the assign table for an active entry whose name matches `name`
 * case-insensitively. For multi-directory assigns, this returns the head entry
 * stored in the table (the chain nodes share the same name).
 *
 * @param name Assign name without a colon.
 * @return Pointer to the matching entry, or `nullptr` if not found.
 */
static AssignEntry *find_assign(const char *name)
{
    for (int i = 0; i < MAX_ASSIGNS; i++)
    {
        if (assign_table[i].active && str_eq_nocase(assign_table[i].name, name))
        {
            return &assign_table[i];
        }
    }
    return nullptr;
}

// Find free slot
/**
 * @brief Find an unused slot in the assign table.
 *
 * @details
 * Returns the first entry that is not marked active. The returned entry is not
 * initialized; callers must fill out fields and set @ref AssignEntry::active.
 *
 * @return Pointer to a free table entry, or `nullptr` if the table is full.
 */
static AssignEntry *find_free_slot()
{
    for (int i = 0; i < MAX_ASSIGNS; i++)
    {
        if (!assign_table[i].active)
        {
            return &assign_table[i];
        }
    }
    return nullptr;
}

// Initialize the assign system
/** @copydoc viper::assign::init */
void init()
{
    // Clear table
    for (int i = 0; i < MAX_ASSIGNS; i++)
    {
        assign_table[i].active = false;
        assign_table[i].name[0] = '\0';
        assign_table[i].dir_inode = 0;
        assign_table[i].flags = ASSIGN_NONE;
        assign_table[i].next = nullptr;
    }
    assign_count = 0;

    // Create system assigns pointing to ViperFS root
    u64 root_inode = fs::viperfs::ROOT_INODE;

    // SYS: - Boot device root (ViperFS root directory)
    set("SYS", root_inode, ASSIGN_SYSTEM);
    console::print("[assign] SYS: -> root inode ");
    console::print_dec(static_cast<i64>(root_inode));
    console::print("\n");

    // D0: - Physical drive 0 (same as SYS for now)
    set("D0", root_inode, ASSIGN_SYSTEM);
    console::print("[assign] D0:  -> root inode ");
    console::print_dec(static_cast<i64>(root_inode));
    console::print("\n");

    console::print("[assign] Assign system initialized\n");
}

/** @copydoc viper::assign::setup_standard_assigns */
void setup_standard_assigns()
{
    console::print("[assign] Setting up standard Amiga-style assigns...\n");

    // Standard directory mappings (lowercase on disk, uppercase assign names)
    struct StandardAssign
    {
        const char *name; // Assign name (e.g., "C")
        const char *path; // Filesystem path (e.g., "/c")
    };

    static const StandardAssign standard_assigns[] = {
        {"C", "/c"},     // Commands directory
        {"S", "/s"},     // Startup-sequence scripts
        {"L", "/l"},     // Shared libraries
        {"T", "/t"},     // Temporary files
        {"CERTS", "/certs"}, // Certificate store
    };

    for (const auto &sa : standard_assigns)
    {
        u64 ino = fs::vfs::resolve_path(sa.path);
        if (ino != 0)
        {
            set(sa.name, ino, ASSIGN_SYSTEM);
            console::print("[assign] ");
            console::print(sa.name);
            console::print(":  -> ");
            console::print(sa.path);
            console::print(" (inode ");
            console::print_dec(static_cast<i64>(ino));
            console::print(")\n");
        }
        else
        {
            console::print("[assign] ");
            console::print(sa.name);
            console::print(": skipped (");
            console::print(sa.path);
            console::print(" not found)\n");
        }
    }
}

// Set an assign
/** @copydoc viper::assign::set */
AssignError set(const char *name, u64 dir_inode, u32 flags)
{
    if (!name || name[0] == '\0')
    {
        return AssignError::InvalidName;
    }

    usize name_len = lib::strlen(name);
    if (name_len > MAX_ASSIGN_NAME)
    {
        return AssignError::InvalidName;
    }

    // Check if already exists
    AssignEntry *entry = find_assign(name);
    if (entry)
    {
        // Cannot modify system assigns
        if (entry->flags & ASSIGN_SYSTEM)
        {
            return AssignError::ReadOnly;
        }
        // Update existing
        entry->dir_inode = dir_inode;
        entry->flags = flags;
        return AssignError::OK;
    }

    // Find free slot
    entry = find_free_slot();
    if (!entry)
    {
        return AssignError::TableFull;
    }

    // Create new entry
    lib::strncpy(entry->name, name, MAX_ASSIGN_NAME + 1);
    entry->dir_inode = dir_inode;
    entry->flags = flags;
    entry->next = nullptr;
    entry->active = true;
    assign_count++;

    return AssignError::OK;
}

// Set an assign from a directory handle
/** @copydoc viper::assign::set_from_handle */
AssignError set_from_handle(const char *name, Handle dir_handle, u32 flags)
{
    // Look up the handle in the current viper's cap_table
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return AssignError::InvalidHandle;
    }

    cap::Entry *entry = ct->get_checked(dir_handle, cap::Kind::Directory);
    if (!entry)
    {
        return AssignError::InvalidHandle;
    }

    // Get the DirObject and extract its inode
    kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);
    u64 inode = dir->inode_num();

    return set(name, inode, flags);
}

// Add to multi-directory assign
/** @copydoc viper::assign::add */
AssignError add(const char *name, u64 dir_inode)
{
    AssignEntry *entry = find_assign(name);

    if (!entry)
    {
        // Create new assign with MULTI flag
        AssignError err = set(name, dir_inode, ASSIGN_MULTI);
        return err;
    }

    if (entry->flags & ASSIGN_SYSTEM)
    {
        return AssignError::ReadOnly;
    }

    // Add to chain
    entry->flags |= ASSIGN_MULTI;

    // Find end of chain and add new entry
    AssignEntry *tail = entry;
    while (tail->next)
    {
        tail = tail->next;
    }

    AssignEntry *new_entry = find_free_slot();
    if (!new_entry)
    {
        return AssignError::TableFull;
    }

    lib::strncpy(new_entry->name, name, MAX_ASSIGN_NAME + 1);
    new_entry->dir_inode = dir_inode;
    new_entry->flags = ASSIGN_MULTI;
    new_entry->next = nullptr;
    new_entry->active = true;
    tail->next = new_entry;
    assign_count++;

    return AssignError::OK;
}

// Remove an assign
/** @copydoc viper::assign::remove */
AssignError remove(const char *name)
{
    AssignEntry *entry = find_assign(name);

    if (!entry)
    {
        return AssignError::NotFound;
    }

    if (entry->flags & ASSIGN_SYSTEM)
    {
        return AssignError::ReadOnly;
    }

    // Remove entire chain for multi-directory assigns
    while (entry)
    {
        AssignEntry *next = entry->next;
        entry->active = false;
        entry->name[0] = '\0';
        entry->dir_inode = 0;
        entry->next = nullptr;
        assign_count--;
        entry = next;
    }

    return AssignError::OK;
}

// Get assign inode by name
/** @copydoc viper::assign::get_inode */
u64 get_inode(const char *name)
{
    AssignEntry *entry = find_assign(name);
    if (entry)
    {
        return entry->dir_inode;
    }
    return 0;
}

// Get assign as a directory handle in the caller's cap_table
/** @copydoc viper::assign::get */
Handle get(const char *name)
{
    AssignEntry *entry = find_assign(name);
    if (!entry)
    {
        return cap::HANDLE_INVALID;
    }

    // Get current viper's cap_table
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return cap::HANDLE_INVALID;
    }

    // Create a DirObject for the assign's directory
    kobj::DirObject *dir = kobj::DirObject::create(entry->dir_inode);
    if (!dir)
    {
        return cap::HANDLE_INVALID;
    }

    // Insert into cap_table with read/traverse rights
    cap::Rights rights = cap::CAP_READ | cap::CAP_TRAVERSE;
    cap::Handle h = ct->insert(dir, cap::Kind::Directory, rights);
    if (h == cap::HANDLE_INVALID)
    {
        delete dir;
        return cap::HANDLE_INVALID;
    }

    return h;
}

// Check if assign exists
/** @copydoc viper::assign::exists */
bool exists(const char *name)
{
    return find_assign(name) != nullptr;
}

// Check if assign is system
/** @copydoc viper::assign::is_system */
bool is_system(const char *name)
{
    AssignEntry *entry = find_assign(name);
    if (entry)
    {
        return (entry->flags & ASSIGN_SYSTEM) != 0;
    }
    return false;
}

// List all assigns
/** @copydoc viper::assign::list */
int list(AssignInfo *buffer, int max_count)
{
    int count = 0;

    for (int i = 0; i < MAX_ASSIGNS && count < max_count; i++)
    {
        if (assign_table[i].active)
        {
            // Skip chain entries (they're part of multi-dir assigns)
            bool is_chain_entry = false;
            for (int j = 0; j < MAX_ASSIGNS; j++)
            {
                if (j != i && assign_table[j].active && assign_table[j].next == &assign_table[i])
                {
                    is_chain_entry = true;
                    break;
                }
            }
            if (is_chain_entry)
                continue;

            lib::strncpy(buffer[count].name, assign_table[i].name, 32);
            // Store inode as handle for now (syscall will create real handle if needed)
            buffer[count].handle = static_cast<u32>(assign_table[i].dir_inode);
            buffer[count].flags = assign_table[i].flags;
            count++;
        }
    }

    return count;
}

// Parse assign name from path
/** @copydoc viper::assign::parse_assign */
bool parse_assign(const char *path, char *assign_out, const char **remainder_out)
{
    if (!path)
        return false;

    // Find the colon
    const char *colon = path;
    while (*colon && *colon != ':')
    {
        colon++;
    }

    if (*colon != ':')
    {
        // No colon found - relative path
        return false;
    }

    usize name_len = colon - path;
    if (name_len == 0 || name_len > MAX_ASSIGN_NAME)
    {
        return false;
    }

    // Copy assign name
    for (usize i = 0; i < name_len; i++)
    {
        assign_out[i] = path[i];
    }
    assign_out[name_len] = '\0';

    // Set remainder (skip the colon)
    *remainder_out = colon + 1;
    return true;
}

/**
 * @brief Check whether a character is treated as a path separator.
 *
 * @details
 * The Assign system accepts both Unix (`/`) and Windows (`\\`) path separators
 * because assign-style paths are often typed interactively and may originate
 * from different conventions.
 *
 * @param c Character to test.
 * @return `true` if `c` is a separator, otherwise `false`.
 */
static inline bool is_separator(char c)
{
    return c == '/' || c == '\\';
}

// Resolve a path with assigns
/** @copydoc viper::assign::resolve_path */
Handle resolve_path(const char *path, u32 flags)
{
    if (!path)
        return cap::HANDLE_INVALID;

    char assign_name[MAX_ASSIGN_NAME + 1];
    const char *remainder = nullptr;

    if (!parse_assign(path, assign_name, &remainder))
    {
        // No assign prefix - would need current directory
        // For now, return invalid
        return cap::HANDLE_INVALID;
    }

    // Look up the assign's base inode
    u64 base_inode = get_inode(assign_name);
    if (base_inode == 0)
    {
        return cap::HANDLE_INVALID;
    }

    // Get current viper's cap_table
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return cap::HANDLE_INVALID;
    }

    // If no remainder or empty remainder, return the assign directory itself
    if (!remainder || *remainder == '\0')
    {
        kobj::DirObject *dir = kobj::DirObject::create(base_inode);
        if (!dir)
            return cap::HANDLE_INVALID;

        cap::Rights rights = cap::CAP_READ | cap::CAP_TRAVERSE;
        cap::Handle h = ct->insert(dir, cap::Kind::Directory, rights);
        if (h == cap::HANDLE_INVALID)
        {
            delete dir;
        }
        return h;
    }

    // Skip any leading separators
    while (*remainder && is_separator(*remainder))
    {
        remainder++;
    }

    // If only separators, return base directory
    if (*remainder == '\0')
    {
        kobj::DirObject *dir = kobj::DirObject::create(base_inode);
        if (!dir)
            return cap::HANDLE_INVALID;

        cap::Rights rights = cap::CAP_READ | cap::CAP_TRAVERSE;
        cap::Handle h = ct->insert(dir, cap::Kind::Directory, rights);
        if (h == cap::HANDLE_INVALID)
        {
            delete dir;
        }
        return h;
    }

    // Current inode we're traversing from
    u64 current_ino = base_inode;

    // Walk path components
    const char *p = remainder;
    while (*p)
    {
        // Skip separators
        while (*p && is_separator(*p))
        {
            p++;
        }
        if (*p == '\0')
            break;

        // Find end of component
        const char *comp_start = p;
        while (*p && !is_separator(*p))
        {
            p++;
        }
        usize comp_len = p - comp_start;

        if (comp_len == 0)
            continue;

        // Read current directory inode
        fs::viperfs::Inode *dir_inode = fs::viperfs::viperfs().read_inode(current_ino);
        if (!dir_inode)
        {
            return cap::HANDLE_INVALID;
        }

        // Check that current inode is a directory
        if (!fs::viperfs::is_directory(dir_inode))
        {
            fs::viperfs::viperfs().release_inode(dir_inode);
            return cap::HANDLE_INVALID;
        }

        // Lookup the component in this directory
        u64 next_ino = fs::viperfs::viperfs().lookup(dir_inode, comp_start, comp_len);
        fs::viperfs::viperfs().release_inode(dir_inode);

        if (next_ino == 0)
        {
            // Component not found
            return cap::HANDLE_INVALID;
        }

        current_ino = next_ino;
    }

    // Create a handle for the final inode
    fs::viperfs::Inode *final_inode = fs::viperfs::viperfs().read_inode(current_ino);
    if (!final_inode)
    {
        return cap::HANDLE_INVALID;
    }

    bool is_dir = fs::viperfs::is_directory(final_inode);
    fs::viperfs::viperfs().release_inode(final_inode);

    if (is_dir)
    {
        // Create a DirObject
        kobj::DirObject *dir = kobj::DirObject::create(current_ino);
        if (!dir)
            return cap::HANDLE_INVALID;

        cap::Rights rights = cap::CAP_READ | cap::CAP_TRAVERSE;
        cap::Handle h = ct->insert(dir, cap::Kind::Directory, rights);
        if (h == cap::HANDLE_INVALID)
        {
            delete dir;
        }
        return h;
    }
    else
    {
        // Create a FileObject
        kobj::FileObject *file = kobj::FileObject::create(current_ino, flags);
        if (!file)
            return cap::HANDLE_INVALID;

        // Determine rights based on open flags
        cap::Rights rights = cap::CAP_NONE;
        u32 access = flags & 0x3;
        if (access == kobj::file_flags::O_RDONLY || access == kobj::file_flags::O_RDWR)
        {
            rights = rights | cap::CAP_READ;
        }
        if (access == kobj::file_flags::O_WRONLY || access == kobj::file_flags::O_RDWR)
        {
            rights = rights | cap::CAP_WRITE;
        }

        cap::Handle h = ct->insert(file, cap::Kind::File, rights);
        if (h == cap::HANDLE_INVALID)
        {
            delete file;
        }
        return h;
    }
}

// Debug: print all assigns
/** @copydoc viper::assign::debug_dump */
void debug_dump()
{
    console::print("[assign] Active assigns:\n");
    for (int i = 0; i < MAX_ASSIGNS; i++)
    {
        if (assign_table[i].active)
        {
            console::print("  ");
            console::print(assign_table[i].name);
            console::print(": inode=");
            console::print_dec(static_cast<i64>(assign_table[i].dir_inode));
            if (assign_table[i].flags & ASSIGN_SYSTEM)
            {
                console::print(" [SYSTEM]");
            }
            if (assign_table[i].flags & ASSIGN_MULTI)
            {
                console::print(" [MULTI]");
            }
            console::print("\n");
        }
    }
}

} // namespace viper::assign
