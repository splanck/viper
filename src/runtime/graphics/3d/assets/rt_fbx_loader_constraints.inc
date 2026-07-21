//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_fbx_loader_constraints.inc
// Purpose: Parse, validate, and evaluate bounded FBX transform and IK constraints.
// Key invariants:
//   - Numeric object IDs are the only graph identity; display names only bind dynamic weights.
//   - Active dependency cycles and malformed required roles reject the complete load transaction.
//   - Static and animated callers use the same finite world-pose evaluator.
// Ownership/Lifetime:
//   - fbx_constraint_set_t owns its constraints, source arrays, and stable evaluation order.
//   - fbx_constraint_pose_t owns budgeted scratch arrays and borrows parsed Model nodes.
// Links: rt_fbx_loader.c, rt_fbx_loader_scene.inc, ADR 0145
//
//===----------------------------------------------------------------------===//

typedef enum {
    FBX_CONSTRAINT_UNKNOWN = 0,
    FBX_CONSTRAINT_POSITION,
    FBX_CONSTRAINT_ROTATION,
    FBX_CONSTRAINT_SCALE,
    FBX_CONSTRAINT_PARENT,
    FBX_CONSTRAINT_AIM,
    FBX_CONSTRAINT_SINGLE_CHAIN_IK,
} fbx_constraint_kind_t;

typedef struct {
    int64_t model_id;
    double weight;
    double translation_offset[3];
    double rotation_offset[3];
    double scale_offset[3];
} fbx_constraint_source_t;

typedef struct {
    int64_t id;
    fbx_node_t *source_node;
    fbx_constraint_kind_t kind;
    int active;
    int locked;
    double weight;
    int64_t target_id;
    fbx_constraint_source_t *sources;
    int32_t source_count;
    int32_t source_capacity;
    int affect_translation[3];
    int affect_rotation[3];
    int affect_scale[3];
    double translation_offset[3];
    double rotation_offset[3];
    double scale_offset[3];
    int world_up_type;
    int64_t world_up_id;
    double world_up_vector[3];
    double aim_vector[3];
    double up_vector[3];
    int64_t ik_end_id;
    int64_t ik_effector_id;
    int64_t ik_pole_id;
    int pole_vector_type;
    double pole_vector[3];
    double twist;
    int warned_unsolvable;
} fbx_constraint_t;

typedef struct {
    fbx_constraint_t *entries;
    int32_t count;
    int32_t capacity;
    int32_t *order;
    fbx_load_context_t *context;
} fbx_constraint_set_t;

struct fbx_constraint_pose {
    int32_t count;
    int z_up;
    int64_t *ids;
    int32_t *parent_indices;
    fbx_node_t **sources;
    fbx_transform_components_t *components;
    double *local_matrices;
    double *global_matrices;
    fbx_model_id_entry_t *id_table;
    uint8_t *fixed_globals;
    uint8_t *evaluation_flags;
};

static void fbx_constraint_pose_free(fbx_constraint_pose_t *pose) {
    if (!pose)
        return;
    free(pose->ids);
    free(pose->parent_indices);
    free(pose->sources);
    free(pose->components);
    free(pose->local_matrices);
    free(pose->global_matrices);
    free(pose->id_table);
    free(pose->fixed_globals);
    free(pose->evaluation_flags);
    memset(pose, 0, sizeof(*pose));
}

static void fbx_constraint_set_free(fbx_constraint_set_t *set) {
    if (!set)
        return;
    for (int32_t i = 0; i < set->count; ++i)
        free(set->entries[i].sources);
    free(set->entries);
    free(set->order);
    memset(set, 0, sizeof(*set));
}

static fbx_constraint_kind_t fbx_constraint_kind_from_text(const char *text) {
    if (!text)
        return FBX_CONSTRAINT_UNKNOWN;
    if (fbx_ascii_equals_i(text, "Position From Positions") ||
        fbx_ascii_equals_i(text, "Position"))
        return FBX_CONSTRAINT_POSITION;
    if (fbx_ascii_equals_i(text, "Rotation From Rotations") ||
        fbx_ascii_equals_i(text, "Rotation"))
        return FBX_CONSTRAINT_ROTATION;
    if (fbx_ascii_equals_i(text, "Scale From Scales") || fbx_ascii_equals_i(text, "Scale"))
        return FBX_CONSTRAINT_SCALE;
    if (fbx_ascii_equals_i(text, "Parent-Child") || fbx_ascii_equals_i(text, "Parent"))
        return FBX_CONSTRAINT_PARENT;
    if (fbx_ascii_equals_i(text, "Aim"))
        return FBX_CONSTRAINT_AIM;
    if (fbx_ascii_equals_i(text, "Single Chain IK") ||
        fbx_ascii_equals_i(text, "SingleChainIK"))
        return FBX_CONSTRAINT_SINGLE_CHAIN_IK;
    return FBX_CONSTRAINT_UNKNOWN;
}

static const char *fbx_constraint_type_text(fbx_node_t *node) {
    fbx_node_t *type;
    if (!node)
        return "";
    if (node->prop_count >= 3 && fbx_prop_str(node, 2)[0])
        return fbx_prop_str(node, 2);
    type = fbx_find_child(node, "Type");
    return type && type->prop_count > 0 ? fbx_prop_str(type, 0) : "";
}

static int fbx_constraint_property_number(fbx_node_t *node,
                                          const char *name,
                                          double fallback,
                                          double *out,
                                          int *out_found) {
    fbx_node_t *property;
    double value;
    if (out_found)
        *out_found = 0;
    if (!out)
        return 0;
    *out = fallback;
    property = fbx_find_property70(node, name);
    if (property && property->prop_count >= 5) {
        value = fbx_prop_f64(property, 4);
        if (!isfinite(value))
            return 0;
        *out = value;
        if (out_found)
            *out_found = 1;
        return 1;
    }
    property = node ? fbx_find_child(node, name) : NULL;
    if (property && property->prop_count > 0) {
        value = fbx_prop_f64(property, 0);
        if (!isfinite(value))
            return 0;
        *out = value;
        if (out_found)
            *out_found = 1;
    }
    return 1;
}

static int fbx_constraint_property_xyz(fbx_node_t *node,
                                       const char *name,
                                       const double fallback[3],
                                       double out[3],
                                       int *out_found) {
    fbx_node_t *property;
    if (out_found)
        *out_found = 0;
    if (!out || !fallback)
        return 0;
    memcpy(out, fallback, 3u * sizeof(double));
    property = fbx_find_property70(node, name);
    if (property) {
        if (!fbx_read_p_xyz(property, out))
            return 0;
        if (out_found)
            *out_found = 1;
        return 1;
    }
    property = node ? fbx_find_child(node, name) : NULL;
    if (property) {
        if (property->prop_count < 3)
            return 0;
        for (int component = 0; component < 3; ++component) {
            out[component] = fbx_prop_f64(property, component);
            if (!isfinite(out[component]))
                return 0;
        }
        if (out_found)
            *out_found = 1;
    }
    return 1;
}

static int fbx_constraint_property_bool(fbx_node_t *node,
                                        const char *name,
                                        int fallback,
                                        int *out) {
    double value;
    int found = 0;
    if (!out || !fbx_constraint_property_number(node, name, fallback ? 1.0 : 0.0, &value, &found))
        return 0;
    *out = found ? value != 0.0 : fallback;
    return 1;
}

static int fbx_constraint_name_has_suffix(const char *name, const char *suffix) {
    size_t name_len;
    size_t suffix_len;
    if (!name || !suffix)
        return 0;
    name_len = strlen(name);
    suffix_len = strlen(suffix);
    return name_len > suffix_len &&
           fbx_ascii_equals_i(name + name_len - suffix_len, suffix);
}

static int fbx_constraint_property_matches_model(fbx_node_t *property,
                                                 fbx_node_t *model,
                                                 const char *suffix) {
    const char *model_name = "";
    size_t model_len = 0;
    const char *property_name;
    size_t suffix_len;
    size_t property_len;
    if (!property || !model || !suffix || property->prop_count < 1 ||
        !fbx_object_name_view(model, 1, &model_name, &model_len))
        return 0;
    property_name = fbx_prop_str(property, 0);
    suffix_len = strlen(suffix);
    property_len = strlen(property_name);
    if (model_len > SIZE_MAX - suffix_len || property_len != model_len + suffix_len)
        return 0;
    for (size_t i = 0; i < model_len; ++i) {
        unsigned char a = (unsigned char)property_name[i];
        unsigned char b = (unsigned char)model_name[i];
        if (a >= 'A' && a <= 'Z')
            a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = (unsigned char)(b + ('a' - 'A'));
        if (a != b)
            return 0;
    }
    return fbx_ascii_equals_i(property_name + model_len, suffix);
}

static fbx_node_t *fbx_constraint_find_model_property(fbx_node_t *constraint,
                                                      fbx_node_t *model,
                                                      const char *suffix) {
    fbx_node_t *properties = constraint ? fbx_find_child(constraint, "Properties70") : NULL;
    if (!properties)
        return NULL;
    for (int32_t i = 0; i < properties->child_count; ++i) {
        fbx_node_t *property = &properties->children[i];
        if (strcmp(property->name, "P") == 0 &&
            fbx_constraint_property_matches_model(property, model, suffix))
            return property;
    }
    return NULL;
}

