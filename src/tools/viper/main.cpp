//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the top-level `viper` driver. The executable dispatches to
// subcommands that run IL programs, compile BASIC, or apply optimizer passes.
// Shared CLI plumbing lives in cli.cpp; this file wires those helpers into the
// `main` entry point and prints user-facing usage information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point and usage utilities for the `viper` driver.
/// @details The translation unit owns only user-interface glue; heavy lifting
///          such as pass management or VM execution is delegated to subcommands.

#include "cli.hpp"
#include "cmd_asset.hpp"
#include "cmd_codegen_arm64.hpp"
#include "cmd_codegen_x64.hpp"
#include "common/PlatformCapabilities.hpp"
#include "il/core/Module.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "support/diag_expected.hpp"
#include "viper/version.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if VIPER_HOST_WINDOWS
#include <cstdlib>
#endif

namespace {

/// @brief Print the viper version banner and runtime configuration summary.
///
/// @details The banner includes the viper version, current IL version, and whether
///          deterministic numerics are enabled. The routine is factored out so
///          both `main` and future subcommands can reuse it when handling
///          `--version` flags.
void printVersion() {
    // Version banner
    std::cout << "viper v" << VIPER_VERSION_STR << "\n";
    if (std::string(VIPER_SNAPSHOT_STR).size())
        std::cout << "snap: " << VIPER_SNAPSHOT_STR << "\n";
    std::cout << "IL current: " << VIPER_IL_VERSION_STR << "\n";
    std::cout << "IL supported: " << VIPER_IL_VERSION_STR << "\n";
    std::cout << "Precise Numerics: enabled\n";
}

} // namespace

namespace {
/// @brief Implement `--dump-runtime-descriptors`: print the runtime descriptor
///        table in a stable, sorted format to stdout.
/// @return 0 on success.
int dumpRuntimeDescriptors() {
    using il::runtime::findRuntimeSignatureId;
    using il::runtime::RuntimeDescriptor;
    using il::runtime::runtimeRegistry;

    const auto &reg = runtimeRegistry();

    struct DescriptorGroup {
        std::optional<il::runtime::RtSig> sig{};
        il::runtime::RuntimeHandler handler{nullptr};
        std::vector<const RuntimeDescriptor *> descriptors;
        size_t firstIndex{0};

        bool matches(std::optional<il::runtime::RtSig> otherSig,
                     il::runtime::RuntimeHandler otherHandler) const noexcept {
            return sig == otherSig && handler == otherHandler;
        }
    };

    std::vector<DescriptorGroup> groups;
    groups.reserve(reg.size());

    for (size_t i = 0; i < reg.size(); ++i) {
        const auto &d = reg[i];
        const auto sig = findRuntimeSignatureId(d.name);
        auto it = std::find_if(groups.begin(), groups.end(), [&](const DescriptorGroup &group) {
            return group.matches(sig, d.handler);
        });
        if (it == groups.end()) {
            DescriptorGroup group;
            group.sig = sig;
            group.handler = d.handler;
            group.firstIndex = i;
            group.descriptors.push_back(&d);
            groups.push_back(std::move(group));
        } else {
            it->descriptors.push_back(&d);
        }
    }

    auto typeListToString = [](const std::vector<il::core::Type> &ts) -> std::string {
        std::ostringstream os;
        for (size_t i = 0; i < ts.size(); ++i) {
            if (i)
                os << ',';
            os << ts[i].toString();
        }
        return os.str();
    };

    auto effectsToString = [](const il::runtime::RuntimeSignature &s,
                              il::runtime::RuntimeTrapClass trap) -> std::string {
        std::vector<std::string> items;
        items.push_back(s.nothrow ? "NoThrow" : "MayThrow");
        if (s.readonly)
            items.push_back("ReadOnly");
        if (s.pure)
            items.push_back("Pure");
        if (trap != il::runtime::RuntimeTrapClass::None) {
            switch (trap) {
                case il::runtime::RuntimeTrapClass::PowDomainOverflow:
                    items.emplace_back("Trap:PowDomainOverflow");
                    break;
                default:
                    items.emplace_back("Trap:Unknown");
                    break;
            }
        }
        if (items.empty())
            return std::string("None");
        std::ostringstream os;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i)
                os << ", ";
            os << items[i];
        }
        return os.str();
    };

    std::sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) {
        return a.firstIndex < b.firstIndex;
    });

    for (const auto &entry : groups) {
        const auto &vec = entry.descriptors;
        // choose canonical: prefer Viper.* else first
        const RuntimeDescriptor *canonical = vec.front();
        for (const auto *d : vec) {
            if (d->name.rfind("Viper.", 0) == 0) {
                canonical = d;
                break;
            }
        }

        // collect aliases (rt_* only)
        std::vector<std::string_view> aliases;
        for (const auto *d : vec) {
            if (d == canonical)
                continue;
            if (d->name.rfind("rt_", 0) == 0)
                aliases.push_back(d->name);
        }

        // Print block
        std::cout << "NAME: " << canonical->name << "\n";

        std::cout << "  ALIASES: ";
        if (aliases.empty()) {
            std::cout << "(none)\n";
        } else {
            for (size_t i = 0; i < aliases.size(); ++i) {
                if (i)
                    std::cout << ", ";
                std::cout << aliases[i];
            }
            std::cout << "\n";
        }

        const auto &sig = canonical->signature;
        std::cout << "  SIGNATURE: " << sig.retType.toString() << '('
                  << typeListToString(sig.paramTypes) << ")\n";
        std::cout << "  EFFECTS: " << effectsToString(sig, canonical->trapClass) << "\n";
    }
    return 0;
}
} // namespace

namespace {
/// @brief Return true when @p text begins with @p prefix.
/// @details The runtime API dump uses this helper for portable namespace and
///          token classification without relying on platform-specific APIs.
/// @param text Candidate text to inspect.
/// @param prefix Prefix to test.
/// @return True if @p text starts with @p prefix.
bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/// @brief Return true when @p text ends with @p suffix.
/// @details Used for compact signature and public-name classification.
/// @param text Candidate text to inspect.
/// @param suffix Suffix to test.
/// @return True if @p text ends with @p suffix.
bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

/// @brief Return the last dot-separated segment of a runtime name.
/// @details `Viper.Network.Http.Get` becomes `Get`; names without dots are
///          returned unchanged.
/// @param name Public runtime name or member name.
/// @return View covering the final segment.
std::string_view lastRuntimeNameSegment(std::string_view name) {
    const size_t pos = name.rfind('.');
    if (pos == std::string_view::npos)
        return name;
    return name.substr(pos + 1);
}

/// @brief Return the owner namespace/class path for a public runtime name.
/// @details `Viper.Network.Http.Get` becomes `Viper.Network.Http`.
/// @param name Public runtime name.
/// @return Owning namespace/class path, or an empty string for root names.
std::string runtimeApiOwner(std::string_view name) {
    const size_t pos = name.rfind('.');
    if (pos == std::string_view::npos)
        return {};
    return std::string(name.substr(0, pos));
}

/// @brief Build a lowercase ASCII slug from a public runtime name.
/// @details Non-alphanumeric runs collapse to one dash. The result is a
///          best-effort Markdown anchor hint for generated docs links.
/// @param text Runtime name, class name, or phrase.
/// @return Lowercase slug string.
std::string runtimeApiSlug(std::string_view text) {
    std::string slug;
    bool previousDash = false;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            slug.push_back(static_cast<char>(std::tolower(ch)));
            previousDash = false;
        } else if (!previousDash) {
            slug.push_back('-');
            previousDash = true;
        }
    }
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    while (!slug.empty() && slug.front() == '-')
        slug.erase(slug.begin());
    return slug;
}

/// @brief Split a comma-separated runtime type list at top-level commas.
/// @details Generic-looking tokens such as `seq<str>` and
///          `obj<Viper.Collections.Seq>` may contain nested punctuation; this
///          helper preserves those tokens while separating parameters.
/// @param text Text inside a signature's parentheses.
/// @return Individual parameter type tokens.
std::vector<std::string> splitRuntimeTypeList(std::string_view text) {
    std::vector<std::string> out;
    size_t start = 0;
    int angleDepth = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size()) {
            if (text[i] == '<') {
                ++angleDepth;
            } else if (text[i] == '>' && angleDepth > 0) {
                --angleDepth;
            }
        }
        if (i == text.size() || (text[i] == ',' && angleDepth == 0)) {
            std::string item(text.substr(start, i - start));
            item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char c) {
                           return !std::isspace(c);
                       }));
            item.erase(std::find_if(item.rbegin(),
                                    item.rend(),
                                    [](unsigned char c) { return !std::isspace(c); })
                           .base(),
                       item.end());
            if (!item.empty())
                out.push_back(std::move(item));
            start = i + 1;
        }
    }
    return out;
}

