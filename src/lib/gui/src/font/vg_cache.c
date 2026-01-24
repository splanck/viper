// vg_cache.c - Glyph cache implementation
#include "vg_ttf_internal.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Hash Function
//=============================================================================

static uint64_t make_cache_key(float size, uint32_t codepoint)
{
    // Quantize size to avoid floating point comparison issues
    uint32_t size_bits;
    memcpy(&size_bits, &size, sizeof(uint32_t));
    return ((uint64_t)size_bits << 32) | codepoint;
}

static size_t hash_key(uint64_t key, size_t bucket_count)
{
    // FNV-1a style mixing
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccd;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53;
    key ^= key >> 33;
    return (size_t)(key % bucket_count);
}

//=============================================================================
// Cache Creation/Destruction
//=============================================================================

vg_glyph_cache_t *vg_cache_create(void)
{
    vg_glyph_cache_t *cache = calloc(1, sizeof(vg_glyph_cache_t));
    if (!cache)
        return NULL;

    cache->bucket_count = VG_CACHE_INITIAL_SIZE;
    cache->buckets = calloc(cache->bucket_count, sizeof(vg_cache_entry_t *));
    if (!cache->buckets)
    {
        free(cache);
        return NULL;
    }

    return cache;
}

static void free_entry(vg_cache_entry_t *entry)
{
    if (entry->glyph.bitmap)
    {
        free(entry->glyph.bitmap);
    }
    free(entry);
}

void vg_cache_destroy(vg_glyph_cache_t *cache)
{
    if (!cache)
        return;

    // Free all entries
    for (size_t i = 0; i < cache->bucket_count; i++)
    {
        vg_cache_entry_t *entry = cache->buckets[i];
        while (entry)
        {
            vg_cache_entry_t *next = entry->next;
            free_entry(entry);
            entry = next;
        }
    }

    free(cache->buckets);
    free(cache);
}

//=============================================================================
// Cache Clear
//=============================================================================

void vg_cache_clear(vg_glyph_cache_t *cache)
{
    if (!cache)
        return;

    for (size_t i = 0; i < cache->bucket_count; i++)
    {
        vg_cache_entry_t *entry = cache->buckets[i];
        while (entry)
        {
            vg_cache_entry_t *next = entry->next;
            free_entry(entry);
            entry = next;
        }
        cache->buckets[i] = NULL;
    }

    cache->entry_count = 0;
    cache->memory_used = 0;
}

//=============================================================================
// Cache Resize
//=============================================================================

static bool cache_resize(vg_glyph_cache_t *cache)
{
    size_t new_count = cache->bucket_count * 2;
    if (new_count > VG_CACHE_MAX_SIZE)
    {
        new_count = VG_CACHE_MAX_SIZE;
        if (new_count == cache->bucket_count)
        {
            return false; // Already at max size
        }
    }

    vg_cache_entry_t **new_buckets = calloc(new_count, sizeof(vg_cache_entry_t *));
    if (!new_buckets)
        return false;

    // Rehash all entries
    for (size_t i = 0; i < cache->bucket_count; i++)
    {
        vg_cache_entry_t *entry = cache->buckets[i];
        while (entry)
        {
            vg_cache_entry_t *next = entry->next;
            size_t new_idx = hash_key(entry->key, new_count);
            entry->next = new_buckets[new_idx];
            new_buckets[new_idx] = entry;
            entry = next;
        }
    }

    free(cache->buckets);
    cache->buckets = new_buckets;
    cache->bucket_count = new_count;

    return true;
}

//=============================================================================
// Cache Eviction (LRU would be better, but simple random eviction for now)
//=============================================================================

static void cache_evict_some(vg_glyph_cache_t *cache)
{
    // Simple eviction: remove entries from first non-empty buckets
    int to_evict = cache->entry_count / 4; // Evict 25%
    if (to_evict < 1)
        to_evict = 1;

    for (size_t i = 0; i < cache->bucket_count && to_evict > 0; i++)
    {
        while (cache->buckets[i] && to_evict > 0)
        {
            vg_cache_entry_t *entry = cache->buckets[i];
            cache->buckets[i] = entry->next;

            cache->memory_used -= entry->glyph.width * entry->glyph.height;
            cache->entry_count--;
            to_evict--;

            free_entry(entry);
        }
    }
}

//=============================================================================
// Cache Get
//=============================================================================

const vg_glyph_t *vg_cache_get(vg_glyph_cache_t *cache, float size, uint32_t codepoint)
{
    if (!cache)
        return NULL;

    uint64_t key = make_cache_key(size, codepoint);
    size_t idx = hash_key(key, cache->bucket_count);

    vg_cache_entry_t *entry = cache->buckets[idx];
    while (entry)
    {
        if (entry->key == key)
        {
            return &entry->glyph;
        }
        entry = entry->next;
    }

    return NULL;
}

//=============================================================================
// Cache Put
//=============================================================================

void vg_cache_put(vg_glyph_cache_t *cache, float size, uint32_t codepoint, const vg_glyph_t *glyph)
{
    if (!cache || !glyph)
        return;

    uint64_t key = make_cache_key(size, codepoint);

    // Check if already exists
    size_t idx = hash_key(key, cache->bucket_count);
    vg_cache_entry_t *entry = cache->buckets[idx];
    while (entry)
    {
        if (entry->key == key)
        {
            return; // Already cached
        }
        entry = entry->next;
    }

    // Check memory limit and evict if necessary
    size_t glyph_memory = glyph->width * glyph->height;
    if (cache->memory_used + glyph_memory > VG_CACHE_MAX_MEMORY)
    {
        cache_evict_some(cache);
    }

    // Check load factor and resize if necessary
    if (cache->entry_count >= cache->bucket_count * 0.75)
    {
        cache_resize(cache);
        idx = hash_key(key, cache->bucket_count); // Recalculate after resize
    }

    // Create new entry
    entry = calloc(1, sizeof(vg_cache_entry_t));
    if (!entry)
        return;

    entry->key = key;
    entry->glyph = *glyph;

    // Copy bitmap if present
    if (glyph->bitmap && glyph->width > 0 && glyph->height > 0)
    {
        size_t bitmap_size = glyph->width * glyph->height;
        entry->glyph.bitmap = malloc(bitmap_size);
        if (entry->glyph.bitmap)
        {
            memcpy(entry->glyph.bitmap, glyph->bitmap, bitmap_size);
            cache->memory_used += bitmap_size;
        }
    }

    // Insert at head of bucket
    entry->next = cache->buckets[idx];
    cache->buckets[idx] = entry;
    cache->entry_count++;
}