static int fbx_constraint_append_source(fbx_constraint_t *constraint,
                                        int64_t model_id,
                                        fbx_load_context_t *context) {
    fbx_constraint_source_t *grown;
    int32_t capacity;
    if (!constraint || model_id == 0)
        return 0;
    for (int32_t i = 0; i < constraint->source_count; ++i)
        if (constraint->sources[i].model_id == model_id)
            return 0;
    if (constraint->source_count >= constraint->source_capacity) {
        if (constraint->source_capacity > INT32_MAX / 2)
            return 0;
        capacity = constraint->source_capacity == 0 ? 4 : constraint->source_capacity * 2;
        if ((size_t)capacity > SIZE_MAX / sizeof(*grown) ||
            !fbx_budget_charge(context,
                               (uint64_t)(capacity - constraint->source_capacity),
                               sizeof(*grown)))
            return 0;
        grown = (fbx_constraint_source_t *)realloc(
            constraint->sources, (size_t)capacity * sizeof(*grown));
        if (!grown)
            return 0;
        memset(grown + constraint->source_capacity,
               0,
               (size_t)(capacity - constraint->source_capacity) * sizeof(*grown));
        constraint->sources = grown;
        constraint->source_capacity = capacity;
    }
    fbx_constraint_source_t *source = &constraint->sources[constraint->source_count++];
    memset(source, 0, sizeof(*source));
    source->model_id = model_id;
    source->weight = 1.0;
    source->scale_offset[0] = source->scale_offset[1] = source->scale_offset[2] = 1.0;
    return 1;
}

static int fbx_constraint_role_equals(const char *role, const char *expected) {
    return role && expected && fbx_ascii_equals_i(role, expected);
}

static int fbx_constraint_bind_connection(fbx_constraint_t *constraint,
                                          fbx_node_t *objects,
                                          int64_t model_id,
                                          const char *role,
                                          fbx_load_context_t *context) {
    fbx_node_t *model;
    if (!constraint || !objects || !role || model_id == 0)
        return 1;
    model = fbx_find_object_by_id(objects, model_id);
    if (!model || strcmp(model->name, "Model") != 0)
        return 0;
    if (fbx_constraint_role_equals(role, "Constrained Object") ||
        fbx_constraint_role_equals(role, "ConstrainedObject") ||
        fbx_constraint_role_equals(role, "Constrained object (Child)")) {
        if (constraint->target_id != 0 && constraint->target_id != model_id)
            return 0;
        constraint->target_id = model_id;
        return 1;
    }
    if (fbx_constraint_role_equals(role, "First Joint") ||
        fbx_constraint_role_equals(role, "FirstJointObject")) {
        if (constraint->target_id != 0 && constraint->target_id != model_id)
            return 0;
        constraint->target_id = model_id;
        return 1;
    }
    if (fbx_constraint_role_equals(role, "End Joint") ||
        fbx_constraint_role_equals(role, "EndJointObject")) {
        if (constraint->ik_end_id != 0 && constraint->ik_end_id != model_id)
            return 0;
        constraint->ik_end_id = model_id;
        return 1;
    }
    if (fbx_constraint_role_equals(role, "Effector") ||
        fbx_constraint_role_equals(role, "EffectorObject")) {
        if (constraint->ik_effector_id != 0 && constraint->ik_effector_id != model_id)
            return 0;
        constraint->ik_effector_id = model_id;
        return 1;
    }
    if (fbx_constraint_role_equals(role, "World Up Object") ||
        fbx_constraint_role_equals(role, "WorldUpObject")) {
        if (constraint->world_up_id != 0 && constraint->world_up_id != model_id)
            return 0;
        constraint->world_up_id = model_id;
        return 1;
    }
    if (fbx_constraint_role_equals(role, "Pole Vector Object") ||
        fbx_constraint_role_equals(role, "PoleVectorObjects")) {
        if (constraint->ik_pole_id != 0 && constraint->ik_pole_id != model_id)
            return 0;
        constraint->ik_pole_id = model_id;
        return fbx_constraint_append_source(constraint, model_id, context);
    }
    if (fbx_constraint_role_equals(role, "Source") ||
        fbx_constraint_role_equals(role, "ConstraintSources") ||
        fbx_constraint_role_equals(role, "Source (Parent)") ||
        fbx_constraint_role_equals(role, "Aim At Object") ||
        fbx_constraint_role_equals(role, "AimAtObjects")) {
        return fbx_constraint_append_source(constraint, model_id, context);
    }
    return 1;
}

static int fbx_constraint_read_source_properties(fbx_constraint_t *constraint,
                                                 fbx_node_t *objects) {
    fbx_node_t *properties;
    int32_t dynamic_weight_count = 0;
    if (!constraint || !objects)
        return 0;
    properties = fbx_find_child(constraint->source_node, "Properties70");
    if (properties) {
        for (int32_t i = 0; i < properties->child_count; ++i) {
            fbx_node_t *property = &properties->children[i];
            const char *name;
            if (strcmp(property->name, "P") != 0 || property->prop_count < 1)
                continue;
            name = fbx_prop_str(property, 0);
            if (!fbx_ascii_equals_i(name, "Weight") &&
                fbx_constraint_name_has_suffix(name, ".Weight"))
                ++dynamic_weight_count;
        }
    }
    if (dynamic_weight_count != 0 && dynamic_weight_count != constraint->source_count)
        return 0;
    for (int32_t i = 0; i < constraint->source_count; ++i) {
        fbx_constraint_source_t *source = &constraint->sources[i];
        fbx_node_t *model = fbx_find_object_by_id(objects, source->model_id);
        fbx_node_t *weight_property =
            fbx_constraint_find_model_property(constraint->source_node, model, ".Weight");
        double divisor = constraint->kind == FBX_CONSTRAINT_SINGLE_CHAIN_IK ? 1.0 : 100.0;
        if (dynamic_weight_count > 0 && !weight_property)
            return 0;
        if (weight_property) {
            double weight;
            if (weight_property->prop_count < 5 ||
                !isfinite(weight = fbx_prop_f64(weight_property, 4)))
                return 0;
            source->weight = weight / divisor;
        }
        if (constraint->kind == FBX_CONSTRAINT_PARENT) {
            static const double zero[3] = {0.0, 0.0, 0.0};
            static const double one[3] = {1.0, 1.0, 1.0};
            fbx_node_t *property;
            property = fbx_constraint_find_model_property(
                constraint->source_node, model, ".Offset T");
            if (property && !fbx_read_p_xyz(property, source->translation_offset))
                return 0;
            if (!property)
                memcpy(source->translation_offset, zero, sizeof(zero));
            property = fbx_constraint_find_model_property(
                constraint->source_node, model, ".Offset R");
            if (property && !fbx_read_p_xyz(property, source->rotation_offset))
                return 0;
            if (!property)
                memcpy(source->rotation_offset, zero, sizeof(zero));
            property = fbx_constraint_find_model_property(
                constraint->source_node, model, ".Offset S");
            if (property && !fbx_read_p_xyz(property, source->scale_offset))
                return 0;
            if (!property)
                memcpy(source->scale_offset, one, sizeof(one));
        }
    }
    return 1;
}

static int fbx_constraint_read_common_properties(fbx_constraint_t *constraint) {
    static const double zero[3] = {0.0, 0.0, 0.0};
    static const double one[3] = {1.0, 1.0, 1.0};
    double weight;
    double enum_value;
    int found_translation = 0;
    int found_scaling = 0;
    if (!constraint ||
        !fbx_constraint_property_bool(constraint->source_node, "Active", 1, &constraint->active) ||
        !fbx_constraint_property_bool(constraint->source_node, "Lock", 0, &constraint->locked) ||
        !fbx_constraint_property_number(
            constraint->source_node, "Weight", 100.0, &weight, NULL) ||
        !fbx_constraint_property_xyz(constraint->source_node,
                                     "Translation",
                                     zero,
                                     constraint->translation_offset,
                                     &found_translation) ||
        !fbx_constraint_property_xyz(constraint->source_node,
                                     "Rotation",
                                     zero,
                                     constraint->rotation_offset,
                                     NULL) ||
        !fbx_constraint_property_xyz(constraint->source_node,
                                     "Scaling",
                                     one,
                                     constraint->scale_offset,
                                     &found_scaling))
        return 0;
    if (!found_translation &&
        !fbx_constraint_property_xyz(constraint->source_node,
                                     "TranslationOffset",
                                     zero,
                                     constraint->translation_offset,
                                     NULL))
        return 0;
    if (!found_scaling) {
        int found_scaling_offset = 0;
        if (!fbx_constraint_property_xyz(constraint->source_node,
                                         "ScalingOffset",
                                         one,
                                         constraint->scale_offset,
                                         &found_scaling_offset) ||
            (!found_scaling_offset &&
             !fbx_constraint_property_xyz(constraint->source_node,
                                          "ScaleOffset",
                                          one,
                                          constraint->scale_offset,
                                          NULL)))
            return 0;
    }
    constraint->weight = fbx_clamp_double(weight / 100.0, 0.0, 1.0, 1.0);
    for (int axis = 0; axis < 3; ++axis) {
        const char suffix = (char)('X' + axis);
        char name[32];
        snprintf(name, sizeof(name), "Affect%c", suffix);
        if (!fbx_constraint_property_bool(
                constraint->source_node, name, 1, &constraint->affect_translation[axis]) ||
            !fbx_constraint_property_bool(
                constraint->source_node, name, 1, &constraint->affect_rotation[axis]) ||
            !fbx_constraint_property_bool(
                constraint->source_node, name, 1, &constraint->affect_scale[axis]))
            return 0;
        snprintf(name, sizeof(name), "AffectTranslation%c", suffix);
        if (constraint->kind == FBX_CONSTRAINT_PARENT &&
            !fbx_constraint_property_bool(
                constraint->source_node, name, 1, &constraint->affect_translation[axis]))
            return 0;
        snprintf(name, sizeof(name), "AffectRotation%c", suffix);
        if (constraint->kind == FBX_CONSTRAINT_PARENT &&
            !fbx_constraint_property_bool(
                constraint->source_node, name, 1, &constraint->affect_rotation[axis]))
            return 0;
        snprintf(name, sizeof(name), "AffectScale%c", suffix);
        if (constraint->kind == FBX_CONSTRAINT_PARENT &&
            !fbx_constraint_property_bool(
                constraint->source_node, name, 0, &constraint->affect_scale[axis]))
            return 0;
    }
    constraint->aim_vector[0] = 1.0;
    constraint->up_vector[1] = 1.0;
    constraint->world_up_vector[1] = 1.0;
    if (!fbx_constraint_property_xyz(constraint->source_node,
                                     "RotationOffset",
                                     constraint->rotation_offset,
                                     constraint->rotation_offset,
                                     NULL) ||
        !fbx_constraint_property_xyz(constraint->source_node,
                                     "AimVector",
                                     constraint->aim_vector,
                                     constraint->aim_vector,
                                     NULL) ||
        !fbx_constraint_property_xyz(constraint->source_node,
                                     "UpVector",
                                     constraint->up_vector,
                                     constraint->up_vector,
                                     NULL) ||
        !fbx_constraint_property_xyz(constraint->source_node,
                                     "WorldUpVector",
                                     constraint->world_up_vector,
                                     constraint->world_up_vector,
                                     NULL) ||
        !fbx_constraint_property_xyz(constraint->source_node,
                                     "PoleVector",
                                     zero,
                                     constraint->pole_vector,
                                     NULL) ||
        !fbx_constraint_property_number(
            constraint->source_node, "WorldUpType", 0.0, &enum_value, NULL))
        return 0;
    constraint->world_up_type = (int)enum_value;
    if (constraint->world_up_type < 0 || constraint->world_up_type > 4)
        return 0;
    if (!fbx_constraint_property_number(
            constraint->source_node, "PoleVectorType", 0.0, &enum_value, NULL))
        return 0;
    constraint->pole_vector_type = (int)enum_value;
    if (constraint->pole_vector_type < 0 || constraint->pole_vector_type > 1)
        return 0;
    if (!fbx_constraint_property_number(
            constraint->source_node, "Twist", 0.0, &constraint->twist, NULL))
        return 0;
    return 1;
}