/// @brief Parsed view of the compact runtime.def signature dialect.
/// @details The public dump keeps exact signature text and adds this parsed
///          structure so tools do not need to implement their own ad hoc parser.
struct RuntimeApiSignatureParts {
    std::string returnType;              ///< Return type token before `(`.
    std::vector<std::string> paramTypes; ///< Parameter type tokens inside `(...)`.
    bool valid{false};                   ///< True when the signature had `ret(args)` shape.
};

/// @brief Parse a compact runtime.def signature.
/// @param signature Signature text such as `str(i64,str)`.
/// @return Parsed return and parameter tokens, or `valid=false` when malformed.
RuntimeApiSignatureParts parseRuntimeApiSignature(std::string_view signature) {
    RuntimeApiSignatureParts parts;
    const size_t open = signature.find('(');
    const size_t close = signature.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open)
        return parts;
    parts.returnType = std::string(signature.substr(0, open));
    parts.paramTypes = splitRuntimeTypeList(signature.substr(open + 1, close - open - 1));
    parts.valid = true;
    return parts;
}

/// @brief Emit a JSON string array.
/// @param os Stream receiving JSON.
/// @param values Values to emit as JSON strings.
void emitStringArrayJson(std::ostream &os, const std::vector<std::string> &values) {
    using il::support::printJsonStringEscaped;
    os << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            os << ',';
        printJsonStringEscaped(os, values[i]);
    }
    os << ']';
}

/// @brief Emit structured JSON metadata for one runtime type token.
/// @details The `raw` field always preserves the exact public spelling. The
///          `nullable` flag records current suffix-nullable spellings without
///          requiring tools to guess what punctuation means.
/// @param os Stream receiving JSON.
/// @param rawType Type token from a public signature.
void emitRuntimeApiTypeJson(std::ostream &os, std::string_view rawType) {
    using il::support::printJsonStringEscaped;

    std::string type(rawType);
    bool nullable = false;
    if (!type.empty() && type.back() == '?') {
        nullable = true;
        type.pop_back();
    }

    os << "{\"raw\":";
    printJsonStringEscaped(os, rawType);
    os << ",\"kind\":";

    if (type == "void") {
        printJsonStringEscaped(os, "void");
    } else if (type == "i1") {
        printJsonStringEscaped(os, "boolean");
    } else if (type == "i8" || type == "i16" || type == "i32" || type == "i64") {
        printJsonStringEscaped(os, "integer");
    } else if (type == "f32" || type == "f64") {
        printJsonStringEscaped(os, "number");
    } else if (type == "str") {
        printJsonStringEscaped(os, "string");
    } else if (startsWith(type, "seq<") && endsWith(type, ">")) {
        printJsonStringEscaped(os, "sequence");
        os << ",\"element_type\":";
        emitRuntimeApiTypeJson(os, std::string_view(type).substr(4, type.size() - 5));
    } else if (startsWith(type, "obj<") && endsWith(type, ">")) {
        printJsonStringEscaped(os, "object");
        os << ",\"class\":";
        printJsonStringEscaped(os, std::string_view(type).substr(4, type.size() - 5));
    } else if (type == "obj") {
        printJsonStringEscaped(os, "object");
    } else if (type == "ptr") {
        printJsonStringEscaped(os, "pointer");
    } else {
        printJsonStringEscaped(os, "unknown");
    }

    os << ",\"nullable\":" << (nullable ? "true" : "false") << '}';
}

/// @brief Infer the documentation file that owns a runtime API row.
/// @details This first implementation is deliberately coarse. It gives tools a
///          stable link target while exact per-member anchors are added to docs.
/// @param name Public runtime name or class name.
/// @return Repository-relative documentation path.
std::string docsFileForRuntimeName(std::string_view name) {
    if (startsWith(name, "Viper.Collections."))
        return "docs/viperlib/collections/README.md";
    if (startsWith(name, "Viper.Crypto."))
        return "docs/viperlib/crypto.md";
    if (startsWith(name, "Viper.Data.") || startsWith(name, "Viper.Text."))
        return "docs/viperlib/text/README.md";
    if (startsWith(name, "Viper.Diagnostics."))
        return "docs/viperlib/diagnostics.md";
    if (startsWith(name, "Viper.Game3D.") || startsWith(name, "Viper.Graphics3D."))
        return "docs/viperlib/graphics/rendering3d.md";
    if (startsWith(name, "Viper.Game2D.") || startsWith(name, "Viper.Graphics2D."))
        return "docs/viperlib/graphics/rendering2d.md";
    if (startsWith(name, "Viper.Game."))
        return "docs/viperlib/game.md";
    if (startsWith(name, "Viper.Graphics."))
        return "docs/viperlib/graphics/README.md";
    if (startsWith(name, "Viper.GUI."))
        return "docs/viperlib/gui/README.md";
    if (startsWith(name, "Viper.Input."))
        return "docs/viperlib/input.md";
    if (startsWith(name, "Viper.IO.") || startsWith(name, "Viper.Assets."))
        return "docs/viperlib/io/README.md";
    if (startsWith(name, "Viper.Localization."))
        return "docs/viperlib/localization/README.md";
    if (startsWith(name, "Viper.Math."))
        return "docs/viperlib/math.md";
    if (startsWith(name, "Viper.Network."))
        return "docs/viperlib/network.md";
    if (startsWith(name, "Viper.Sound."))
        return "docs/viperlib/audio.md";
    if (startsWith(name, "Viper.Threads."))
        return "docs/viperlib/threads.md";
    if (startsWith(name, "Viper.Time."))
        return "docs/viperlib/time.md";
    if (startsWith(name, "Viper.Zia."))
        return "docs/viperlib/zia.md";
    if (startsWith(name, "Viper.System.") || startsWith(name, "Viper.Memory.") ||
        startsWith(name, "Viper.Terminal."))
        return "docs/viperlib/system.md";
    return "docs/viperlib/core.md";
}

/// @brief Emit a best-effort docs anchor hint for a runtime row.
/// @param os Stream receiving JSON.
/// @param name Runtime API row name.
void emitDocsAnchorJson(std::ostream &os, std::string_view name) {
    std::string anchor = docsFileForRuntimeName(name);
    anchor += '#';
    anchor += runtimeApiSlug(name);
    il::support::printJsonStringEscaped(os, anchor);
}

/// @brief Infer capability tags for a runtime API row from its namespace.
/// @details Tags are broad and additive; future source-authored registry
///          metadata can replace these heuristics without changing consumers.
/// @param name Public runtime name or class name.
/// @return Ordered capability tags.
std::vector<std::string> inferRuntimeCapabilities(std::string_view name) {
    std::vector<std::string> caps;
    if (startsWith(name, "Viper.Graphics3D.") || startsWith(name, "Viper.Game3D.")) {
        caps.push_back("graphics");
        caps.push_back("graphics3d");
    } else if (startsWith(name, "Viper.Graphics2D.") || startsWith(name, "Viper.Game2D.")) {
        caps.push_back("graphics");
        caps.push_back("graphics2d");
    } else if (startsWith(name, "Viper.Graphics.")) {
        caps.push_back("graphics");
    }
    if (startsWith(name, "Viper.GUI."))
        caps.push_back("gui");
    if (startsWith(name, "Viper.Game."))
        caps.push_back("game");
    if (startsWith(name, "Viper.Input."))
        caps.push_back("input");
    if (startsWith(name, "Viper.Network."))
        caps.push_back("network");
    if (startsWith(name, "Viper.Crypto."))
        caps.push_back("crypto");
    if (startsWith(name, "Viper.IO.") || startsWith(name, "Viper.Assets."))
        caps.push_back("filesystem");
    if (startsWith(name, "Viper.Sound."))
        caps.push_back("audio");
    if (startsWith(name, "Viper.Threads."))
        caps.push_back("threads");
    if (startsWith(name, "Viper.Localization."))
        caps.push_back("localization");
    if (startsWith(name, "Viper.Zia.") || startsWith(name, "Viper.Basic.") ||
        startsWith(name, "Viper.Project.") || startsWith(name, "Viper.Workspace."))
        caps.push_back("tooling");
    if (caps.empty())
        caps.push_back("core");
    return caps;
}

