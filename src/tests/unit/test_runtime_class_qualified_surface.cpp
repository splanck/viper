//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_runtime_class_qualified_surface.cpp
// Purpose: Guard runtime.def class-qualified Graphics3D/Game3D names against
//   drift from the runtime class method/property surface.
// Key invariants:
//   - runtime.def is the single source of truth and is consumed by X-macro
//     expansion, not by text scraping.
//   - Class-qualified aliases must resolve to a callable class member with the
//     same public member name.
//   - Direct runtime class leaf names under Viper.* must not collide.
//   - Instance method signatures omit the leading obj receiver from RT_FUNC.
// Ownership/Lifetime:
//   - Test tables own copied runtime.def string literals for process lifetime.
//   - No runtime objects are allocated.
// Links: src/il/runtime/runtime.def
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct SignatureException {
    const char *class_name;
    const char *method_name;
    const char *target_id;
    const char *reason;
};

static const std::vector<SignatureException> kSignatureExceptions = {
    {"Viper.Graphics3D.IKSolver3D",
     "LookAt",
     "IKSolver3DLookAt",
     "Static IK-solver factory; the leading obj is the skeleton input, not a receiver."},
    {"Viper.Graphics3D.IKSolver3D",
     "FABRIK",
     "IKSolver3DFABRIK",
     "Static IK-solver factory; the leading obj is the chain input, not a receiver."},
};

struct RuntimeFunc {
    std::string id;
    std::string c_symbol;
    std::string canonical;
    std::string signature;
};

struct RuntimeAlias {
    std::string canonical;
    std::string target_id;
};

struct RuntimeProp {
    std::string name;
    std::string type;
    std::string getter_id;
    std::string setter_id;
};

struct RuntimeMethod {
    std::string name;
    std::string signature;
    std::string target_id;
};

struct RuntimeClass {
    std::string name;
    std::string type_id;
    std::string layout;
    std::string ctor_id;
    std::vector<RuntimeProp> props;
    std::vector<RuntimeMethod> methods;
};

struct RuntimeSurface {
    std::vector<RuntimeFunc> funcs;
    std::vector<RuntimeAlias> aliases;
    std::vector<RuntimeClass> classes;
    RuntimeClass *current_class = nullptr;

    void add_func(const char *id,
                  const char *c_symbol,
                  const char *canonical,
                  const char *signature) {
        funcs.push_back({id, c_symbol, canonical, signature});
    }

    void add_alias(const char *canonical, const char *target_id) {
        aliases.push_back({canonical, target_id});
    }

    void begin_class(const char *name,
                     const char *type_id,
                     const char *layout,
                     const char *ctor_id) {
        classes.push_back({name, type_id, layout, ctor_id, {}, {}});
        current_class = &classes.back();
    }

    void add_prop(const char *name,
                  const char *type,
                  const char *getter_id,
                  const char *setter_id) {
        if (current_class)
            current_class->props.push_back({name, type, getter_id, setter_id});
    }

    void add_method(const char *name, const char *signature, const char *target_id) {
        if (current_class)
            current_class->methods.push_back({name, signature, target_id});
    }

    void end_class() {
        current_class = nullptr;
    }
};