static int fbx_constraint_model_parent_id(fbx_node_t *objects,
                                          const fbx_conn_table_t *ct,
                                          int64_t model_id,
                                          int64_t *out_parent_id) {
    int64_t parent_id = fbx_find_parent(ct, model_id);
    if (parent_id != 0) {
        fbx_node_t *parent = fbx_find_object_by_id(objects, parent_id);
        if (!parent || strcmp(parent->name, "Model") != 0)
            parent_id = 0;
    }
    if (out_parent_id)
        *out_parent_id = parent_id;
    return 1;
}

static int fbx_constraint_ik_chain_valid(const fbx_constraint_t *constraint,
                                         fbx_node_t *objects,
                                         const fbx_conn_table_t *ct) {
    int64_t current;
    int32_t guard = 0;
    if (!constraint || constraint->target_id == 0 || constraint->ik_end_id == 0 ||
        constraint->ik_effector_id == 0)
        return 0;
    current = constraint->ik_end_id;
    while (current != 0 && guard++ <= objects->child_count) {
        int64_t parent_id = 0;
        if (current == constraint->target_id)
            return 1;
        fbx_constraint_model_parent_id(objects, ct, current, &parent_id);
        current = parent_id;
    }
    return 0;
}

static int fbx_constraint_writes_model(const fbx_constraint_t *constraint,
                                       fbx_node_t *objects,
                                       const fbx_conn_table_t *ct,
                                       int64_t model_id) {
    if (!constraint || !constraint->active || model_id == 0)
        return 0;
    if (constraint->kind != FBX_CONSTRAINT_SINGLE_CHAIN_IK)
        return constraint->target_id == model_id;
    for (int64_t current = constraint->ik_end_id, guard = 0;
         current != 0 && guard++ <= objects->child_count;) {
        int64_t parent_id = 0;
        if (current == model_id)
            return 1;
        if (current == constraint->target_id)
            break;
        fbx_constraint_model_parent_id(objects, ct, current, &parent_id);
        current = parent_id;
    }
    return 0;
}

static int fbx_constraint_affects_model_global(const fbx_constraint_t *producer,
                                               fbx_node_t *objects,
                                               const fbx_conn_table_t *ct,
                                               int64_t model_id) {
    for (int64_t current = model_id, guard = 0;
         current != 0 && guard++ <= objects->child_count;) {
        int64_t parent_id = 0;
        if (fbx_constraint_writes_model(producer, objects, ct, current))
            return 1;
        fbx_constraint_model_parent_id(objects, ct, current, &parent_id);
        current = parent_id;
    }
    return 0;
}

static int fbx_constraint_depends_on(const fbx_constraint_t *consumer,
                                     const fbx_constraint_t *producer,
                                     fbx_node_t *objects,
                                     const fbx_conn_table_t *ct) {
    int64_t target_parent = 0;
    if (!consumer || !producer || consumer == producer || !consumer->active ||
        !producer->active || consumer->kind == FBX_CONSTRAINT_UNKNOWN ||
        producer->kind == FBX_CONSTRAINT_UNKNOWN)
        return 0;
    for (int32_t source = 0; source < consumer->source_count; ++source)
        if (fbx_constraint_affects_model_global(
                producer, objects, ct, consumer->sources[source].model_id))
            return 1;
    {
        const int64_t role_ids[] = {consumer->world_up_id,
                                    consumer->ik_effector_id,
                                    consumer->ik_pole_id};
        for (size_t role = 0; role < sizeof(role_ids) / sizeof(role_ids[0]); ++role)
            if (role_ids[role] != 0 && fbx_constraint_affects_model_global(
                                              producer, objects, ct, role_ids[role]))
                return 1;
    }
    fbx_constraint_model_parent_id(objects, ct, consumer->target_id, &target_parent);
    return target_parent != 0 &&
           fbx_constraint_affects_model_global(producer, objects, ct, target_parent);
}

static int fbx_constraint_has_self_dependency(const fbx_constraint_t *constraint,
                                              fbx_node_t *objects,
                                              const fbx_conn_table_t *ct) {
    if (!constraint || !constraint->active || constraint->kind == FBX_CONSTRAINT_UNKNOWN)
        return 0;
    for (int32_t source = 0; source < constraint->source_count; ++source)
        if (fbx_constraint_affects_model_global(
                constraint, objects, ct, constraint->sources[source].model_id))
            return 1;
    {
        const int64_t role_ids[] = {constraint->world_up_id,
                                    constraint->ik_effector_id,
                                    constraint->ik_pole_id};
        for (size_t role = 0; role < sizeof(role_ids) / sizeof(role_ids[0]); ++role)
            if (role_ids[role] != 0 && fbx_constraint_affects_model_global(
                                              constraint, objects, ct, role_ids[role]))
                return 1;
    }
    return 0;
}

static int fbx_constraint_build_order(fbx_constraint_set_t *set,
                                      fbx_node_t *objects,
                                      const fbx_conn_table_t *ct) {
    int32_t *indegree;
    uint8_t *placed;
    int32_t output_count = 0;
    if (!set || set->count == 0)
        return 1;
    if ((size_t)set->count > SIZE_MAX / sizeof(*set->order) ||
        !fbx_budget_charge(set->context, (uint64_t)set->count, sizeof(*set->order)) ||
        !fbx_budget_charge(set->context, (uint64_t)set->count, sizeof(*indegree)) ||
        !fbx_budget_charge(set->context, (uint64_t)set->count, sizeof(*placed)))
        return 0;
    set->order = (int32_t *)malloc((size_t)set->count * sizeof(*set->order));
    indegree = (int32_t *)calloc((size_t)set->count, sizeof(*indegree));
    placed = (uint8_t *)calloc((size_t)set->count, sizeof(*placed));
    if (!set->order || !indegree || !placed) {
        free(indegree);
        free(placed);
        return 0;
    }
    for (int32_t to = 0; to < set->count; ++to) {
        fbx_constraint_t *consumer = &set->entries[to];
        if (!consumer->active || consumer->kind == FBX_CONSTRAINT_UNKNOWN)
            continue;
        for (int32_t from = 0; from < set->count; ++from) {
            fbx_constraint_t *producer = &set->entries[from];
            if (fbx_constraint_depends_on(consumer, producer, objects, ct))
                ++indegree[to];
        }
    }
    for (int32_t pass = 0; pass < set->count; ++pass) {
        int progress = 0;
        for (int32_t i = 0; i < set->count; ++i) {
            fbx_constraint_t *constraint = &set->entries[i];
            if (placed[i] || indegree[i] != 0)
                continue;
            placed[i] = 1;
            set->order[output_count++] = i;
            progress = 1;
            if (constraint->active && constraint->kind != FBX_CONSTRAINT_UNKNOWN) {
                for (int32_t to = 0; to < set->count; ++to) {
                    fbx_constraint_t *consumer = &set->entries[to];
                    if (placed[to] || !consumer->active ||
                        consumer->kind == FBX_CONSTRAINT_UNKNOWN)
                        continue;
                    if (fbx_constraint_depends_on(consumer, constraint, objects, ct) &&
                        indegree[to] > 0)
                        --indegree[to];
                }
            }
        }
        if (!progress)
            break;
    }
    free(indegree);
    free(placed);
    return output_count == set->count;
}