/// @brief Return the preferred public replacement for a compatibility row.
/// @details The overhaul preserves old API names for source compatibility, but
///          tools need a machine-readable pointer to the modern spelling. An
///          empty result means the row is not a compatibility alias or no
///          direct one-for-one replacement exists.
/// @param name Public runtime name or class name.
/// @return Preferred replacement runtime name, or empty string.
std::string inferRuntimeMigrationTarget(std::string_view name) {
    const std::string_view leaf = lastRuntimeNameSegment(name);

    if (name == "Viper.Math.Bits.LeadZ")
        return "Viper.Math.Bits.CountLeadingZeros";
    if (name == "Viper.Math.Bits.TrailZ")
        return "Viper.Math.Bits.CountTrailingZeros";
    if (name == "Viper.Math.Bits.Rotl")
        return "Viper.Math.Bits.RotateLeft";
    if (name == "Viper.Math.Bits.Rotr")
        return "Viper.Math.Bits.RotateRight";
    if (name == "Viper.Math.Bits.Ushr")
        return "Viper.Math.Bits.ShiftRightLogical";
    if (name == "Viper.Collections.BloomFilter.Fpr")
        return "Viper.Collections.BloomFilter.FalsePositiveRate";
    if (name == "Viper.Core.Convert.ToString_Int")
        return "Viper.Core.Convert.ToStringInt";
    if (name == "Viper.Core.Convert.ToString_Double")
        return "Viper.Core.Convert.ToStringDouble";
    if (name == "Viper.Core.Parse.TryNum")
        return "Viper.Core.Parse.TryDouble";
    if (name == "Viper.Core.Parse.NumOr")
        return "Viper.Core.Parse.DoubleOr";
    if (name == "Viper.Text.Fmt.NumSci")
        return "Viper.Text.Fmt.Scientific";
    if (name == "Viper.Text.Fmt.NumPct")
        return "Viper.Text.Fmt.Percent";
    if (name == "Viper.Text.Fmt.BoolYN")
        return "Viper.Text.Fmt.YesNo";

    if (name == "Viper.Terminal.ReadLine" || name == "Viper.Terminal.InputLine")
        return "Viper.Terminal.TryReadLine";
    if (name == "Viper.Terminal.Ask")
        return "Viper.Terminal.TryAsk";

    if (name == "Viper.Collections.LruCache.get_Cap")
        return "Viper.Collections.LruCache.get_Capacity";
    if (name == "Viper.Collections.Ring.get_Cap")
        return "Viper.Collections.Ring.get_Capacity";
    if (name == "Viper.Collections.Seq.get_Cap")
        return "Viper.Collections.Seq.get_Capacity";
    if (name == "Viper.Collections.Deque.get_Cap")
        return "Viper.Collections.Deque.get_Capacity";
    if (name == "Viper.Threads.Channel.get_Cap")
        return "Viper.Threads.Channel.get_Capacity";
    if (name == "Viper.IO.BinaryBuffer.NewCap")
        return "Viper.IO.BinaryBuffer.NewCapacity";

    if (name == "Viper.Collections.LruCache.Put")
        return "Viper.Collections.LruCache.Set";
    if (name == "Viper.Collections.BiMap.Put")
        return "Viper.Collections.BiMap.Set";
    if (name == "Viper.Collections.MultiMap.Put")
        return "Viper.Collections.MultiMap.Add";

    if (name == "Viper.Graphics.Canvas.SetDTMax")
        return "Viper.Graphics.Canvas.SetMaxDeltaTime";
    if (name == "Viper.Graphics3D.Canvas3D.SetDTMax")
        return "Viper.Graphics3D.Canvas3D.SetMaxDeltaTime";

    if (name == "Viper.Graphics3D.Mesh3D.NewBox")
        return "Viper.Graphics3D.Mesh3D.Box";
    if (name == "Viper.Graphics3D.Mesh3D.NewSphere")
        return "Viper.Graphics3D.Mesh3D.Sphere";
    if (name == "Viper.Graphics3D.Mesh3D.NewPlane")
        return "Viper.Graphics3D.Mesh3D.Plane";
    if (name == "Viper.Graphics3D.Mesh3D.NewCylinder")
        return "Viper.Graphics3D.Mesh3D.Cylinder";
    if (name == "Viper.Graphics3D.Material3D.NewColor")
        return "Viper.Graphics3D.Material3D.FromColor";
    if (name == "Viper.Graphics3D.Material3D.NewTextured")
        return "Viper.Graphics3D.Material3D.Textured";
    if (name == "Viper.Graphics3D.Material3D.NewPBR")
        return "Viper.Graphics3D.Material3D.PBR";
    if (name == "Viper.Graphics3D.Light3D.NewDirectional")
        return "Viper.Graphics3D.Light3D.Directional";
    if (name == "Viper.Graphics3D.Light3D.NewPoint")
        return "Viper.Graphics3D.Light3D.Point";
    if (name == "Viper.Graphics3D.Light3D.NewAmbient")
        return "Viper.Graphics3D.Light3D.Ambient";
    if (name == "Viper.Graphics3D.Light3D.NewSpot")
        return "Viper.Graphics3D.Light3D.Spot";
    if (name == "Viper.Graphics3D.Collider3D.NewBox")
        return "Viper.Graphics3D.Collider3D.Box";
    if (name == "Viper.Graphics3D.Collider3D.NewSphere")
        return "Viper.Graphics3D.Collider3D.Sphere";
    if (name == "Viper.Graphics3D.Collider3D.NewCapsule")
        return "Viper.Graphics3D.Collider3D.Capsule";
    if (name == "Viper.Graphics3D.World3D.NewWithCamera")
        return "Viper.Graphics3D.World3D.WithCamera";
    if (name == "Viper.Graphics3D.World3D.NewWithHorizontalCamera")
        return "Viper.Graphics3D.World3D.WithHorizontalCamera";

    if (startsWith(name, "Viper.Game3D.Keys"))
        return "Viper.Input.Key";
    if (name == "Viper.Game3D.Assets3D.LoadTemplate")
        return "Viper.Game3D.Prefab.Load";
    if (name == "Viper.Game3D.Assets3D.LoadTemplateAsset")
        return "Viper.Game3D.Prefab.LoadAsset";
    if (name == "Viper.Game3D.Assets3D.LoadTemplateAsync")
        return "Viper.Game3D.Prefab.LoadAsync";
    if (name == "Viper.Game3D.Assets3D.LoadTemplateAssetAsync")
        return "Viper.Game3D.Prefab.LoadAssetAsync";
    if (name == "Viper.Game3D.AssetHandle3D.GetTemplate")
        return "Viper.Game3D.AssetHandle3D.GetPrefab";

    if (name == "Viper.Crypto.Hash.CRC32")
        return "Viper.Crypto.Legacy.Hash.CRC32";
    if (name == "Viper.Crypto.Hash.CRC32Bytes")
        return "Viper.Crypto.Legacy.Hash.CRC32Bytes";
    if (name == "Viper.Crypto.Hash.MD5")
        return "Viper.Crypto.Legacy.Hash.MD5";
    if (name == "Viper.Crypto.Hash.MD5Bytes")
        return "Viper.Crypto.Legacy.Hash.MD5Bytes";
    if (name == "Viper.Crypto.Hash.SHA1")
        return "Viper.Crypto.Legacy.Hash.SHA1";
    if (name == "Viper.Crypto.Hash.SHA1Bytes")
        return "Viper.Crypto.Legacy.Hash.SHA1Bytes";
    if (name == "Viper.Crypto.Hash.HmacMD5")
        return "Viper.Crypto.Legacy.Hash.HmacMD5";
    if (name == "Viper.Crypto.Hash.HmacMD5Bytes")
        return "Viper.Crypto.Legacy.Hash.HmacMD5Bytes";
    if (name == "Viper.Crypto.Hash.HmacSHA1")
        return "Viper.Crypto.Legacy.Hash.HmacSHA1";
    if (name == "Viper.Crypto.Hash.HmacSHA1Bytes")
        return "Viper.Crypto.Legacy.Hash.HmacSHA1Bytes";

    if (name == "Viper.Crypto.Aes.Encrypt")
        return "Viper.Crypto.Legacy.Aes.EncryptCBC";
    if (name == "Viper.Crypto.Aes.Decrypt")
        return "Viper.Crypto.Legacy.Aes.DecryptCBC";
    if (name == "Viper.Crypto.Aes.DecryptResult")
        return "Viper.Crypto.Legacy.Aes.DecryptCBCResult";
    if (name == "Viper.Crypto.Aes.TryDecrypt")
        return "Viper.Crypto.Legacy.Aes.TryDecryptCBC";

    if (name == "Viper.Math.Random.ChanceInt")
        return "Viper.Math.Random.Chance";

    if (name == "Viper.Collections.Queue.TryPop")
        return "Viper.Collections.Queue.TryPopOption";
    if (name == "Viper.Collections.Bytes.Find")
        return "Viper.Collections.Bytes.FindOption";
    if (name == "Viper.Collections.List.Find")
        return "Viper.Collections.List.FindOption";
    if (name == "Viper.Collections.Seq.Find")
        return "Viper.Collections.Seq.FindOption";
    if (name == "Viper.Collections.Seq.FindWhere")
        return "Viper.Collections.Seq.FindWhereOption";
    if (name == "Viper.Collections.UnionFind.Find")
        return "Viper.Collections.UnionFind.FindRootOption";
    if (name == "Viper.Collections.Heap.TryPeek")
        return "Viper.Collections.Heap.TryPeekOption";
    if (name == "Viper.Collections.Heap.TryPop")
        return "Viper.Collections.Heap.TryPopOption";
    if (name == "Viper.Collections.Stack.TryPop")
        return "Viper.Collections.Stack.TryPopOption";
    if (name == "Viper.Collections.Deque.TryPopFront")
        return "Viper.Collections.Deque.TryPopFrontOption";
    if (name == "Viper.Collections.Deque.TryPopBack")
        return "Viper.Collections.Deque.TryPopBackOption";
    if (name == "Viper.Threads.Channel.TryRecv")
        return "Viper.Threads.Channel.TryRecvOption";
    if (name == "Viper.Threads.ConcurrentQueue.TryDequeue")
        return "Viper.Threads.ConcurrentQueue.TryDequeueOption";
    if (name == "Viper.Threads.Future.TryGet")
        return "Viper.Threads.Future.TryGetOption";
    if (name == "Viper.Time.DateTime.TryParse")
        return "Viper.Time.DateTime.TryParseOption";
    if (name == "Viper.Localization.Locale.TryParse")
        return "Viper.Localization.Locale.TryParseOption";
    if (name == "Viper.Localization.MessageBundle.TryGet")
        return "Viper.Localization.MessageBundle.TryGetOption";
    if (name == "Viper.Functional.LazySeq.Find")
        return "Viper.Functional.LazySeq.FindOption";

    if (name == "Viper.Data.Xml.Find")
        return "Viper.Data.Xml.FindOption";
    if (name == "Viper.Text.Pattern.Find")
        return "Viper.Text.Pattern.FindOption";
    if (name == "Viper.Text.Pattern.FindFrom")
        return "Viper.Text.Pattern.FindFromOption";
    if (name == "Viper.Text.Pattern.FindPos")
        return "Viper.Text.Pattern.FindPosOption";
    if (name == "Viper.Text.CompiledPattern.Find")
        return "Viper.Text.CompiledPattern.FindOption";
    if (name == "Viper.Text.CompiledPattern.FindFrom")
        return "Viper.Text.CompiledPattern.FindFromOption";
    if (name == "Viper.Text.CompiledPattern.FindPos")
        return "Viper.Text.CompiledPattern.FindPosOption";

    if (name == "Viper.Game2D.SceneDocument.FindObject")
        return "Viper.Game2D.SceneDocument.FindObjectOption";
    if (name == "Viper.Graphics2D.SceneNode.Find")
        return "Viper.Graphics2D.SceneNode.FindOption";
    if (name == "Viper.Graphics2D.SceneGraph.Find")
        return "Viper.Graphics2D.SceneGraph.FindOption";
    if (name == "Viper.Sound.Audio.FindGroup")
        return "Viper.Sound.Audio.FindGroupOption";
    if (name == "Viper.Graphics3D.SceneGraph.Find")
        return "Viper.Graphics3D.SceneGraph.FindOption";
    if (name == "Viper.Graphics3D.SceneNode.Find")
        return "Viper.Graphics3D.SceneNode.FindOption";
    if (name == "Viper.Graphics3D.Skeleton3D.FindBone")
        return "Viper.Graphics3D.Skeleton3D.FindBoneOption";
    if (name == "Viper.Graphics3D.SceneAsset.FindNode")
        return "Viper.Graphics3D.SceneAsset.FindNodeOption";
    if (name == "Viper.Game3D.World3D.FindNode")
        return "Viper.Game3D.World3D.FindNodeOption";
    if (name == "Viper.Game3D.World3D.FindEntity")
        return "Viper.Game3D.World3D.FindEntityOption";
    if (name == "Viper.Graphics3D.NavMesh3D.FindPath")
        return "Viper.Graphics3D.NavMesh3D.FindPathOption";
    if (name == "Viper.Graphics3D.SceneAsset.Load" ||
        name == "Viper.Graphics3D.AssetDiagnostics3D.get_LastLoadError" ||
        name == "Viper.Graphics3D.AssetDiagnostics3D.get_LastLoadErrorCode")
        return "Viper.Graphics3D.SceneAsset.LoadResult";
    if (name == "Viper.Graphics3D.SceneAsset.LoadAsset")
        return "Viper.Graphics3D.SceneAsset.LoadAssetResult";
    if (name == "Viper.Graphics3D.SceneAsset.LoadAnimation")
        return "Viper.Graphics3D.SceneAsset.LoadAnimationResult";
    if (name == "Viper.Graphics3D.SceneAsset.LoadAnimationAsset")
        return "Viper.Graphics3D.SceneAsset.LoadAnimationAssetResult";
    if (name == "Viper.Graphics3D.SceneAsset.LoadNodeAnimation")
        return "Viper.Graphics3D.SceneAsset.LoadNodeAnimationResult";
    if (name == "Viper.Graphics3D.SceneAsset.LoadNodeAnimationAsset")
        return "Viper.Graphics3D.SceneAsset.LoadNodeAnimationAssetResult";
    if (name == "Viper.Zia.SemanticJob.Error")
        return "Viper.Zia.SemanticJob.ErrorOption";

    if (name == "Viper.Crypto.Module.EnableApprovedMode")
        return "Viper.Crypto.Module.EnableApprovedModeForProcess";
    if (name == "Viper.Crypto.Module.DisableApprovedMode")
        return "Viper.Crypto.Module.DisableApprovedModeForProcess";
    if (name == "Viper.Crypto.Module.IsApprovedMode")
        return "Viper.Crypto.Module.IsApprovedModeForProcess";

    if (name == "Viper.Core.Box.ValueType")
        return "Viper.Runtime.Unsafe.ValueType";
    if (name == "Viper.Core.Box.ValueTypeAddField" || name == "Viper.Core.ValueType.AddField")
        return "Viper.Runtime.Unsafe.ValueTypeAddField";

    if (name == "Viper.Network.HttpReq.SetTlsVerify")
        return "Viper.Network.HttpReq.AllowInsecureCertificatesForTesting";
    if (name == "Viper.Network.RestClient.LastStatus" ||
        name == "Viper.Network.RestClient.LastResponse" ||
        name == "Viper.Network.RestClient.LastOk")
        return "Viper.Network.RestClient.GetResult";
    if (name == "Viper.System.Exec.ShellFull" || name == "Viper.System.Exec.LastExitCode")
        return "Viper.System.Exec.ShellResult";
    if (name == "Viper.Network.SmtpClient.Send" || name == "Viper.Network.SmtpClient.get_LastError")
        return "Viper.Network.SmtpClient.SendResult";
    if (name == "Viper.Network.SmtpClient.SendHtml")
        return "Viper.Network.SmtpClient.SendHtmlResult";
    if (name == "Viper.Game.UI.Table.HandleClick" || name == "Viper.Game.UI.Table.LastHeaderClick")
        return "Viper.Game.UI.Table.HandleClickResult";
    if (name == "Viper.GUI.FindBar.FindNext")
        return "Viper.GUI.FindBar.FindNextOption";
    if (name == "Viper.GUI.FindBar.FindPrev")
        return "Viper.GUI.FindBar.FindPrevOption";
    if (name == "Viper.GUI.TestHarness.FindById")
        return "Viper.GUI.TestHarness.FindByIdOption";
    if (name == "Viper.GUI.TestHarness.FindByName")
        return "Viper.GUI.TestHarness.FindByNameOption";
    if (name == "Viper.GUI.TestHarness.FindByType")
        return "Viper.GUI.TestHarness.FindByTypeOption";
    if (name == "Viper.GUI.CommandRegistry.Find")
        return "Viper.GUI.CommandRegistry.FindOption";

    if (name == "Viper.Game.Pathfinder.get_LastFound" ||
        name == "Viper.Game.Pathfinder.get_LastSteps")
        return "Viper.Game.Pathfinder.FindPathResult";
    if (name == "Viper.Game.Pathfinder.FindPath" || name == "Viper.Game.Pathfinder.FindPathLength")
        return "Viper.Game.Pathfinder.FindPathResult";
    if (name == "Viper.Game.Pathfinder.FindNearest")
        return "Viper.Game.Pathfinder.FindNearestResult";
    if (name == "Viper.Game.PathResult.get_Length" || name == "Viper.Game.PathResult.Length")
        return "Viper.Game.PathResult.get_StepCount";
    if (name == "Viper.Game.Quadtree.GetResult" || name == "Viper.Game.Quadtree.get_ResultCount" ||
        name == "Viper.Game.Quadtree.QueryWasTruncated")
        return "Viper.Game.Quadtree.QueryRectResult";
    if (name == "Viper.Game.Quadtree.GetPairs" || name == "Viper.Game.Quadtree.PairFirst" ||
        name == "Viper.Game.Quadtree.PairSecond" || name == "Viper.Game.Quadtree.PairsWasTruncated")
        return "Viper.Game.Quadtree.QueryPairs";
    if (name == "Viper.Game.AnimStateMachine.EventsFiredCount" ||
        name == "Viper.Game.AnimStateMachine.EventFiredId" ||
        name == "Viper.Game.AnimStateMachine.get_EventFired")
        return "Viper.Game.AnimStateMachine.PollEvents";
    if (name == "Viper.Game.AnimTimeline.EventsFiredCount" ||
        name == "Viper.Game.AnimTimeline.EventFiredId")
        return "Viper.Game.AnimTimeline.PollEvents";

    if (name == "Viper.Text.JsonStream.Error")
        return "Viper.Text.JsonStream.NextResult";
    if (name == "Viper.Data.Xml.Error")
        return "Viper.Data.Xml.ParseResult";
    if (name == "Viper.Data.Yaml.Error")
        return "Viper.Data.Yaml.ParseResult";
    if (name == "Viper.Data.Serialize.Error")
        return "Viper.Data.Serialize.ParseResult";

    if (name == "Viper.Crypto.Tls.Error")
        return "Viper.Crypto.Tls.ConnectResult";
    if (name == "Viper.System.Pty.LastError")
        return "Viper.System.Pty.OpenResult";
    if (name == "Viper.Game2D.SceneDocument.LastError")
        return "Viper.Game2D.SceneDocument.LoadResult";

    if (name == "Viper.Memory.Retain")
        return "Viper.Runtime.Unsafe.Retain";
    if (name == "Viper.Memory.Release")
        return "Viper.Runtime.Unsafe.Release";
    if (name == "Viper.Memory.RetainStr")
        return "Viper.Runtime.Unsafe.RetainStr";
    if (name == "Viper.Memory.ReleaseStr")
        return "Viper.Runtime.Unsafe.ReleaseStr";

    if (name == "Viper.Error.SetThrowMsg")
        return "Viper.Runtime.Unsafe.SetThrowMsg";
    if (name == "Viper.Error.ClearThrowMsg")
        return "Viper.Runtime.Unsafe.ClearThrowMsg";
    if (name == "Viper.Error.SetTrapFields")
        return "Viper.Runtime.Unsafe.SetTrapFields";
    if (name == "Viper.Error.RaiseKind")
        return "Viper.Runtime.Unsafe.RaiseKind";
    if (startsWith(name, "Viper.Error.GetTrap") || name == "Viper.Error.GetThrowMsg")
        return "Viper.Diagnostics.CurrentTrap";

    (void)leaf;
    return {};
}