RuntimeSurface load_runtime_surface() {
    RuntimeSurface surface;

#define RT_FUNC(id, c_symbol, canonical, signature, ...)                                           \
    surface.add_func(#id, #c_symbol, canonical, signature);
#define RT_ALIAS(canonical, target_id) surface.add_alias(canonical, #target_id);
#define RT_BRIDGE(target_id, roles)
#define RT_CLASS_BEGIN(name, type_id, layout, ctor_id)                                             \
    surface.begin_class(name, #type_id, layout, #ctor_id);
#define RT_PROP(name, type, getter_id, setter_id)                                                  \
    surface.add_prop(name, type, #getter_id, #setter_id);
#define RT_METHOD(name, signature, target_id) surface.add_method(name, signature, #target_id);
#define RT_CLASS_END() surface.end_class();
#include "il/runtime/runtime.def"
#undef RT_CLASS_END
#undef RT_METHOD
#undef RT_PROP
#undef RT_CLASS_BEGIN
#undef RT_BRIDGE
#undef RT_ALIAS
#undef RT_FUNC

    return surface;
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool is_scoped_class(std::string_view class_name) {
    return starts_with(class_name, "Viper.Graphics3D.") || starts_with(class_name, "Viper.Game3D.");
}

std::string upper_first(std::string_view value) {
    std::string out(value);
    if (!out.empty() && out.front() >= 'a' && out.front() <= 'z')
        out.front() = static_cast<char>(out.front() - 'a' + 'A');
    return out;
}

std::vector<std::string> split_signature_args(std::string_view args) {
    std::vector<std::string> out;
    std::string current;
    int angle_depth = 0;
    for (char c : args) {
        if (c == '<') {
            ++angle_depth;
            current.push_back(c);
        } else if (c == '>') {
            --angle_depth;
            current.push_back(c);
        } else if (c == ',' && angle_depth == 0) {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty())
        out.push_back(current);
    return out;
}

std::string join_signature(std::string_view ret, const std::vector<std::string> &args) {
    std::ostringstream out;
    out << ret << "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i)
            out << ",";
        out << args[i];
    }
    out << ")";
    return out.str();
}

bool is_object_token(std::string_view token) {
    return token == "obj" || starts_with(token, "obj<");
}

std::string normalize_object_token(const std::string &token) {
    return is_object_token(token) ? std::string("obj") : token;
}

std::string normalized_signature(std::string_view signature) {
    size_t open = signature.find('(');
    size_t close = signature.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open)
        return std::string(signature);

    std::string ret = normalize_object_token(std::string(signature.substr(0, open)));
    std::vector<std::string> args =
        split_signature_args(signature.substr(open + 1, close - open - 1));
    for (std::string &arg : args)
        arg = normalize_object_token(arg);
    return join_signature(ret, args);
}

bool is_static_factory_method(const RuntimeClass &runtime_class, const RuntimeMethod &method) {
    return runtime_class.ctor_id == method.target_id || starts_with(method.name, "New") ||
           starts_with(method.name, "From") || method.name == "Of";
}

std::string method_signature_for_target(const RuntimeClass &runtime_class,
                                        const RuntimeMethod &method,
                                        std::string_view target_signature) {
    size_t open = target_signature.find('(');
    size_t close = target_signature.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open)
        return std::string(target_signature);

    std::string_view ret = target_signature.substr(0, open);
    std::vector<std::string> args =
        split_signature_args(target_signature.substr(open + 1, close - open - 1));
    if (!args.empty() && args.front() == "obj" &&
        !is_static_factory_method(runtime_class, method)) {
        args.erase(args.begin());
    }
    return join_signature(ret, args);
}

bool signature_exception_applies(std::string_view class_name,
                                 std::string_view method_name,
                                 std::string_view target_id) {
    for (const SignatureException &exception : kSignatureExceptions) {
        if (class_name == exception.class_name && method_name == exception.method_name &&
            target_id == exception.target_id) {
            return true;
        }
    }
    return false;
}

struct RuntimeIndex {
    std::map<std::string, const RuntimeFunc *> funcs_by_id;
    std::map<std::string, const RuntimeFunc *> funcs_by_canonical;
    std::map<std::string, const RuntimeAlias *> aliases_by_canonical;
    std::map<std::string, const RuntimeClass *> classes_by_name;
};

RuntimeIndex build_index(const RuntimeSurface &surface) {
    RuntimeIndex index;
    for (const RuntimeFunc &func : surface.funcs) {
        index.funcs_by_id.emplace(func.id, &func);
        index.funcs_by_canonical.emplace(func.canonical, &func);
    }
    for (const RuntimeAlias &alias : surface.aliases)
        index.aliases_by_canonical.emplace(alias.canonical, &alias);
    for (const RuntimeClass &runtime_class : surface.classes)
        index.classes_by_name.emplace(runtime_class.name, &runtime_class);
    return index;
}

const RuntimeFunc *resolve_func(const RuntimeIndex &index, const std::string &id_or_canonical) {
    if (id_or_canonical.empty() || id_or_canonical == "none")
        return nullptr;

    auto id_it = index.funcs_by_id.find(id_or_canonical);
    if (id_it != index.funcs_by_id.end())
        return id_it->second;

    auto canonical_it = index.funcs_by_canonical.find(id_or_canonical);
    if (canonical_it != index.funcs_by_canonical.end())
        return canonical_it->second;

    auto alias_it = index.aliases_by_canonical.find(id_or_canonical);
    if (alias_it != index.aliases_by_canonical.end())
        return resolve_func(index, alias_it->second->target_id);

    return nullptr;
}

const RuntimeClass *find_owner_class(const RuntimeIndex &index, std::string_view canonical) {
    const RuntimeClass *best = nullptr;
    for (const auto &entry : index.classes_by_name) {
        const RuntimeClass *runtime_class = entry.second;
        if (!is_scoped_class(runtime_class->name))
            continue;
        std::string prefix = runtime_class->name + ".";
        if (starts_with(canonical, prefix) &&
            (!best || runtime_class->name.size() > best->name.size())) {
            best = runtime_class;
        }
    }
    return best;
}

void add_reachable(std::map<std::string, std::set<std::string>> &reachable,
                   const std::string &canonical,
                   const RuntimeFunc *target) {
    if (target)
        reachable[canonical].insert(target->id);
}

struct ReachableSurface {
    std::map<std::string, std::set<std::string>> reachable;
    std::map<std::string, std::set<std::string>> explicit_methods;
};

ReachableSurface build_reachable_names(const RuntimeSurface &surface, const RuntimeIndex &index) {
    ReachableSurface out;
    for (const RuntimeClass &runtime_class : surface.classes) {
        if (!is_scoped_class(runtime_class.name))
            continue;

        const RuntimeFunc *ctor = resolve_func(index, runtime_class.ctor_id);
        if (ctor)
            add_reachable(out.reachable, ctor->canonical, ctor);

        for (const RuntimeProp &prop : runtime_class.props) {
            const RuntimeFunc *getter = resolve_func(index, prop.getter_id);
            const RuntimeFunc *setter = resolve_func(index, prop.setter_id);
            if (getter)
                add_reachable(out.reachable, getter->canonical, getter);
            if (setter)
                add_reachable(out.reachable, setter->canonical, setter);
        }

        for (const RuntimeMethod &method : runtime_class.methods) {
            const RuntimeFunc *target = resolve_func(index, method.target_id);
            add_reachable(out.reachable, runtime_class.name + "." + method.name, target);
            if (target) {
                out.explicit_methods[runtime_class.name + "." + method.name].insert(target->id);
            }
        }
    }
    return out;
}

bool is_reachable(const ReachableSurface &reachable,
                  const std::string &canonical,
                  const RuntimeFunc *target) {
    auto it = reachable.reachable.find(canonical);
    return target && it != reachable.reachable.end() && it->second.count(target->id) != 0;
}

bool is_explicit_method(const ReachableSurface &reachable,
                        const std::string &canonical,
                        const RuntimeFunc *target) {
    auto it = reachable.explicit_methods.find(canonical);
    return target && it != reachable.explicit_methods.end() && it->second.count(target->id) != 0;
}

bool check_coverage(const RuntimeSurface &surface, const RuntimeIndex &index) {
    ReachableSurface reachable = build_reachable_names(surface, index);
    std::vector<std::string> failures;

    for (const RuntimeFunc &func : surface.funcs) {
        const RuntimeClass *owner = find_owner_class(index, func.canonical);
        if (!owner)
            continue;
        if (!is_reachable(reachable, func.canonical, &func)) {
            failures.push_back(func.canonical + " -> " + func.id);
        }
    }

    for (const RuntimeAlias &alias : surface.aliases) {
        const RuntimeClass *owner = find_owner_class(index, alias.canonical);
        if (!owner)
            continue;
        const RuntimeFunc *target = resolve_func(index, alias.target_id);
        if (!is_explicit_method(reachable, alias.canonical, target) &&
            !is_reachable(reachable, alias.canonical, target)) {
            failures.push_back(alias.canonical + " -> " + alias.target_id);
        }
    }

    if (failures.empty())
        return true;

    std::cerr << "Class-qualified runtime names are not reachable through RT_CLASS members:\n";
    for (const std::string &failure : failures)
        std::cerr << "  " << failure << "\n";
    return false;
}

bool has_getter_name(const RuntimeClass &runtime_class,
                     const RuntimeIndex &index,
                     const RuntimeProp &prop) {
    if (prop.getter_id == "none")
        return true;
    const RuntimeFunc *getter = resolve_func(index, prop.getter_id);
    return getter && getter->canonical == runtime_class.name + ".get_" + prop.name;
}

std::vector<std::string> setter_method_names(const RuntimeProp &prop) {
    if (!prop.name.empty() && prop.name.front() >= 'a' && prop.name.front() <= 'z')
        return {"set" + upper_first(prop.name)};
    return {"Set" + prop.name};
}

bool has_setter_symmetry(const RuntimeClass &runtime_class,
                         const RuntimeIndex &index,
                         const RuntimeProp &prop) {
    if (prop.setter_id == "none")
        return true;

    if (!resolve_func(index, prop.setter_id))
        return false;

    std::vector<std::string> candidates = setter_method_names(prop);
    for (const RuntimeMethod &method : runtime_class.methods) {
        for (const std::string &candidate : candidates)
            if (method.name == candidate && resolve_func(index, method.target_id))
                return true;
    }

    for (const std::string &candidate : candidates) {
        auto alias_it = index.aliases_by_canonical.find(runtime_class.name + "." + candidate);
        if (alias_it != index.aliases_by_canonical.end() &&
            resolve_func(index, alias_it->second->target_id)) {
            return true;
        }
    }
    return false;
}

bool check_naming_symmetry(const RuntimeSurface &surface, const RuntimeIndex &index) {
    std::vector<std::string> failures;
    for (const RuntimeClass &runtime_class : surface.classes) {
        if (!is_scoped_class(runtime_class.name))
            continue;
        for (const RuntimeProp &prop : runtime_class.props) {
            if (!has_getter_name(runtime_class, index, prop))
                failures.push_back(runtime_class.name + "." + prop.name +
                                   " is missing qualified getter " + runtime_class.name + ".get_" +
                                   prop.name);
            if (!has_setter_symmetry(runtime_class, index, prop))
                failures.push_back(runtime_class.name + "." + prop.name + " setter is missing " +
                                   setter_method_names(prop).front() + " RT_METHOD or RT_ALIAS");
        }
    }

    if (failures.empty())
        return true;

    std::cerr << "Runtime property naming symmetry violations:\n";
    for (const std::string &failure : failures)
        std::cerr << "  " << failure << "\n";
    return false;
}

bool check_method_signatures(const RuntimeSurface &surface, const RuntimeIndex &index) {
    std::vector<std::string> failures;
    for (const RuntimeClass &runtime_class : surface.classes) {
        if (!is_scoped_class(runtime_class.name))
            continue;
        for (const RuntimeMethod &method : runtime_class.methods) {
            const RuntimeFunc *target = resolve_func(index, method.target_id);
            if (!target) {
                failures.push_back(runtime_class.name + "." + method.name +
                                   " targets missing runtime function " + method.target_id);
                continue;
            }
            if (signature_exception_applies(runtime_class.name, method.name, method.target_id))
                continue;

            std::string expected =
                method_signature_for_target(runtime_class, method, target->signature);
            bool matches = normalized_signature(method.signature) == normalized_signature(expected);
            if (!matches && runtime_class.layout == "none") {
                matches = normalized_signature(method.signature) ==
                          normalized_signature(target->signature);
            }
            if (!matches) {
                failures.push_back(runtime_class.name + "." + method.name + " targets " +
                                   method.target_id + ": method signature " + method.signature +
                                   " should be " + expected + " from RT_FUNC " + target->signature);
            }
        }
    }

    if (failures.empty())
        return true;

    std::cerr << "Runtime method signature violations:\n";
    for (const std::string &failure : failures)
        std::cerr << "  " << failure << "\n";
    return false;
}

std::string direct_leaf_name(std::string_view class_name) {
    constexpr std::string_view kViperPrefix = "Viper.";
    if (!starts_with(class_name, kViperPrefix))
        return {};

    std::string_view tail = class_name.substr(kViperPrefix.size());
    size_t dot = tail.rfind('.');
    if (dot == std::string_view::npos)
        return {};
    return std::string(tail.substr(dot + 1));
}

bool check_runtime_class_leaf_names(const RuntimeSurface &surface) {
    std::map<std::string, std::set<std::string>> owners_by_leaf;

    for (const RuntimeClass &runtime_class : surface.classes) {
        std::string leaf = direct_leaf_name(runtime_class.name);
        if (!leaf.empty())
            owners_by_leaf[leaf].insert(runtime_class.name);
    }

    std::vector<std::string> failures;
    for (const auto &entry : owners_by_leaf) {
        if (entry.second.size() > 1) {
            std::ostringstream line;
            line << entry.first << " is exported by";
            for (const std::string &owner : entry.second)
                line << " " << owner;
            failures.push_back(line.str());
        }
    }

    if (failures.empty())
        return true;

    std::cerr << "Runtime class leaf-name collisions:\n";
    for (const std::string &failure : failures)
        std::cerr << "  " << failure << "\n";
    return false;
}

} // namespace

int main() {
    RuntimeSurface surface = load_runtime_surface();
    RuntimeIndex index = build_index(surface);

    bool ok = true;
    ok = check_coverage(surface, index) && ok;
    ok = check_naming_symmetry(surface, index) && ok;
    ok = check_method_signatures(surface, index) && ok;
    ok = check_runtime_class_leaf_names(surface) && ok;

    if (!ok)
        return 1;

    std::cout << "runtime class-qualified surface guardrails passed\n";
    return 0;
}