static int fbx_parse_constraints(fbx_node_t *objects,
                                 const fbx_conn_table_t *ct,
                                 fbx_load_context_t *context,
                                 fbx_constraint_set_t *out_set) {
    int32_t count = 0;
    if (!objects || !ct || !context || !out_set)
        return 0;
    memset(out_set, 0, sizeof(*out_set));
    out_set->context = context;
    for (int32_t i = 0; i < objects->child_count; ++i)
        if (objects->children[i].name && strcmp(objects->children[i].name, "Constraint") == 0)
            ++count;
    if (count == 0)
        return 1;
    if ((size_t)count > SIZE_MAX / sizeof(*out_set->entries) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*out_set->entries)))
        return 0;
    out_set->entries = (fbx_constraint_t *)calloc((size_t)count, sizeof(*out_set->entries));
    if (!out_set->entries)
        return 0;
    out_set->capacity = count;
    for (int32_t i = 0; i < objects->child_count; ++i) {
        fbx_node_t *node = &objects->children[i];
        fbx_constraint_t *constraint;
        const char *type_text;
        if (!node->name || strcmp(node->name, "Constraint") != 0)
            continue;
        if (node->prop_count < 1)
            goto invalid;
        constraint = &out_set->entries[out_set->count++];
        constraint->id = fbx_prop_i64(node, 0);
        constraint->source_node = node;
        type_text = fbx_constraint_type_text(node);
        constraint->kind = fbx_constraint_kind_from_text(type_text);
        constraint->scale_offset[0] = constraint->scale_offset[1] =
            constraint->scale_offset[2] = 1.0;
        for (int32_t connection = fbx_connections_first_for_parent(ct, constraint->id);
             connection >= 0;
             connection = fbx_connections_next_for_parent(ct, connection)) {
            if (ct->entries[connection].prop &&
                !fbx_constraint_bind_connection(constraint,
                                                objects,
                                                ct->entries[connection].child_id,
                                                ct->entries[connection].prop,
                                                context))
                goto invalid;
        }
        for (int32_t connection = fbx_connections_first_for_child(ct, constraint->id);
             connection >= 0;
             connection = fbx_connections_next_for_child(ct, connection)) {
            if (ct->entries[connection].prop &&
                !fbx_constraint_bind_connection(constraint,
                                                objects,
                                                ct->entries[connection].parent_id,
                                                ct->entries[connection].prop,
                                                context))
                goto invalid;
        }
        if (!fbx_constraint_read_common_properties(constraint) ||
            !fbx_constraint_read_source_properties(constraint, objects))
            goto invalid;
        if (constraint->kind == FBX_CONSTRAINT_UNKNOWN) {
            const char *name = "";
            size_t name_len = 0;
            (void)fbx_object_name_view(node, 1, &name, &name_len);
            rt_asset_error_add_warningf(
                "FBX custom constraint '%.*s' (%s) is preserved but not evaluated",
                (int)(name_len > 120u ? 120u : name_len),
                name,
                type_text && type_text[0] ? type_text : "unknown");
            continue;
        }
        if (!constraint->active)
            continue;
        if (constraint->target_id == 0)
            goto invalid;
        if (constraint->kind == FBX_CONSTRAINT_SINGLE_CHAIN_IK) {
            if (!fbx_constraint_ik_chain_valid(constraint, objects, ct))
                goto invalid;
        } else if (constraint->source_count <= 0) {
            goto invalid;
        }
        for (int32_t source = 0; source < constraint->source_count; ++source)
            if (constraint->sources[source].model_id == constraint->target_id)
                goto invalid;
        if (fbx_constraint_has_self_dependency(constraint, objects, ct))
            goto invalid;
    }
    if (!fbx_constraint_build_order(out_set, objects, ct))
        goto invalid;
    return 1;
invalid:
    rt_asset_error_set_if_empty(RT_ASSET_ERROR_CORRUPT,
                                "FBX.Load: invalid active constraint graph or properties");
    fbx_constraint_set_free(out_set);
    return 0;
}

static int32_t fbx_constraint_pose_index(const fbx_constraint_pose_t *pose, int64_t id) {
    return pose && pose->id_table ? fbx_model_index_for_id(pose->id_table, pose->count, id) : -1;
}

static double *fbx_constraint_pose_local(fbx_constraint_pose_t *pose, int32_t index) {
    return pose && index >= 0 && index < pose->count
               ? &pose->local_matrices[(size_t)index * 16u]
               : NULL;
}

static double *fbx_constraint_pose_global(fbx_constraint_pose_t *pose, int32_t index) {
    return pose && index >= 0 && index < pose->count
               ? &pose->global_matrices[(size_t)index * 16u]
               : NULL;
}

static const double *fbx_constraint_static_global_for_id(const fbx_constraint_pose_t *pose,
                                                         int64_t model_id) {
    int32_t index = fbx_constraint_pose_index(pose, model_id);
    return index >= 0 ? &pose->global_matrices[(size_t)index * 16u] : NULL;
}

static double fbx_constraint_vec3_length(const double value[3]) {
    double length_squared;
    if (!value)
        return 0.0;
    length_squared = value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
    return isfinite(length_squared) && length_squared > 0.0 ? sqrt(length_squared) : 0.0;
}