/// @brief Infer a stability tier for a runtime API row.
/// @details Classifications are discovery metadata only; they do not remove,
///          hide, or disable any existing API.
/// @param name Public runtime name or class name.
/// @return Stability tier string.
std::string inferRuntimeStability(std::string_view name) {
    const std::string_view leaf = lastRuntimeNameSegment(name);
    if (leaf == "AllowInsecureCertificatesForTesting" || leaf == "EnableApprovedMode" ||
        leaf == "DisableApprovedMode" || leaf == "EnableApprovedModeForProcess" ||
        leaf == "DisableApprovedModeForProcess" || name == "Viper.Core.Box.ValueType" ||
        name == "Viper.Core.Box.ValueTypeAddField" || name == "Viper.Core.ValueType.AddField" ||
        name == "Viper.Network.HttpReq.SetTlsVerify" || startsWith(name, "Viper.Runtime.Unsafe.") ||
        startsWith(name, "Viper.Memory.Retain") || startsWith(name, "Viper.Memory.Release") ||
        startsWith(name, "Viper.Error.Set") || startsWith(name, "Viper.Error.Clear") ||
        startsWith(name, "Viper.Error.Raise"))
        return "unsafe";
    if (!inferRuntimeMigrationTarget(name).empty())
        return "legacy";
    if (startsWith(name, "Viper.Crypto.Legacy."))
        return "legacy";
    if (leaf == "MD5" || leaf == "SHA1" || leaf == "HmacMD5" || leaf == "HmacSHA1" ||
        leaf == "LastError" || leaf == "LastStatus" || leaf == "LastResponse" || leaf == "LastOk")
        return "legacy";
    if (startsWith(name, "Viper.Zia.") || startsWith(name, "Viper.Basic.") ||
        startsWith(name, "Viper.Project.") || startsWith(name, "Viper.Workspace.") ||
        startsWith(name, "Viper.Assets.") || startsWith(name, "Viper.GUI.") ||
        startsWith(name, "Viper.Game3D.") || startsWith(name, "Viper.Graphics3D."))
        return "preview";
    return "stable";
}

