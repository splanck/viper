/// @brief Ensure the body→entity parallel arrays hold at least `need` slots,
///   reallocating (and copying existing entries) on growth; returns 0 on OOM.
static int game3d_world_grow_body_index(rt_game3d_world *world, int32_t need) {
    int32_t new_cap;
    if (!world)
        return 0;
    if (world->body_index_capacity >= need)
        return 1;
    if (!game3d_compute_capacity(world->body_index_capacity, need, 16, sizeof(void *), &new_cap))
        return 0;
    void **new_bodies = (void **)calloc((size_t)new_cap, sizeof(*new_bodies));
    rt_game3d_entity **new_entities =
        (rt_game3d_entity **)calloc((size_t)new_cap, sizeof(*new_entities));
    if (!new_bodies || !new_entities) {
        free(new_bodies);
        free(new_entities);
        return 0;
    }
    if (world->body_index_count > 0) {
        memcpy(new_bodies,
               world->body_index_bodies,
               (size_t)world->body_index_count * sizeof(*new_bodies));
        memcpy(new_entities,
               world->body_index_entities,
               (size_t)world->body_index_count * sizeof(*new_entities));
    }
    free(world->body_index_bodies);
    free(world->body_index_entities);
    world->body_index_bodies = new_bodies;
    world->body_index_entities = new_entities;
    world->body_index_capacity = new_cap;
    return 1;
}

/// @brief Remove the entry for physics `body` from the body→entity index, shifting
///   later entries down to keep the parallel arrays compact. No-op if absent.
static void game3d_world_body_index_remove(rt_game3d_world *world, void *body) {
    if (!world || !body)
        return;
    for (int32_t i = 0; i < world->body_index_count; ++i) {
        if (world->body_index_bodies[i] != body)
            continue;
        for (int32_t j = i; j < world->body_index_count - 1; ++j) {
            world->body_index_bodies[j] = world->body_index_bodies[j + 1];
            world->body_index_entities[j] = world->body_index_entities[j + 1];
        }
        world->body_index_count--;
        world->body_index_bodies[world->body_index_count] = NULL;
        world->body_index_entities[world->body_index_count] = NULL;
        return;
    }
}

/// @brief Index `entity` under its physics body for fast body→entity lookup,
///   replacing any prior entry for the same body; returns 0 on OOM (1 if no body).
static int game3d_world_body_index_add(rt_game3d_world *world, rt_game3d_entity *entity) {
    if (!world || !entity || !entity->body)
        return 1;
    game3d_world_body_index_remove(world, entity->body);
    if (!game3d_world_grow_body_index(world, world->body_index_count + 1))
        return 0;
    world->body_index_bodies[world->body_index_count] = entity->body;
    world->body_index_entities[world->body_index_count] = entity;
    world->body_index_count++;
    return 1;
}