static int fbx_constraint_vec3_normalize(double value[3]) {
    double length = fbx_constraint_vec3_length(value);
    if (!(length > 1e-15) || !isfinite(length))
        return 0;
    value[0] /= length;
    value[1] /= length;
    value[2] /= length;
    return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static double fbx_constraint_vec3_dot(const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void fbx_constraint_vec3_cross(const double a[3], const double b[3], double out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void fbx_constraint_quaternion_normalize(double quaternion[4]) {
    double length_squared;
    double inverse;
    if (!quaternion)
        return;
    length_squared = quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1] +
                     quaternion[2] * quaternion[2] + quaternion[3] * quaternion[3];
    if (!isfinite(length_squared) || length_squared <= 1e-30) {
        quaternion[0] = quaternion[1] = quaternion[2] = 0.0;
        quaternion[3] = 1.0;
        return;
    }
    inverse = 1.0 / sqrt(length_squared);
    for (int i = 0; i < 4; ++i)
        quaternion[i] *= inverse;
}

static double fbx_constraint_quaternion_dot(const double a[4], const double b[4]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

static void fbx_constraint_quaternion_multiply(const double a[4],
                                               const double b[4],
                                               double out[4]) {
    double result[4];
    result[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    result[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    result[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    result[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    memcpy(out, result, sizeof(result));
    fbx_constraint_quaternion_normalize(out);
}

static void fbx_constraint_quaternion_nlerp(const double from[4],
                                            const double to[4],
                                            double alpha,
                                            double out[4]) {
    double sign = fbx_constraint_quaternion_dot(from, to) < 0.0 ? -1.0 : 1.0;
    alpha = fbx_clamp_double(alpha, 0.0, 1.0, 0.0);
    for (int i = 0; i < 4; ++i)
        out[i] = from[i] * (1.0 - alpha) + to[i] * sign * alpha;
    fbx_constraint_quaternion_normalize(out);
}

static void fbx_constraint_quaternion_from_euler(const double euler[3], double out[4]) {
    double rotation[16];
    fbx_mat4_euler_degrees(euler, 0, rotation);
    fbx_decompose_matrix_trs(rotation, NULL, out, NULL);
}

static void fbx_constraint_euler_xyz_from_quaternion(const double quaternion[4],
                                                     double out[3]) {
    static const double radians_to_degrees = 57.2957795130823208768;
    double matrix[16];
    double sy;
    fbx_mat4_quaternion_rotation(quaternion, matrix);
    sy = fbx_clamp_double(-matrix[8], -1.0, 1.0, 0.0);
    out[1] = asin(sy);
    if (fabs(cos(out[1])) > 1e-10) {
        out[0] = atan2(matrix[9], matrix[10]);
        out[2] = atan2(matrix[4], matrix[0]);
    } else {
        out[0] = atan2(-matrix[6], matrix[5]);
        out[2] = 0.0;
    }
    for (int i = 0; i < 3; ++i)
        out[i] *= radians_to_degrees;
}

static void fbx_constraint_compose_trs(const double position[3],
                                       const double quaternion[4],
                                       const double scale[3],
                                       double out[16]) {
    double translation[16];
    double rotation[16];
    double scaling[16];
    fbx_mat4_translate_local(position[0], position[1], position[2], translation);
    fbx_mat4_quaternion_rotation(quaternion, rotation);
    fbx_mat4_scale_local(scale[0], scale[1], scale[2], scaling);
    fbx_mat4_mul_local(translation, rotation, out);
    fbx_mat4_append_local(out, scaling);
}

static void fbx_constraint_transform_vector(const double matrix[16],
                                            const double value[3],
                                            double out[3]) {
    out[0] = matrix[0] * value[0] + matrix[1] * value[1] + matrix[2] * value[2];
    out[1] = matrix[4] * value[0] + matrix[5] * value[1] + matrix[6] * value[2];
    out[2] = matrix[8] * value[0] + matrix[9] * value[1] + matrix[10] * value[2];
}

static void fbx_constraint_source_vector(const fbx_constraint_pose_t *pose,
                                         const double value[3],
                                         double out[3]) {
    if (pose && pose->z_up) {
        out[0] = value[0];
        out[1] = value[2];
        out[2] = -value[1];
    } else {
        memcpy(out, value, 3u * sizeof(double));
    }
}

static void fbx_constraint_source_scale(const fbx_constraint_pose_t *pose,
                                        const double value[3],
                                        double out[3]) {
    if (pose && pose->z_up) {
        out[0] = value[0];
        out[1] = value[2];
        out[2] = value[1];
    } else {
        memcpy(out, value, 3u * sizeof(double));
    }
}

static void fbx_constraint_source_rotation(const fbx_constraint_pose_t *pose,
                                           const double euler[3],
                                           double out_quaternion[4]) {
    double matrix[16];
    fbx_mat4_euler_degrees(euler, 0, matrix);
    if (pose && pose->z_up)
        fbx_mat4_apply_zup(matrix);
    fbx_decompose_matrix_trs(matrix, NULL, out_quaternion, NULL);
}

static void fbx_constraint_quaternion_from_to(const double from_value[3],
                                              const double to_value[3],
                                              double out[4]) {
    double from[3] = {from_value[0], from_value[1], from_value[2]};
    double to[3] = {to_value[0], to_value[1], to_value[2]};
    double cross[3];
    double dot;
    if (!fbx_constraint_vec3_normalize(from) || !fbx_constraint_vec3_normalize(to)) {
        out[0] = out[1] = out[2] = 0.0;
        out[3] = 1.0;
        return;
    }
    dot = fbx_clamp_double(fbx_constraint_vec3_dot(from, to), -1.0, 1.0, 1.0);
    if (dot < -0.999999) {
        double axis[3] = {fabs(from[0]) < fabs(from[1]) && fabs(from[0]) < fabs(from[2]) ? 1.0
                                                                                       : 0.0,
                          fabs(from[1]) <= fabs(from[0]) && fabs(from[1]) < fabs(from[2]) ? 1.0
                                                                                       : 0.0,
                          0.0};
        if (axis[0] == 0.0 && axis[1] == 0.0)
            axis[2] = 1.0;
        fbx_constraint_vec3_cross(from, axis, cross);
        (void)fbx_constraint_vec3_normalize(cross);
        out[0] = cross[0];
        out[1] = cross[1];
        out[2] = cross[2];
        out[3] = 0.0;
        return;
    }
    fbx_constraint_vec3_cross(from, to, cross);
    out[0] = cross[0];
    out[1] = cross[1];
    out[2] = cross[2];
    out[3] = 1.0 + dot;
    fbx_constraint_quaternion_normalize(out);
}

static int fbx_constraint_pose_recompute(fbx_constraint_pose_t *pose) {
    uint8_t *evaluated;
    int32_t evaluated_count = 0;
    if (!pose || pose->count < 0)
        return 0;
    if (pose->count == 0)
        return 1;
    evaluated = pose->evaluation_flags;
    if (!evaluated)
        return 0;
    memset(evaluated, 0, (size_t)pose->count * sizeof(*evaluated));
    for (int32_t pass = 0; pass < pose->count && evaluated_count < pose->count; ++pass) {
        int32_t progress = 0;
        for (int32_t i = 0; i < pose->count; ++i) {
            int32_t parent = pose->parent_indices[i];
            double *global;
            if (evaluated[i] || (parent >= 0 && !evaluated[parent]))
                continue;
            global = fbx_constraint_pose_global(pose, i);
            if (!pose->fixed_globals[i]) {
                if (parent >= 0) {
                    fbx_build_inherited_global_matrix(
                        fbx_constraint_pose_global(pose, parent),
                        &pose->components[parent],
                        fbx_constraint_pose_local(pose, i),
                        &pose->components[i],
                        global);
                } else {
                    memcpy(global, fbx_constraint_pose_local(pose, i), 16u * sizeof(double));
                }
            }
            evaluated[i] = 1;
            ++evaluated_count;
            ++progress;
        }
        if (!progress)
            break;
    }
    return evaluated_count == pose->count;
}

static int fbx_constraint_pose_write_world(fbx_constraint_pose_t *pose,
                                           int32_t index,
                                           const double world[16]) {
    int32_t parent;
    double local[16];
    double position[3];
    double quaternion[4];
    double scale[3];
    if (!pose || index < 0 || index >= pose->count || !world)
        return 0;
    parent = pose->parent_indices[index];
    if (parent >= 0) {
        double inverse_parent[16];
        if (!fbx_mat4_inverse_affine(fbx_constraint_pose_global(pose, parent), inverse_parent))
            return 0;
        fbx_mat4_mul_local(inverse_parent, world, local);
    } else {
        memcpy(local, world, sizeof(local));
    }
    memcpy(fbx_constraint_pose_local(pose, index), local, sizeof(local));
    memcpy(fbx_constraint_pose_global(pose, index), world, 16u * sizeof(double));
    fbx_decompose_matrix_trs(local, position, quaternion, scale);
    pose->components[index].translation[0] = position[0];
    pose->components[index].translation[1] = position[1];
    pose->components[index].translation[2] = position[2];
    pose->components[index].scale[0] = scale[0];
    pose->components[index].scale[1] = scale[1];
    pose->components[index].scale[2] = scale[2];
    pose->fixed_globals[index] = 1;
    return 1;
}

static int fbx_constraint_pose_apply_trs(fbx_constraint_pose_t *pose,
                                         int32_t target_index,
                                         const double desired_position[3],
                                         const double desired_quaternion[4],
                                         const double desired_scale[3],
                                         const int affect_translation[3],
                                         const int affect_rotation[3],
                                         const int affect_scale[3],
                                         double weight) {
    double current_position[3];
    double current_quaternion[4];
    double current_scale[3];
    double position[3];
    double quaternion[4];
    double scale[3];
    double world[16];
    int all_rotation = affect_rotation[0] && affect_rotation[1] && affect_rotation[2];
    if (!pose || target_index < 0 || target_index >= pose->count)
        return 0;
    weight = fbx_clamp_double(weight, 0.0, 1.0, 0.0);
    if (weight <= 0.0)
        return 1;
    fbx_decompose_matrix_trs(fbx_constraint_pose_global(pose, target_index),
                             current_position,
                             current_quaternion,
                             current_scale);
    for (int axis = 0; axis < 3; ++axis) {
        position[axis] = affect_translation[axis]
                             ? current_position[axis] +
                                   (desired_position[axis] - current_position[axis]) * weight
                             : current_position[axis];
        scale[axis] = affect_scale[axis]
                          ? current_scale[axis] + (desired_scale[axis] - current_scale[axis]) * weight
                          : current_scale[axis];
    }
    if (all_rotation) {
        fbx_constraint_quaternion_nlerp(
            current_quaternion, desired_quaternion, weight, quaternion);
    } else {
        double current_euler[3];
        double desired_euler[3];
        double euler[3];
        fbx_constraint_euler_xyz_from_quaternion(current_quaternion, current_euler);
        fbx_constraint_euler_xyz_from_quaternion(desired_quaternion, desired_euler);
        for (int axis = 0; axis < 3; ++axis) {
            double delta = remainder(desired_euler[axis] - current_euler[axis], 360.0);
            euler[axis] = affect_rotation[axis] ? current_euler[axis] + delta * weight
                                                : current_euler[axis];
        }
        fbx_constraint_quaternion_from_euler(euler, quaternion);
    }
    fbx_constraint_compose_trs(position, quaternion, scale, world);
    return fbx_constraint_pose_write_world(pose, target_index, world) &&
           fbx_constraint_pose_recompute(pose);
}

static int fbx_constraint_weighted_sources(const fbx_constraint_t *constraint,
                                           fbx_constraint_pose_t *pose,
                                           int parent_mode,
                                           double out_position[3],
                                           double out_quaternion[4],
                                           double out_scale[3]) {
    double quaternion_sum[4] = {0.0, 0.0, 0.0, 0.0};
    double reference[4] = {0.0, 0.0, 0.0, 1.0};
    double weight_sum = 0.0;
    int have_reference = 0;
    memset(out_position, 0, 3u * sizeof(double));
    memset(out_scale, 0, 3u * sizeof(double));
    for (int32_t i = 0; i < constraint->source_count; ++i) {
        const fbx_constraint_source_t *source = &constraint->sources[i];
        int32_t source_index = fbx_constraint_pose_index(pose, source->model_id);
        double source_matrix[16];
        double position[3];
        double quaternion[4];
        double scale[3];
        double weight = source->weight > 0.0 && isfinite(source->weight) ? source->weight : 0.0;
        if (source_index < 0)
            return 0;
        memcpy(source_matrix,
               fbx_constraint_pose_global(pose, source_index),
               sizeof(source_matrix));
        if (parent_mode) {
            double offset[16];
            double composed[16];
            double offset_quaternion[4];
            fbx_constraint_quaternion_from_euler(source->rotation_offset, offset_quaternion);
            fbx_constraint_compose_trs(source->translation_offset,
                                       offset_quaternion,
                                       source->scale_offset,
                                       offset);
            if (pose->z_up)
                fbx_mat4_apply_zup(offset);
            fbx_mat4_mul_local(source_matrix, offset, composed);
            memcpy(source_matrix, composed, sizeof(source_matrix));
        }
        fbx_decompose_matrix_trs(source_matrix, position, quaternion, scale);
        if (weight <= 0.0)
            continue;
        if (!have_reference) {
            memcpy(reference, quaternion, sizeof(reference));
            have_reference = 1;
        }
        if (fbx_constraint_quaternion_dot(reference, quaternion) < 0.0)
            for (int lane = 0; lane < 4; ++lane)
                quaternion[lane] = -quaternion[lane];
        for (int axis = 0; axis < 3; ++axis) {
            out_position[axis] += position[axis] * weight;
            out_scale[axis] += scale[axis] * weight;
        }
        for (int lane = 0; lane < 4; ++lane)
            quaternion_sum[lane] += quaternion[lane] * weight;
        weight_sum += weight;
    }
    if (!(weight_sum > 0.0) || !isfinite(weight_sum))
        return 0;
    for (int axis = 0; axis < 3; ++axis) {
        out_position[axis] /= weight_sum;
        out_scale[axis] /= weight_sum;
    }
    for (int lane = 0; lane < 4; ++lane)
        out_quaternion[lane] = quaternion_sum[lane] / weight_sum;
    fbx_constraint_quaternion_normalize(out_quaternion);
    return 1;
}

static void fbx_constraint_runtime_mask(const fbx_constraint_pose_t *pose,
                                        const int source_mask[3],
                                        int runtime_mask[3]) {
    if (pose && pose->z_up) {
        runtime_mask[0] = source_mask[0];
        runtime_mask[1] = source_mask[2];
        runtime_mask[2] = source_mask[1];
    } else {
        memcpy(runtime_mask, source_mask, 3u * sizeof(*runtime_mask));
    }
}

static int fbx_constraint_apply_standard(fbx_constraint_t *constraint,
                                         fbx_constraint_pose_t *pose) {
    int32_t target = fbx_constraint_pose_index(pose, constraint->target_id);
    double current_position[3];
    double current_quaternion[4];
    double current_scale[3];
    double desired_position[3];
    double desired_quaternion[4];
    double desired_scale[3];
    int affect_translation[3];
    int affect_rotation[3];
    int affect_scale[3];
    if (target < 0 || !fbx_constraint_weighted_sources(constraint,
                                                       pose,
                                                       constraint->kind == FBX_CONSTRAINT_PARENT,
                                                       desired_position,
                                                       desired_quaternion,
                                                       desired_scale))
        return 0;
    fbx_decompose_matrix_trs(fbx_constraint_pose_global(pose, target),
                             current_position,
                             current_quaternion,
                             current_scale);
    if (constraint->kind == FBX_CONSTRAINT_POSITION) {
        double offset[3];
        fbx_constraint_source_vector(pose, constraint->translation_offset, offset);
        for (int axis = 0; axis < 3; ++axis) {
            desired_position[axis] += offset[axis];
            desired_scale[axis] = current_scale[axis];
        }
        memcpy(desired_quaternion, current_quaternion, sizeof(desired_quaternion));
    } else if (constraint->kind == FBX_CONSTRAINT_ROTATION) {
        double offset[4];
        double composed[4];
        fbx_constraint_source_rotation(pose, constraint->rotation_offset, offset);
        fbx_constraint_quaternion_multiply(desired_quaternion, offset, composed);
        memcpy(desired_quaternion, composed, sizeof(desired_quaternion));
        memcpy(desired_position, current_position, sizeof(desired_position));
        memcpy(desired_scale, current_scale, sizeof(desired_scale));
    } else if (constraint->kind == FBX_CONSTRAINT_SCALE) {
        double offset[3];
        fbx_constraint_source_scale(pose, constraint->scale_offset, offset);
        for (int axis = 0; axis < 3; ++axis) {
            desired_scale[axis] *= offset[axis];
            desired_position[axis] = current_position[axis];
        }
        memcpy(desired_quaternion, current_quaternion, sizeof(desired_quaternion));
    }
    fbx_constraint_runtime_mask(pose, constraint->affect_translation, affect_translation);
    fbx_constraint_runtime_mask(pose, constraint->affect_rotation, affect_rotation);
    fbx_constraint_runtime_mask(pose, constraint->affect_scale, affect_scale);
    return fbx_constraint_pose_apply_trs(pose,
                                         target,
                                         desired_position,
                                         desired_quaternion,
                                         desired_scale,
                                         affect_translation,
                                         affect_rotation,
                                         affect_scale,
                                         constraint->weight);
}

static void fbx_constraint_parallel_fallback(const double direction[3], double out[3]) {
    if (fabs(direction[0]) <= fabs(direction[1]) && fabs(direction[0]) <= fabs(direction[2])) {
        out[0] = 1.0;
        out[1] = 0.0;
        out[2] = 0.0;
    } else if (fabs(direction[1]) <= fabs(direction[2])) {
        out[0] = 0.0;
        out[1] = 1.0;
        out[2] = 0.0;
    } else {
        out[0] = 0.0;
        out[1] = 0.0;
        out[2] = 1.0;
    }
}

static int fbx_constraint_apply_aim(fbx_constraint_t *constraint,
                                    fbx_constraint_pose_t *pose) {
    int32_t target = fbx_constraint_pose_index(pose, constraint->target_id);
    double current_position[3];
    double current_quaternion[4];
    double current_scale[3];
    double weighted_position[3];
    double ignored_quaternion[4];
    double ignored_scale[3];
    double direction[3];
    double world_up[3] = {0.0, 1.0, 0.0};
    double local_aim[3];
    double local_up[3];
    double local_side[3];
    double world_side[3];
    double corrected_local_up[3];
    double corrected_world_up[3];
    double local_basis[9];
    double world_basis[9];
    double rotation[16];
    double desired_quaternion[4];
    double offset_quaternion[4];
    double composed_quaternion[4];
    int affect_translation[3];
    int affect_rotation[3];
    int affect_scale[3];
    if (target < 0 || !fbx_constraint_weighted_sources(constraint,
                                                       pose,
                                                       0,
                                                       weighted_position,
                                                       ignored_quaternion,
                                                       ignored_scale))
        return 0;
    fbx_constraint_source_vector(pose, constraint->aim_vector, local_aim);
    fbx_constraint_source_vector(pose, constraint->up_vector, local_up);
    fbx_decompose_matrix_trs(fbx_constraint_pose_global(pose, target),
                             current_position,
                             current_quaternion,
                             current_scale);
    direction[0] = weighted_position[0] - current_position[0];
    direction[1] = weighted_position[1] - current_position[1];
    direction[2] = weighted_position[2] - current_position[2];
    if (!fbx_constraint_vec3_normalize(direction) ||
        !fbx_constraint_vec3_normalize(local_aim) ||
        !fbx_constraint_vec3_normalize(local_up))
        return 0;
    if (constraint->world_up_type == 1) {
        int32_t up_index = fbx_constraint_pose_index(pose, constraint->world_up_id);
        if (up_index < 0)
            return 0;
        world_up[0] = fbx_constraint_pose_global(pose, up_index)[3] - current_position[0];
        world_up[1] = fbx_constraint_pose_global(pose, up_index)[7] - current_position[1];
        world_up[2] = fbx_constraint_pose_global(pose, up_index)[11] - current_position[2];
    } else if (constraint->world_up_type == 2) {
        int32_t up_index = fbx_constraint_pose_index(pose, constraint->world_up_id);
        double up_vector[3];
        if (up_index < 0)
            return 0;
        fbx_constraint_source_vector(pose, constraint->world_up_vector, up_vector);
        fbx_constraint_transform_vector(fbx_constraint_pose_global(pose, up_index),
                                        up_vector,
                                        world_up);
    } else if (constraint->world_up_type == 3) {
        fbx_constraint_source_vector(pose, constraint->world_up_vector, world_up);
    } else if (constraint->world_up_type == 4) {
        fbx_constraint_transform_vector(
            fbx_constraint_pose_global(pose, target), local_up, world_up);
    }
    if (!fbx_constraint_vec3_normalize(world_up))
        fbx_constraint_parallel_fallback(direction, world_up);
    fbx_constraint_vec3_cross(local_aim, local_up, local_side);
    if (!fbx_constraint_vec3_normalize(local_side)) {
        fbx_constraint_parallel_fallback(local_aim, local_up);
        fbx_constraint_vec3_cross(local_aim, local_up, local_side);
        if (!fbx_constraint_vec3_normalize(local_side))
            return 0;
    }
    fbx_constraint_vec3_cross(local_side, local_aim, corrected_local_up);
    (void)fbx_constraint_vec3_normalize(corrected_local_up);
    fbx_constraint_vec3_cross(direction, world_up, world_side);
    if (!fbx_constraint_vec3_normalize(world_side)) {
        fbx_constraint_parallel_fallback(direction, world_up);
        fbx_constraint_vec3_cross(direction, world_up, world_side);
        if (!fbx_constraint_vec3_normalize(world_side))
            return 0;
    }
    fbx_constraint_vec3_cross(world_side, direction, corrected_world_up);
    (void)fbx_constraint_vec3_normalize(corrected_world_up);
    local_basis[0] = local_aim[0];
    local_basis[1] = corrected_local_up[0];
    local_basis[2] = local_side[0];
    local_basis[3] = local_aim[1];
    local_basis[4] = corrected_local_up[1];
    local_basis[5] = local_side[1];
    local_basis[6] = local_aim[2];
    local_basis[7] = corrected_local_up[2];
    local_basis[8] = local_side[2];
    world_basis[0] = direction[0];
    world_basis[1] = corrected_world_up[0];
    world_basis[2] = world_side[0];
    world_basis[3] = direction[1];
    world_basis[4] = corrected_world_up[1];
    world_basis[5] = world_side[1];
    world_basis[6] = direction[2];
    world_basis[7] = corrected_world_up[2];
    world_basis[8] = world_side[2];
    fbx_mat4_identity_local(rotation);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            rotation[row * 4 + column] =
                world_basis[row * 3 + 0] * local_basis[column * 3 + 0] +
                world_basis[row * 3 + 1] * local_basis[column * 3 + 1] +
                world_basis[row * 3 + 2] * local_basis[column * 3 + 2];
    fbx_decompose_matrix_trs(rotation, NULL, desired_quaternion, NULL);
    fbx_constraint_source_rotation(pose, constraint->rotation_offset, offset_quaternion);
    fbx_constraint_quaternion_multiply(
        desired_quaternion, offset_quaternion, composed_quaternion);
    fbx_constraint_runtime_mask(pose, constraint->affect_translation, affect_translation);
    fbx_constraint_runtime_mask(pose, constraint->affect_rotation, affect_rotation);
    fbx_constraint_runtime_mask(pose, constraint->affect_scale, affect_scale);
    return fbx_constraint_pose_apply_trs(pose,
                                         target,
                                         current_position,
                                         composed_quaternion,
                                         current_scale,
                                         affect_translation,
                                         affect_rotation,
                                         affect_scale,
                                         constraint->weight);
}

static void fbx_constraint_rotate_about_axis(const double point[3],
                                             const double origin[3],
                                             const double axis_value[3],
                                             double angle,
                                             double out[3]) {
    double axis[3] = {axis_value[0], axis_value[1], axis_value[2]};
    double value[3] = {point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    double cross[3];
    double cosine = cos(angle);
    double sine = sin(angle);
    double dot;
    if (!fbx_constraint_vec3_normalize(axis) || !isfinite(cosine) || !isfinite(sine)) {
        memcpy(out, point, 3u * sizeof(double));
        return;
    }
    fbx_constraint_vec3_cross(axis, value, cross);
    dot = fbx_constraint_vec3_dot(axis, value);
    for (int component = 0; component < 3; ++component)
        out[component] = origin[component] + value[component] * cosine + cross[component] * sine +
                         axis[component] * dot * (1.0 - cosine);
}

static void fbx_constraint_ik_apply_pole(double *positions,
                                         int32_t count,
                                         const double pole[3]) {
    for (int32_t i = 1; i + 1 < count; ++i) {
        double *previous = &positions[(size_t)(i - 1) * 3u];
        double *current = &positions[(size_t)i * 3u];
        double *next = &positions[(size_t)(i + 1) * 3u];
        double axis[3] = {next[0] - previous[0],
                          next[1] - previous[1],
                          next[2] - previous[2]};
        double current_vector[3] = {current[0] - previous[0],
                                    current[1] - previous[1],
                                    current[2] - previous[2]};
        double pole_vector[3] = {pole[0] - previous[0],
                                 pole[1] - previous[1],
                                 pole[2] - previous[2]};
        double projected_current[3];
        double projected_pole[3];
        double cross[3];
        double angle;
        if (!fbx_constraint_vec3_normalize(axis))
            continue;
        {
            double current_axis = fbx_constraint_vec3_dot(current_vector, axis);
            double pole_axis = fbx_constraint_vec3_dot(pole_vector, axis);
            for (int component = 0; component < 3; ++component) {
                projected_current[component] =
                    current_vector[component] - axis[component] * current_axis;
                projected_pole[component] = pole_vector[component] - axis[component] * pole_axis;
            }
        }
        if (!fbx_constraint_vec3_normalize(projected_current) ||
            !fbx_constraint_vec3_normalize(projected_pole))
            continue;
        fbx_constraint_vec3_cross(projected_current, projected_pole, cross);
        angle = atan2(fbx_constraint_vec3_dot(cross, axis),
                      fbx_clamp_double(fbx_constraint_vec3_dot(projected_current, projected_pole),
                                       -1.0,
                                       1.0,
                                       1.0));
        fbx_constraint_rotate_about_axis(current, previous, axis, angle, current);
    }
}

static int fbx_constraint_apply_ik(fbx_constraint_t *constraint,
                                   fbx_constraint_pose_t *pose,
                                   fbx_load_context_t *context) {
    int32_t first = fbx_constraint_pose_index(pose, constraint->target_id);
    int32_t end = fbx_constraint_pose_index(pose, constraint->ik_end_id);
    int32_t effector = fbx_constraint_pose_index(pose, constraint->ik_effector_id);
    int32_t *chain = NULL;
    double *original = NULL;
    double *positions = NULL;
    double *lengths = NULL;
    int32_t count = 0;
    int32_t walk;
    double total_length = 0.0;
    double target[3];
    double root_anchor[3];
    double pole[3];
    int unreachable = 0;
    if (first < 0 || end < 0 || effector < 0)
        return 0;
    fbx_constraint_source_vector(pose, constraint->pole_vector, pole);
    for (walk = end; walk >= 0 && count <= pose->count; walk = pose->parent_indices[walk]) {
        ++count;
        if (walk == first)
            break;
    }
    if (walk != first || count < 2 || count > pose->count ||
        (size_t)count > SIZE_MAX / sizeof(*chain) ||
        (size_t)count > SIZE_MAX / (3u * sizeof(*positions)) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*chain)) ||
        !fbx_budget_charge(context, (uint64_t)count, 6u * sizeof(double)) ||
        !fbx_budget_charge(context, (uint64_t)(count - 1), sizeof(*lengths)))
        return 0;
    chain = (int32_t *)malloc((size_t)count * sizeof(*chain));
    original = (double *)malloc((size_t)count * 3u * sizeof(*original));
    positions = (double *)malloc((size_t)count * 3u * sizeof(*positions));
    lengths = (double *)malloc((size_t)(count - 1) * sizeof(*lengths));
    if (!chain || !original || !positions || !lengths)
        goto fail;
    walk = end;
    for (int32_t reverse = count - 1; reverse >= 0; --reverse) {
        chain[reverse] = walk;
        walk = pose->parent_indices[walk];
    }
    for (int32_t i = 0; i < count; ++i) {
        double *global = fbx_constraint_pose_global(pose, chain[i]);
        for (int component = 0; component < 3; ++component)
            original[(size_t)i * 3u + (size_t)component] =
                positions[(size_t)i * 3u + (size_t)component] = global[component * 4 + 3];
        if (i > 0) {
            double delta[3] = {positions[(size_t)i * 3u + 0u] -
                                   positions[(size_t)(i - 1) * 3u + 0u],
                               positions[(size_t)i * 3u + 1u] -
                                   positions[(size_t)(i - 1) * 3u + 1u],
                               positions[(size_t)i * 3u + 2u] -
                                   positions[(size_t)(i - 1) * 3u + 2u]};
            lengths[i - 1] = fbx_constraint_vec3_length(delta);
            if (!isfinite(lengths[i - 1]) || lengths[i - 1] < 0.0)
                goto fail;
            total_length += lengths[i - 1];
        }
    }
    target[0] = fbx_constraint_pose_global(pose, effector)[3];
    target[1] = fbx_constraint_pose_global(pose, effector)[7];
    target[2] = fbx_constraint_pose_global(pose, effector)[11];
    memcpy(root_anchor, positions, sizeof(root_anchor));
    if (constraint->pole_vector_type == 1 && constraint->ik_pole_id != 0) {
        int32_t pole_index = fbx_constraint_pose_index(pose, constraint->ik_pole_id);
        if (pole_index < 0)
            goto fail;
        pole[0] = fbx_constraint_pose_global(pose, pole_index)[3];
        pole[1] = fbx_constraint_pose_global(pose, pole_index)[7];
        pole[2] = fbx_constraint_pose_global(pose, pole_index)[11];
    }
    {
        double to_target[3] = {target[0] - root_anchor[0],
                               target[1] - root_anchor[1],
                               target[2] - root_anchor[2]};
        double target_distance = fbx_constraint_vec3_length(to_target);
        if (target_distance > total_length + 1e-9) {
            unreachable = 1;
            if (!fbx_constraint_vec3_normalize(to_target))
                goto fail;
            for (int32_t i = 1; i < count; ++i)
                for (int component = 0; component < 3; ++component)
                    positions[(size_t)i * 3u + (size_t)component] =
                        positions[(size_t)(i - 1) * 3u + (size_t)component] +
                        to_target[component] * lengths[i - 1];
        } else {
            double tolerance = (total_length > 1.0 ? total_length : 1.0) * 1e-6;
            for (int iteration = 0; iteration < 32; ++iteration) {
                memcpy(&positions[(size_t)(count - 1) * 3u], target, sizeof(target));
                for (int32_t i = count - 2; i >= 0; --i) {
                    double direction[3] = {positions[(size_t)i * 3u + 0u] -
                                               positions[(size_t)(i + 1) * 3u + 0u],
                                           positions[(size_t)i * 3u + 1u] -
                                               positions[(size_t)(i + 1) * 3u + 1u],
                                           positions[(size_t)i * 3u + 2u] -
                                               positions[(size_t)(i + 1) * 3u + 2u]};
                    if (!fbx_constraint_vec3_normalize(direction))
                        continue;
                    for (int component = 0; component < 3; ++component)
                        positions[(size_t)i * 3u + (size_t)component] =
                            positions[(size_t)(i + 1) * 3u + (size_t)component] +
                            direction[component] * lengths[i];
                }
                memcpy(positions, root_anchor, sizeof(root_anchor));
                for (int32_t i = 1; i < count; ++i) {
                    double direction[3] = {positions[(size_t)i * 3u + 0u] -
                                               positions[(size_t)(i - 1) * 3u + 0u],
                                           positions[(size_t)i * 3u + 1u] -
                                               positions[(size_t)(i - 1) * 3u + 1u],
                                           positions[(size_t)i * 3u + 2u] -
                                               positions[(size_t)(i - 1) * 3u + 2u]};
                    if (!fbx_constraint_vec3_normalize(direction))
                        continue;
                    for (int component = 0; component < 3; ++component)
                        positions[(size_t)i * 3u + (size_t)component] =
                            positions[(size_t)(i - 1) * 3u + (size_t)component] +
                            direction[component] * lengths[i - 1];
                }
                fbx_constraint_ik_apply_pole(positions, count, pole);
                {
                    double error[3] = {positions[(size_t)(count - 1) * 3u + 0u] - target[0],
                                       positions[(size_t)(count - 1) * 3u + 1u] - target[1],
                                       positions[(size_t)(count - 1) * 3u + 2u] - target[2]};
                    if (fbx_constraint_vec3_length(error) <= tolerance)
                        break;
                    if (iteration == 31)
                        unreachable = 1;
                }
            }
        }
    }
    if (count > 2 && fabs(constraint->twist) > 1e-12) {
        double axis[3] = {positions[(size_t)(count - 1) * 3u + 0u] - positions[0],
                          positions[(size_t)(count - 1) * 3u + 1u] - positions[1],
                          positions[(size_t)(count - 1) * 3u + 2u] - positions[2]};
        double radians = fbx_sanitize_rotation_degrees(constraint->twist) *
                         3.14159265358979323846 / 180.0;
        if (fbx_constraint_vec3_normalize(axis))
            for (int32_t i = 1; i + 1 < count; ++i)
                fbx_constraint_rotate_about_axis(&positions[(size_t)i * 3u],
                                                 positions,
                                                 axis,
                                                 radians,
                                                 &positions[(size_t)i * 3u]);
    }
    if (constraint->weight < 1.0) {
        double weight = fbx_clamp_double(constraint->weight, 0.0, 1.0, 0.0);
        double solved_previous[3] = {positions[0], positions[1], positions[2]};
        memcpy(positions, original, 3u * sizeof(double));
        for (int32_t i = 1; i < count; ++i) {
            double solved_current[3] = {positions[(size_t)i * 3u + 0u],
                                        positions[(size_t)i * 3u + 1u],
                                        positions[(size_t)i * 3u + 2u]};
            double old_direction[3] = {original[(size_t)i * 3u + 0u] -
                                           original[(size_t)(i - 1) * 3u + 0u],
                                       original[(size_t)i * 3u + 1u] -
                                           original[(size_t)(i - 1) * 3u + 1u],
                                       original[(size_t)i * 3u + 2u] -
                                           original[(size_t)(i - 1) * 3u + 2u]};
            double solved_direction[3] = {solved_current[0] - solved_previous[0],
                                          solved_current[1] - solved_previous[1],
                                          solved_current[2] - solved_previous[2]};
            if (!fbx_constraint_vec3_normalize(old_direction) ||
                !fbx_constraint_vec3_normalize(solved_direction))
                goto fail;
            for (int component = 0; component < 3; ++component)
                solved_direction[component] = old_direction[component] * (1.0 - weight) +
                                              solved_direction[component] * weight;
            if (!fbx_constraint_vec3_normalize(solved_direction))
                memcpy(solved_direction, old_direction, sizeof(solved_direction));
            for (int component = 0; component < 3; ++component)
                positions[(size_t)i * 3u + (size_t)component] =
                    positions[(size_t)(i - 1) * 3u + (size_t)component] +
                    solved_direction[component] * lengths[i - 1];
            memcpy(solved_previous, solved_current, sizeof(solved_previous));
        }
    }
    for (int32_t i = 0; i < count; ++i) {
        double current_position[3];
        double current_quaternion[4];
        double current_scale[3];
        double desired_quaternion[4];
        double world[16];
        fbx_decompose_matrix_trs(fbx_constraint_pose_global(pose, chain[i]),
                                 current_position,
                                 current_quaternion,
                                 current_scale);
        memcpy(desired_quaternion, current_quaternion, sizeof(desired_quaternion));
        if (i + 1 < count) {
            double old_direction[3] = {original[(size_t)(i + 1) * 3u + 0u] -
                                           original[(size_t)i * 3u + 0u],
                                       original[(size_t)(i + 1) * 3u + 1u] -
                                           original[(size_t)i * 3u + 1u],
                                       original[(size_t)(i + 1) * 3u + 2u] -
                                           original[(size_t)i * 3u + 2u]};
            double new_direction[3] = {positions[(size_t)(i + 1) * 3u + 0u] -
                                           positions[(size_t)i * 3u + 0u],
                                       positions[(size_t)(i + 1) * 3u + 1u] -
                                           positions[(size_t)i * 3u + 1u],
                                       positions[(size_t)(i + 1) * 3u + 2u] -
                                           positions[(size_t)i * 3u + 2u]};
            double delta[4];
            fbx_constraint_quaternion_from_to(old_direction, new_direction, delta);
            fbx_constraint_quaternion_multiply(delta, current_quaternion, desired_quaternion);
        }
        fbx_constraint_compose_trs(
            &positions[(size_t)i * 3u], desired_quaternion, current_scale, world);
        memcpy(fbx_constraint_pose_global(pose, chain[i]), world, sizeof(world));
        pose->fixed_globals[chain[i]] = 1;
    }
    for (int32_t i = 0; i < count; ++i) {
        double world[16];
        memcpy(world, fbx_constraint_pose_global(pose, chain[i]), sizeof(world));
        if (!fbx_constraint_pose_write_world(pose, chain[i], world))
            goto fail;
    }
    if (!fbx_constraint_pose_recompute(pose))
        goto fail;
    if (unreachable && !constraint->warned_unsolvable) {
        rt_asset_error_add_warning(
            "FBX single-chain IK target is unreachable; closest finite pose retained");
        constraint->warned_unsolvable = 1;
    }
    free(chain);
    free(original);
    free(positions);
    free(lengths);
    return 1;
fail:
    free(chain);
    free(original);
    free(positions);
    free(lengths);
    return 0;
}

static int fbx_constraint_apply_set(fbx_constraint_set_t *set,
                                    fbx_constraint_pose_t *pose,
                                    fbx_load_context_t *context) {
    if (!set || !pose || !context)
        return 0;
    for (int32_t ordered = 0; ordered < set->count; ++ordered) {
        fbx_constraint_t *constraint = &set->entries[set->order ? set->order[ordered] : ordered];
        int success = 1;
        if (!constraint->active || constraint->kind == FBX_CONSTRAINT_UNKNOWN ||
            constraint->weight <= 0.0)
            continue;
        if (constraint->kind == FBX_CONSTRAINT_AIM)
            success = fbx_constraint_apply_aim(constraint, pose);
        else if (constraint->kind == FBX_CONSTRAINT_SINGLE_CHAIN_IK)
            success = fbx_constraint_apply_ik(constraint, pose, context);
        else
            success = fbx_constraint_apply_standard(constraint, pose);
        if (!success) {
            rt_asset_error_set_if_empty(RT_ASSET_ERROR_CORRUPT,
                                        "FBX.Load: constraint evaluation produced an invalid pose");
            return 0;
        }
    }
    return 1;
}

static int fbx_constraint_pose_allocate(fbx_constraint_pose_t *pose,
                                        int32_t count,
                                        fbx_load_context_t *context) {
    if (!pose || count < 0 || !context)
        return 0;
    memset(pose, 0, sizeof(*pose));
    pose->count = count;
    if (count == 0)
        return 1;
    if ((size_t)count > SIZE_MAX / sizeof(*pose->ids) ||
        (size_t)count > SIZE_MAX / (16u * sizeof(double)) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*pose->ids)) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*pose->parent_indices)) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*pose->sources)) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*pose->components)) ||
        !fbx_budget_charge(context, (uint64_t)count, 32u * sizeof(double)) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*pose->id_table)) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*pose->fixed_globals)) ||
        !fbx_budget_charge(context, (uint64_t)count, sizeof(*pose->evaluation_flags)))
        return 0;
    pose->ids = (int64_t *)malloc((size_t)count * sizeof(*pose->ids));
    pose->parent_indices = (int32_t *)malloc((size_t)count * sizeof(*pose->parent_indices));
    pose->sources = (fbx_node_t **)malloc((size_t)count * sizeof(*pose->sources));
    pose->components =
        (fbx_transform_components_t *)malloc((size_t)count * sizeof(*pose->components));
    pose->local_matrices = (double *)malloc((size_t)count * 16u * sizeof(double));
    pose->global_matrices = (double *)malloc((size_t)count * 16u * sizeof(double));
    pose->id_table = (fbx_model_id_entry_t *)malloc((size_t)count * sizeof(*pose->id_table));
    pose->fixed_globals = (uint8_t *)calloc((size_t)count, sizeof(*pose->fixed_globals));
    pose->evaluation_flags =
        (uint8_t *)malloc((size_t)count * sizeof(*pose->evaluation_flags));
    if (!pose->ids || !pose->parent_indices || !pose->sources || !pose->components ||
        !pose->local_matrices || !pose->global_matrices || !pose->id_table ||
        !pose->fixed_globals || !pose->evaluation_flags) {
        fbx_constraint_pose_free(pose);
        return 0;
    }
    return 1;
}