/// @brief Return whether a row exposes diagnostic or mutable last-result state.
/// @details This uses exact public names instead of broad `Error`/`Last`
///          prefixes so ordinary APIs such as `Diagnostics.Log.Error`,
///          `String.LastIndexOf`, and collection `Last()` methods are not
///          reported as side-channel diagnostics.
/// @param name Public runtime name.
/// @param leaf Last dotted segment of @p name.
/// @return True when the row is side-channel diagnostic state.
bool isRuntimeSideChannelDiagnostic(std::string_view name, std::string_view leaf) {
    if (name == "Viper.Crypto.Tls.Error" || name == "Viper.Text.JsonStream.Error" ||
        name == "Viper.Data.Xml.Error" || name == "Viper.Data.Yaml.Error" ||
        name == "Viper.Data.Serialize.Error" || name == "Viper.Game2D.SceneDocument.LastError" ||
        name == "Viper.System.Pty.LastError" || name == "Viper.System.Exec.LastExitCode" ||
        name == "Viper.Game.UI.Table.LastHeaderClick" ||
        name == "Viper.Network.RestClient.LastStatus" ||
        name == "Viper.Network.RestClient.LastResponse" ||
        name == "Viper.Network.RestClient.LastOk" ||
        name == "Viper.Network.SmtpClient.get_LastError" ||
        name == "Viper.Graphics3D.AssetDiagnostics3D.get_LastLoadError" ||
        name == "Viper.Graphics3D.AssetDiagnostics3D.get_LastLoadErrorCode" ||
        name == "Viper.Zia.SemanticJob.Error")
        return true;
    if (leaf == "get_LastError")
        return true;
    return false;
}

/// @brief Infer fallibility classification from a public name and signature.
/// @details The value documents current behavior without changing it. Exact
///          future annotations can override these conservative heuristics.
/// @param name Public runtime name or class-qualified member name.
/// @param signature Compact runtime.def signature.
/// @return Fallibility class.
std::string inferRuntimeFallibility(std::string_view name, std::string_view signature) {
    const RuntimeApiSignatureParts sig = parseRuntimeApiSignature(signature);
    const std::string_view leaf = lastRuntimeNameSegment(name);
    if (isRuntimeSideChannelDiagnostic(name, leaf))
        return "side-channel";
    if (name == "Viper.Terminal.Ask" || name == "Viper.Terminal.ReadLine" ||
        name == "Viper.Terminal.InputLine")
        return "option";
    if (sig.valid) {
        if (endsWith(sig.returnType, "?"))
            return "option";
        if (startsWith(sig.returnType, "obj<Viper.Result"))
            return "result";
        if (startsWith(sig.returnType, "obj<Viper.Option"))
            return "option";
        if (startsWith(sig.returnType, "obj<Viper.Game.PathResult") ||
            startsWith(sig.returnType, "obj<Viper.Game.QueryResult") ||
            startsWith(sig.returnType, "obj<Viper.Game.QuadtreePairResult"))
            return "result";
    }
    if (startsWith(leaf, "Try") || startsWith(leaf, "Find")) {
        if (leaf == "FindAll")
            return "infallible";
        if (name == "Viper.Time.TimeZone.Find")
            return "traps";
        if (sig.valid && sig.returnType == "i1")
            return "infallible";
        return "sentinel";
    }
    if (leaf == "Parse" || startsWith(leaf, "Parse") || startsWith(leaf, "Load") ||
        startsWith(leaf, "Open") || startsWith(leaf, "Connect") || startsWith(leaf, "Decrypt"))
        return "traps";
    return "infallible";
}

/// @brief Infer return ownership for a runtime API row.
/// @details Advisory metadata for generated docs and tools. Exact ownership
///          rules remain governed by runtime docs until source annotations land.
/// @param name Public runtime name or class-qualified member name.
/// @param signature Compact runtime.def signature.
/// @return Ownership classification.
std::string inferRuntimeOwnership(std::string_view name, std::string_view signature) {
    const RuntimeApiSignatureParts sig = parseRuntimeApiSignature(signature);
    if (!sig.valid || sig.returnType == "void")
        return "none";
    if (sig.returnType == "i1" || sig.returnType == "i8" || sig.returnType == "i16" ||
        sig.returnType == "i32" || sig.returnType == "i64" || sig.returnType == "f32" ||
        sig.returnType == "f64" || sig.returnType == "bool")
        return "value";
    if (sig.returnType == "str" || sig.returnType == "string")
        return "owned";
    if (startsWith(sig.returnType, "obj<Viper.Option") ||
        startsWith(sig.returnType, "obj<Viper.Result") ||
        startsWith(sig.returnType, "obj<Viper.Threads.Future"))
        return "owned";
    const std::string_view leaf = lastRuntimeNameSegment(name);
    if (startsWith(leaf, "New") || startsWith(leaf, "From") || startsWith(leaf, "Load") ||
        startsWith(leaf, "Open") || startsWith(leaf, "Create") || leaf == "Clone" || leaf == "Copy")
        return "owned";
    return "unknown";
}