/// @brief FNV-1a 64-bit hash of NUL-terminated `s`; never returns 0 so the value
///   can double as a non-empty-slot marker in the open-addressing name index.
static uint64_t game3d_hash_name(const char *s) {
    uint64_t hash = UINT64_C(1469598103934665603);
    if (!s)
        return 0;
    while (*s) {
        hash ^= (uint8_t)*s++;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}

/// @brief Ensure the name hash table has a power-of-two capacity above 2×`need`
///   (kept sparse to bound probe lengths); reallocates and resets to empty on growth.
/// @details Caller must repopulate via game3d_world_rebuild_name_index after a grow,
///   since growth discards existing slots (count is reset to 0). Returns 0 on OOM.
static int game3d_world_grow_name_index(rt_game3d_world *world, int32_t need) {
    int32_t new_cap = 16;
    if (!world)
        return 0;
    while (new_cap < need * 2) {
        if (new_cap > INT32_MAX / 2)
            return 0;
        new_cap *= 2;
    }
    if (world->name_index_capacity >= new_cap)
        return 1;
    uint64_t *new_hashes = (uint64_t *)calloc((size_t)new_cap, sizeof(*new_hashes));
    rt_game3d_entity **new_entities =
        (rt_game3d_entity **)calloc((size_t)new_cap, sizeof(*new_entities));
    if (!new_hashes || !new_entities) {
        free(new_hashes);
        free(new_entities);
        return 0;
    }
    free(world->name_index_hashes);
    free(world->name_index_entities);
    world->name_index_hashes = new_hashes;
    world->name_index_entities = new_entities;
    world->name_index_capacity = new_cap;
    world->name_index_count = 0;
    return 1;
}

/// @brief Clear all name-index slots without changing capacity.
static void game3d_world_clear_name_index(rt_game3d_world *world) {
    if (!world)
        return;
    if (world->name_index_capacity > 0 && world->name_index_hashes &&
        world->name_index_entities) {
        memset(world->name_index_hashes,
               0,
               (size_t)world->name_index_capacity * sizeof(*world->name_index_hashes));
        memset(world->name_index_entities,
               0,
               (size_t)world->name_index_capacity * sizeof(*world->name_index_entities));
    }
    world->name_index_count = 0;
}

/// @brief Insert `entity` into the name hash table by linear probing on its name hash.
/// @details Skips entities with no/empty name; the first entity wins a duplicate name
///   (a later same-name entity is ignored so find returns the earliest registrant).
static void game3d_world_name_index_insert(rt_game3d_world *world, rt_game3d_entity *entity) {
    const char *name;
    uint64_t hash;
    int32_t mask;
    if (!world || !entity || !entity->name || world->name_index_capacity <= 0)
        return;
    name = rt_string_cstr(entity->name);
    if (!name || !*name)
        return;
    hash = game3d_hash_name(name);
    mask = world->name_index_capacity - 1;
    for (int32_t probe = 0; probe < world->name_index_capacity; ++probe) {
        int32_t slot = (int32_t)((hash + (uint64_t)probe) & (uint64_t)mask);
        rt_game3d_entity *existing = world->name_index_entities[slot];
        if (!existing) {
            world->name_index_hashes[slot] = hash;
            world->name_index_entities[slot] = entity;
            world->name_index_count++;
            return;
        }
        if (world->name_index_hashes[slot] == hash && existing->name) {
            const char *existing_name = rt_string_cstr(existing->name);
            if (existing_name && strcmp(existing_name, name) == 0)
                return;
        }
    }
}

/// @brief Rebuild the entire name→entity hash table from the current entity registry.
/// @details Called after any spawn/despawn/rename mutates the entity set. Clears all
///   slots then re-inserts every entity; grows the table first when entity_count rises.
static void game3d_world_rebuild_name_index(rt_game3d_world *world) {
    if (!world)
        return;
    if (world->entity_count <= 0) {
        game3d_world_clear_name_index(world);
        return;
    }
    if (!game3d_world_grow_name_index(world, world->entity_count)) {
        game3d_world_clear_name_index(world);
        rt_trap("Game3D.World3D: name index allocation failed");
        return;
    }
    game3d_world_clear_name_index(world);
    for (int32_t i = 0; i < world->entity_count; ++i)
        game3d_world_name_index_insert(world, world->entities[i]);
}

/// @brief Look up an entity by name via the hash table; returns NULL if not present.
/// @details Linear-probes from the name's hash slot, comparing full strings on a hash
///   match; stops at the first empty slot (open-addressing absence proof).
static rt_game3d_entity *game3d_world_name_index_find(rt_game3d_world *world, const char *name) {
    uint64_t hash;
    int32_t mask;
    if (!world || !name || !*name || world->name_index_capacity <= 0)
        return NULL;
    hash = game3d_hash_name(name);
    mask = world->name_index_capacity - 1;
    for (int32_t probe = 0; probe < world->name_index_capacity; ++probe) {
        int32_t slot = (int32_t)((hash + (uint64_t)probe) & (uint64_t)mask);
        rt_game3d_entity *entity = world->name_index_entities[slot];
        if (!entity)
            return NULL;
        if (world->name_index_hashes[slot] == hash && entity->name) {
            const char *entity_name = rt_string_cstr(entity->name);
            if (entity_name && strcmp(entity_name, name) == 0)
                return entity;
        }
    }
    return NULL;
}