static int fbx_constraint_build_static_pose(fbx_node_t *objects,
                                            const fbx_conn_table_t *ct,
                                            int z_up,
                                            fbx_constraint_set_t *set,
                                            fbx_load_context_t *context,
                                            fbx_constraint_pose_t *out_pose) {
    int32_t count = 0;
    if (!objects || !ct || !set || !context || !out_pose)
        return 0;
    for (int32_t i = 0; i < objects->child_count; ++i)
        if (objects->children[i].name && strcmp(objects->children[i].name, "Model") == 0 &&
            objects->children[i].prop_count >= 3)
            ++count;
    if (!fbx_constraint_pose_allocate(out_pose, count, context))
        return 0;
    out_pose->z_up = z_up;
    count = 0;
    for (int32_t i = 0; i < objects->child_count; ++i) {
        fbx_node_t *model = &objects->children[i];
        if (!model->name || strcmp(model->name, "Model") != 0 || model->prop_count < 3)
            continue;
        out_pose->ids[count] = fbx_prop_i64(model, 0);
        out_pose->sources[count] = model;
        out_pose->parent_indices[count] = -1;
        fbx_read_model_transform_components(model, &out_pose->components[count]);
        fbx_build_model_transform_matrix(&out_pose->components[count],
                                         fbx_constraint_pose_local(out_pose, count));
        if (z_up)
            fbx_mat4_apply_zup(fbx_constraint_pose_local(out_pose, count));
        out_pose->id_table[count].id = out_pose->ids[count];
        out_pose->id_table[count].index = count;
        ++count;
    }
    qsort(out_pose->id_table,
          (size_t)out_pose->count,
          sizeof(*out_pose->id_table),
          fbx_model_id_entry_cmp);
    for (int32_t i = 0; i < out_pose->count; ++i) {
        int64_t parent_id = fbx_find_parent(ct, out_pose->ids[i]);
        if (parent_id != 0)
            out_pose->parent_indices[i] = fbx_constraint_pose_index(out_pose, parent_id);
    }
    if (!fbx_constraint_pose_recompute(out_pose) ||
        !fbx_constraint_apply_set(set, out_pose, context)) {
        fbx_constraint_pose_free(out_pose);
        return 0;
    }
    return 1;
}