/// @brief Infer a unit for a numeric parameter.
/// @param name Public runtime name or class-qualified member name.
/// @param type Parameter type token.
/// @return Unit name, or empty when no unit is inferred.
std::string inferRuntimeParamUnit(std::string_view name, std::string_view type) {
    if (type != "i64" && type != "f64")
        return {};
    const std::string_view leaf = lastRuntimeNameSegment(name);
    if (leaf.find("Seconds") != std::string_view::npos ||
        leaf.find("Sec") != std::string_view::npos)
        return "seconds";
    if (leaf.find("Milliseconds") != std::string_view::npos ||
        leaf.find("Millis") != std::string_view::npos ||
        leaf.find("Ms") != std::string_view::npos ||
        leaf.find("Timeout") != std::string_view::npos ||
        leaf.find("Sleep") != std::string_view::npos ||
        leaf.find("Wait") != std::string_view::npos ||
        leaf.find("Delay") != std::string_view::npos ||
        leaf.find("Duration") != std::string_view::npos || endsWith(leaf, "For"))
        return "milliseconds";
    if (leaf == "Update" && type == "f64")
        return "seconds";
    return {};
}

/// @brief Infer a semantic domain for a primitive parameter.
/// @param name Public runtime name or class-qualified member name.
/// @param type Parameter type token.
/// @return Domain name, or empty when no domain is inferred.
std::string inferRuntimeParamDomain(std::string_view name, std::string_view type) {
    if (type != "i64")
        return {};
    if (name.find("Key") != std::string_view::npos)
        return "input-key";
    if (name.find("Mouse") != std::string_view::npos)
        return "mouse-button";
    if (name.find("Color") != std::string_view::npos || name.find("RGB") != std::string_view::npos)
        return "color";
    if (name.find("Layer") != std::string_view::npos || name.find("Mask") != std::string_view::npos)
        return "layer-mask";
    if (name.find("Status") != std::string_view::npos)
        return "status-code";
    if (name.find("Log") != std::string_view::npos)
        return "log-level";
    return {};
}

/// @brief Emit parsed return and parameter metadata for a signature.
/// @param os Stream receiving JSON.
/// @param name Runtime row name used for unit/domain hints.
/// @param signature Compact runtime.def signature.
void emitRuntimeSignatureMetadataJson(std::ostream &os,
                                      std::string_view name,
                                      std::string_view signature) {
    const RuntimeApiSignatureParts sig = parseRuntimeApiSignature(signature);
    os << ",\"return_type\":";
    if (sig.valid) {
        emitRuntimeApiTypeJson(os, sig.returnType);
    } else {
        os << "null";
    }
    os << ",\"params\":[";
    if (sig.valid) {
        for (size_t i = 0; i < sig.paramTypes.size(); ++i) {
            if (i)
                os << ',';
            os << "{\"index\":" << i << ",\"type\":";
            emitRuntimeApiTypeJson(os, sig.paramTypes[i]);
            if (const std::string unit = inferRuntimeParamUnit(name, sig.paramTypes[i]);
                !unit.empty()) {
                os << ",\"unit\":";
                il::support::printJsonStringEscaped(os, unit);
            }
            if (const std::string domain = inferRuntimeParamDomain(name, sig.paramTypes[i]);
                !domain.empty()) {
                os << ",\"domain\":";
                il::support::printJsonStringEscaped(os, domain);
            }
            os << '}';
        }
    }
    os << ']';
}

/// @brief Emit common contract metadata fields for one runtime row.
/// @param os Stream receiving JSON.
/// @param name Public runtime name or class-qualified member name.
/// @param signature Compact runtime.def signature.
void emitRuntimeContractMetadataJson(std::ostream &os,
                                     std::string_view name,
                                     std::string_view signature) {
    os << ",\"stability\":";
    il::support::printJsonStringEscaped(os, inferRuntimeStability(name));
    os << ",\"capabilities\":";
    emitStringArrayJson(os, inferRuntimeCapabilities(name));
    os << ",\"fallibility\":";
    il::support::printJsonStringEscaped(os, inferRuntimeFallibility(name, signature));
    os << ",\"ownership\":";
    il::support::printJsonStringEscaped(os, inferRuntimeOwnership(name, signature));
    if (const std::string migrationTarget = inferRuntimeMigrationTarget(name);
        !migrationTarget.empty()) {
        os << ",\"migration_target\":";
        il::support::printJsonStringEscaped(os, migrationTarget);
    }
    os << ",\"docs_anchor\":";
    emitDocsAnchorJson(os, name);
}

/// @brief Infer the broad public contract kind for a runtime class.
/// @details The inference uses existing layout, constructor, and member shape so
///          no runtime.def syntax changes are required for this additive slice.
/// @param klass Runtime class descriptor from the generated catalog.
/// @return Class kind string.
std::string inferRuntimeClassKind(const il::runtime::RuntimeClass &klass) {
    const std::string_view name = klass.qname ? std::string_view(klass.qname) : std::string_view();
    const std::string_view layout =
        klass.layout ? std::string_view(klass.layout) : std::string_view();
    const bool hasCtor = klass.ctor && std::string_view(klass.ctor).size() > 0;
    if (layout == "none" && klass.methods.empty() && !klass.properties.empty())
        return "enum-like";
    if (!hasCtor && !klass.methods.empty() && klass.properties.empty())
        return "static-module";
    if (name.find("Vec") != std::string_view::npos || name.find("Mat") != std::string_view::npos ||
        name.find("Quat") != std::string_view::npos || endsWith(name, ".Color") ||
        endsWith(name, ".Duration") || endsWith(name, ".DateTime") || endsWith(name, ".DateOnly"))
        return "value-object";
    if (layout == "none" && !hasCtor)
        return "static-module";
    if (hasCtor)
        return "instance-handle";
    return "namespace-facade";
}

/// @brief Build a descriptor lookup map keyed by canonical runtime function name.
/// @details Class methods reference canonical target names. This map lets the
///          API dump compare method and target arity to classify static methods.
/// @return Map from canonical name to runtime descriptor pointer.
std::unordered_map<std::string_view, const il::runtime::RuntimeDescriptor *>
buildRuntimeDescriptorByName() {
    std::unordered_map<std::string_view, const il::runtime::RuntimeDescriptor *> map;
    for (const auto &descriptor : il::runtime::runtimeRegistry())
        map.emplace(descriptor.name, &descriptor);
    return map;
}

/// @brief Infer whether a class method is receiverless/static.
/// @details Method signatures omit implicit receivers; descriptor signatures
///          include the C ABI shape. Equal arity means receiverless, while one
///          extra descriptor parameter means implicit receiver.
/// @param method Class method descriptor.
/// @param descriptors Runtime descriptor map keyed by canonical target name.
/// @return True when the method appears receiverless/static.
bool inferRuntimeMethodIsStatic(
    const il::runtime::RuntimeMethod &method,
    const std::unordered_map<std::string_view, const il::runtime::RuntimeDescriptor *>
        &descriptors) {
    const RuntimeApiSignatureParts methodSig =
        parseRuntimeApiSignature(method.signature ? method.signature : "");
    auto it =
        descriptors.find(method.target ? std::string_view(method.target) : std::string_view());
    if (!methodSig.valid || it == descriptors.end())
        return false;
    const RuntimeApiSignatureParts targetSig = parseRuntimeApiSignature(it->second->signatureText);
    if (!targetSig.valid)
        return false;
    return targetSig.paramTypes.size() == methodSig.paramTypes.size();
}

/// @brief Map an opcode TypeCategory to its stable JSON name.
const char *typeCategoryName(il::core::TypeCategory category) {
    using il::core::TypeCategory;
    switch (category) {
        case TypeCategory::None:
            return "none";
        case TypeCategory::Void:
            return "void";
        case TypeCategory::I1:
            return "i1";
        case TypeCategory::I16:
            return "i16";
        case TypeCategory::I32:
            return "i32";
        case TypeCategory::I64:
            return "i64";
        case TypeCategory::F64:
            return "f64";
        case TypeCategory::Ptr:
            return "ptr";
        case TypeCategory::Str:
            return "str";
        case TypeCategory::Error:
            return "error";
        case TypeCategory::ResumeTok:
            return "resume_tok";
        case TypeCategory::Any:
            return "any";
        case TypeCategory::InstrType:
            return "instr_type";
        case TypeCategory::Dynamic:
            return "dynamic";
    }
    return "none";
}

/// @brief Implement `--dump-opcodes`: print the IL opcode registry as a JSON
///        array on stdout, generated from the live opcode metadata table so it
///        cannot drift from Opcode.def.
/// @return 0 on success.
int dumpOpcodes() {
    using il::core::ResultArity;
    using il::support::printJsonStringEscaped;
    std::ostream &os = std::cout;

    os << "{\"ilVersion\":";
    printJsonStringEscaped(os, VIPER_IL_VERSION_STR);
    os << ",\"opcodes\":[";
    bool first = true;
    for (const auto op : il::core::all_opcodes()) {
        const auto &info = il::core::getOpcodeInfo(op);
        if (!first)
            os << ",";
        first = false;
        os << "{\"mnemonic\":";
        printJsonStringEscaped(os, info.name);
        os << ",\"resultArity\":\""
           << (info.resultArity == ResultArity::None
                   ? "none"
                   : (info.resultArity == ResultArity::One ? "one" : "optional"))
           << "\"";
        os << ",\"resultType\":\"" << typeCategoryName(info.resultType) << "\"";
        os << ",\"operandsMin\":" << static_cast<int>(info.numOperandsMin);
        if (info.numOperandsMax == il::core::kVariadicOperandCount)
            os << ",\"operandsMax\":-1";
        else
            os << ",\"operandsMax\":" << static_cast<int>(info.numOperandsMax);
        os << ",\"operandTypes\":[";
        for (size_t i = 0; i < info.operandTypes.size(); ++i) {
            if (i)
                os << ",";
            os << "\"" << typeCategoryName(info.operandTypes[i]) << "\"";
        }
        os << "]";
        os << ",\"sideEffects\":" << (info.hasSideEffects ? "true" : "false");
        os << ",\"successors\":" << static_cast<int>(info.numSuccessors);
        os << ",\"terminator\":" << (info.isTerminator ? "true" : "false");
        os << "}";
    }
    os << "]}\n";
    return 0;
}

/// @brief Implement `--dump-runtime-api`: print the full runtime surface
///        (global functions and classes with members) as one JSON document on
///        stdout. The document is generated from the live registry, so it can
///        never drift from the binary.
/// @return 0 on success.
int dumpRuntimeApi() {
    using il::support::printJsonStringEscaped;
    std::ostream &os = std::cout;
    const auto descriptorsByName = buildRuntimeDescriptorByName();

    os << "{\"version\":";
    printJsonStringEscaped(os, VIPER_VERSION_STR);
    os << ",\"schema_version\":2";
    os << ",\"signature_dialect\":\"runtime-def-v1\"";

    // Global runtime functions with canonical Viper.* names.
    os << ",\"functions\":[";
    {
        const auto &reg = il::runtime::runtimeRegistry();
        bool first = true;
        for (const auto &d : reg) {
            if (d.name.rfind("Viper.", 0) != 0)
                continue; // skip rt_* aliases; canonical names carry the API
            if (!d.publicSurface)
                continue;
            if (!first)
                os << ",";
            first = false;
            os << "{\"name\":";
            printJsonStringEscaped(os, d.name);
            os << ",\"signature\":";
            printJsonStringEscaped(os, d.signatureText);
            os << ",\"kind\":\"function\"";
            os << ",\"owner\":";
            printJsonStringEscaped(os, runtimeApiOwner(d.name));
            emitRuntimeSignatureMetadataJson(os, d.name, d.signatureText);
            emitRuntimeContractMetadataJson(os, d.name, d.signatureText);
            os << "}";
        }
    }
    os << "]";

    // Runtime classes with properties and methods.
    os << ",\"classes\":[";
    {
        const auto &classes = il::runtime::runtimeClassCatalog();
        bool firstClass = true;
        for (const auto &c : classes) {
            if (!firstClass)
                os << ",";
            firstClass = false;
            os << "{\"name\":";
            printJsonStringEscaped(os, c.qname ? c.qname : "");
            os << ",\"constructor\":";
            printJsonStringEscaped(os, c.ctor ? c.ctor : "");
            os << ",\"kind\":\"class\"";
            os << ",\"owner\":";
            printJsonStringEscaped(os, runtimeApiOwner(c.qname ? c.qname : ""));
            os << ",\"class_kind\":";
            printJsonStringEscaped(os, inferRuntimeClassKind(c));
            os << ",\"stability\":";
            printJsonStringEscaped(os, inferRuntimeStability(c.qname ? c.qname : ""));
            if (const std::string migrationTarget =
                    inferRuntimeMigrationTarget(c.qname ? c.qname : "");
                !migrationTarget.empty()) {
                os << ",\"migration_target\":";
                printJsonStringEscaped(os, migrationTarget);
            }
            os << ",\"capabilities\":";
            emitStringArrayJson(os, inferRuntimeCapabilities(c.qname ? c.qname : ""));
            os << ",\"docs_anchor\":";
            emitDocsAnchorJson(os, c.qname ? c.qname : "");
            os << ",\"properties\":[";
            bool firstProp = true;
            for (const auto &p : c.properties) {
                if (!firstProp)
                    os << ",";
                firstProp = false;
                os << "{\"name\":";
                printJsonStringEscaped(os, p.name ? p.name : "");
                os << ",\"type\":";
                printJsonStringEscaped(os, p.type ? p.type : "");
                os << ",\"readonly\":" << (p.readonly ? "true" : "false");
                os << ",\"kind\":\"property\"";
                os << ",\"owner\":";
                printJsonStringEscaped(os, c.qname ? c.qname : "");
                os << ",\"getter\":";
                printJsonStringEscaped(os, p.getter ? p.getter : "");
                os << ",\"setter\":";
                if (p.setter)
                    printJsonStringEscaped(os, p.setter);
                else
                    os << "null";
                os << ",\"return_type\":";
                emitRuntimeApiTypeJson(os, p.type ? p.type : "");
                os << ",\"stability\":";
                printJsonStringEscaped(os, inferRuntimeStability(p.getter ? p.getter : ""));
                os << ",\"capabilities\":";
                emitStringArrayJson(os, inferRuntimeCapabilities(c.qname ? c.qname : ""));
                os << ",\"fallibility\":\"infallible\"";
                os << ",\"ownership\":\"unknown\"";
                os << ",\"docs_anchor\":";
                const std::string propertyName =
                    std::string(c.qname ? c.qname : "") + "." + (p.name ? p.name : "");
                emitDocsAnchorJson(os, propertyName);
                os << "}";
            }
            os << "],\"methods\":[";
            bool firstMethod = true;
            for (const auto &m : c.methods) {
                if (!firstMethod)
                    os << ",";
                firstMethod = false;
                os << "{\"name\":";
                printJsonStringEscaped(os, m.name ? m.name : "");
                os << ",\"signature\":";
                printJsonStringEscaped(os, m.signature ? m.signature : "");
                os << ",\"kind\":\"method\"";
                os << ",\"owner\":";
                printJsonStringEscaped(os, c.qname ? c.qname : "");
                os << ",\"target\":";
                printJsonStringEscaped(os, m.target ? m.target : "");
                os << ",\"is_static\":"
                   << (inferRuntimeMethodIsStatic(m, descriptorsByName) ? "true" : "false");
                const std::string methodName =
                    std::string(c.qname ? c.qname : "") + "." + (m.name ? m.name : "");
                emitRuntimeSignatureMetadataJson(os, methodName, m.signature ? m.signature : "");
                emitRuntimeContractMetadataJson(
                    os, m.target ? m.target : methodName, m.signature ? m.signature : "");
                os << "}";
            }
            os << "]}";
        }
    }
    os << "]}\n";
    return 0;
}

/// @brief Implement `--dump-runtime-classes`: print the runtime class catalog
///        (properties, methods, constructors) in a stable format to stdout.
/// @return 0 on success.
int dumpRuntimeClasses() {
    const auto &classes = il::runtime::runtimeClassCatalog();
    for (const auto &c : classes) {
        std::cout << "CLASS " << (c.qname ? c.qname : "<unnamed>")
                  << " (type: " << (c.layout ? c.layout : "<unknown>") << ")\n";
        for (const auto &p : c.properties) {
            std::cout << "  PROP " << (p.name ? p.name : "<unnamed>") << ": "
                      << (p.type ? p.type : "<type>") << "  -> "
                      << (p.getter ? p.getter : "<getter>") << "\n";
        }
        for (const auto &m : c.methods) {
            std::cout << "  METH " << (m.name ? m.name : "<unnamed>") << "("
                      << (m.signature ? m.signature : "") << ") -> "
                      << (m.target ? m.target : "<target>") << "\n";
        }
        if (c.ctor && std::string(c.ctor).size())
            std::cout << "  CTOR -> " << c.ctor << "\n";
    }
    return 0;
}
} // namespace

/// @brief Print synopsis and option hints for the `viper` CLI.
///
/// @details The top-level help intentionally stays short. Subcommand-specific
///          help carries detailed flags so unrelated implementation options do
///          not crowd the common command list.
void printTopLevelUsage(std::ostream &out) {
    out << "viper v" << VIPER_VERSION_STR << "\n"
        << "Usage: viper <command> [arguments]\n"
        << "\n"
        << "Common commands:\n"
        << "       viper run [target] [options] [-- program-args...]\n"
        << "       viper build [target] [-o output] [options]\n"
        << "       viper check [target] [options]\n"
        << "       viper init <project-name> [--lang zia|basic]\n"
        << "       viper repl [zia|basic]\n"
        << "       viper eval [options] [code]\n"
        << "       viper explain <diagnostic-code>\n"
        << "       viper package [target] [--target macos|linux|windows|tarball] [-o output]\n"
        << "       viper asset bake|validate <model> [output.vscn] [options]\n"
        << "\n"
        << "Developer commands:\n"
        << "       viper front zia|basic ...\n"
        << "       viper -run <file.il> ...\n"
        << "       viper il-opt <file.il> -o <out.il> ...\n"
        << "       viper codegen x64|arm64 <file.il> ...\n"
        << "       viper bench <file.il> ...\n"
        << "       viper install-package ...\n"
        << "\n"
        << "Targets:\n"
        << "  A target is a .zia file, .bas file, directory, or viper.project path.\n"
        << "  If omitted where supported, the target defaults to the current directory.\n"
        << "\n"
        << "Help:\n"
        << "       viper help <command>\n"
        << "       viper help package\n"
        << "       viper help front zia|basic\n"
        << "       viper help codegen x64|arm64\n"
        << "       viper --version\n";
}

void usage() {
    printTopLevelUsage(std::cerr);
}

/// @brief Invoke a subcommand handler with a synthetic `--help` argument vector.
/// @details Lets `viper help <command>` reuse each subcommand's own help text.
int invokeHelp(int (*handler)(int, char **)) {
    char helpFlag[] = "--help";
    char *helpArgv[] = {helpFlag};
    return handler(1, helpArgv);
}

/// @brief Print usage for the `viper codegen` subcommand (architectures + options).
void codegenUsage(std::ostream &out = std::cerr) {
    out << "Usage: viper codegen <arch> <file.il> [options]\n"
        << "\n"
        << "Architectures:\n"
        << "  x64\n"
        << "  arm64\n"
        << "\n"
        << "Use 'viper help codegen x64' or 'viper help codegen arm64' for backend options.\n";
}

namespace viper::tools::ilc {

/// @brief Adapter invoked by `viper codegen x64` from the top-level driver.
/// @details The driver hands control to this helper with `argv` still pointing
///          at the architecture token. The helper strips it before delegating to
///          the actual command implementation so existing parsing logic
///          continues to expect the IL input path as the leading argument.
/// @param argc Number of command-line arguments following the "codegen"
///             subcommand.
/// @param argv Argument vector beginning with the target architecture token.
/// @return Exit status propagated from @ref cmd_codegen_x64.
int run_codegen_x64(int argc, char **argv) {
    return cmd_codegen_x64(argc - 1, argv + 1);
}

} // namespace viper::tools::ilc

/// @brief Program entry for the `viper` command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status of the selected subcommand or `1` on error.
/// @details The first argument determines which handler processes the request:
///          `cmdRunIL` executes `.il` programs, `cmdILOpt` performs
///          optimization passes, and `cmdFrontZia` / `cmdFrontBasic` drive the
///          legacy frontend entry points. Step-by-step summary:
///          1. Verify that at least one subcommand argument is provided.
///          2. Handle `--version` by delegating to @ref printVersion.
///          3. Dispatch to the matching handler with the remaining arguments.
///          4. Fall back to displaying usage when no match exists.
int main(int argc, char **argv) {
#if VIPER_HOST_WINDOWS
    // Disable Windows abort dialog so runtime panics exit cleanly
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    if (argc < 2) {
        usage();
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h") {
        printTopLevelUsage(std::cout);
        return 0;
    }
    if (cmd == "--version") {
        printVersion();
        return 0;
    }
    if (cmd == "--dump-runtime-descriptors") {
        return dumpRuntimeDescriptors();
    }
    if (cmd == "--dump-runtime-classes") {
        return dumpRuntimeClasses();
    }
    if (cmd == "--dump-runtime-api") {
        return dumpRuntimeApi();
    }
    if (cmd == "--dump-opcodes") {
        return dumpOpcodes();
    }
    if (cmd == "--print-error-codes") {
        bool json = false;
        if (argc == 3 && std::string_view(argv[2]) == "--json") {
            json = true;
        } else if (argc > 2) {
            std::cerr << "error: --print-error-codes accepts only optional --json\n";
            return 1;
        }
        return printErrorCodes(json);
    }
    if (cmd == "run") {
        return cmdRun(argc - 2, argv + 2);
    }
    if (cmd == "build") {
        return cmdBuild(argc - 2, argv + 2);
    }
    if (cmd == "check") {
        return cmdCheck(argc - 2, argv + 2);
    }
    if (cmd == "init") {
        return cmdInit(argc - 2, argv + 2);
    }
    if (cmd == "asset") {
        return cmdAsset(argc - 2, argv + 2);
    }
    if (cmd == "package") {
        return cmdPackage(argc - 2, argv + 2);
    }
    if (cmd == "install-package") {
        return cmdInstallPackage(argc - 2, argv + 2);
    }
    if (cmd == "repl") {
        return cmdRepl(argc - 2, argv + 2);
    }
    if (cmd == "eval") {
        return cmdEval(argc - 2, argv + 2);
    }
    if (cmd == "explain") {
        return cmdExplain(argc - 2, argv + 2);
    }
    if (cmd == "help") {
        if (argc < 3) {
            printTopLevelUsage(std::cout);
            return 1;
        }
        const std::string_view topic = argv[2];
        if (topic == "run")
            return invokeHelp(cmdRun);
        if (topic == "build")
            return invokeHelp(cmdBuild);
        if (topic == "check")
            return invokeHelp(cmdCheck);
        if (topic == "init")
            return invokeHelp(cmdInit);
        if (topic == "package")
            return invokeHelp(cmdPackage);
        if (topic == "install-package")
            return invokeHelp(cmdInstallPackage);
        if (topic == "repl")
            return invokeHelp(cmdRepl);
        if (topic == "asset")
            return cmdAssetHelp(stdout);
        if (topic == "eval")
            return invokeHelp(cmdEval);
        if (topic == "explain")
            return invokeHelp(cmdExplain);
        if (topic == "-run")
            return invokeHelp(cmdRunIL);
        if (topic == "il-opt")
            return invokeHelp(cmdILOpt);
        if (topic == "bench")
            return invokeHelp(cmdBench);
        if (topic == "front") {
            if (argc >= 4 && std::string_view(argv[3]) == "zia")
                return invokeHelp(cmdFrontZia);
            if (argc >= 4 && std::string_view(argv[3]) == "basic")
                return invokeHelp(cmdFrontBasic);
            std::cerr << "Usage: viper front zia|basic ...\n"
                      << "Use 'viper help front zia' or 'viper help front basic'.\n";
            return 1;
        }
        if (topic == "codegen") {
            if (argc >= 4 && std::string_view(argv[3]) == "x64")
                return invokeHelp(viper::tools::ilc::cmd_codegen_x64);
            if (argc >= 4 && std::string_view(argv[3]) == "arm64")
                return invokeHelp(viper::tools::ilc::cmd_codegen_arm64);
            codegenUsage(std::cout);
            return 0;
        }
        std::cerr << "unknown help topic: " << topic << "\n";
        usage();
        return 1;
    }
    if (cmd == "-run") {
        return cmdRunIL(argc - 2, argv + 2);
    }
    if (cmd == "il-opt") {
        return cmdILOpt(argc - 2, argv + 2);
    }
    if (cmd == "bench") {
        return cmdBench(argc - 2, argv + 2);
    }
    if (cmd == "codegen") {
        if (argc >= 3 &&
            (std::string_view(argv[2]) == "--help" || std::string_view(argv[2]) == "-h")) {
            codegenUsage(std::cout);
            return 0;
        }
        if (argc >= 3) {
            if (std::string_view(argv[2]) == "x64") {
                return viper::tools::ilc::run_codegen_x64(argc - 2, argv + 2);
            }
            if (std::string_view(argv[2]) == "arm64") {
                return viper::tools::ilc::cmd_codegen_arm64(argc - 3, argv + 3);
            }
        }
        codegenUsage();
        return 1;
    }
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "basic") {
        return cmdFrontBasic(argc - 3, argv + 3);
    }
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "zia") {
        return cmdFrontZia(argc - 3, argv + 3);
    }
    usage();
    return 1;
}
