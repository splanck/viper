//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_tiled_import.cpp
// Purpose: Dependency-aware import of finite orthogonal Tiled JSON/TMX maps
//   into SceneDocument and render-ready Tilemap runtime objects.
//
// Key invariants:
//   - Filesystem and asset-package dependency graphs use separate readers.
//   - Unsupported or ambiguous Tiled features fail before an object is published.
//   - Layer GIDs are normalized to the one tileset bound to that layer.
//   - Input, decoded layer, dependency, and output sizes are explicitly bounded.
//
// Ownership/Lifetime:
//   - Runtime parse trees are released before an import returns.
//   - Result objects retain their SceneDocument/Tilemap payloads.
//   - Temporary Pixels and SceneDocument handles are released after transfer.
//
// Links: rt_scene_editor.h, ../graphics/2d/rt_graphics2d.h,
//   docs/adr/0140-tiled-map-and-scene-import.md
//
//===----------------------------------------------------------------------===//

#include "rt_scene_editor.h"

#include "rt_asset.h"
#include "rt_box.h"
#include "rt_compress.h"
#include "rt_crc32.h"
#include "rt_graphics2d.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_result.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_tilemap.h"
#include "rt_xml.h"
#include "rt_zstd.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int64_t kTiledMapLoaderClassId = INT64_C(-0x620121);
constexpr size_t kMaxDocumentBytes = 16u * 1024u * 1024u;
constexpr size_t kMaxDependencyCount = 4096u;
constexpr size_t kMaxCellsPerLayer = 1024u * 1024u;
constexpr size_t kMaxTotalCells = 4u * 1024u * 1024u;
constexpr size_t kMaxLayers = 16u;
constexpr size_t kMaxObjects = 65536u;
constexpr size_t kMaxProperties = 16384u;
constexpr size_t kMaxStringBytes = 64u * 1024u;
constexpr uint64_t kTiledTransformMask = UINT64_C(0xF0000000);

void releaseObject(void *object) {
    if (object && rt_obj_release_check0(object))
        rt_obj_free(object);
}

void releaseParsedValue(void *value) {
    if (!value)
        return;
    if (rt_string_is_handle(value)) {
        rt_string_unref(static_cast<rt_string>(value));
        return;
    }
    releaseObject(value);
}

class OwnedObject {
  public:
    explicit OwnedObject(void *value = nullptr) : value_(value) {}

    ~OwnedObject() {
        releaseParsedValue(value_);
    }

    OwnedObject(const OwnedObject &) = delete;
    OwnedObject &operator=(const OwnedObject &) = delete;

    void *get() const {
        return value_;
    }

    void *release() {
        void *value = value_;
        value_ = nullptr;
        return value;
    }

  private:
    void *value_;
};

std::string toStd(rt_string value) {
    if (!value)
        return {};
    const char *data = rt_string_cstr(value);
    int64_t length = rt_str_len(value);
    if (!data || length <= 0)
        return {};
    return std::string(data, static_cast<size_t>(length));
}

rt_string makeString(const std::string &value) {
    return rt_string_from_bytes(value.data(), value.size());
}

bool isMap(void *value) {
    return value && rt_obj_class_id(value) == RT_MAP_CLASS_ID;
}

bool isSeq(void *value) {
    return value && rt_obj_class_id(value) == RT_SEQ_CLASS_ID;
}

void *mapGet(void *map, const char *key) {
    if (!isMap(map))
        return nullptr;
    rt_string runtimeKey = rt_const_cstr(key);
    void *value = rt_map_get(map, runtimeKey);
    rt_string_unref(runtimeKey);
    return value;
}

bool mapHas(void *map, const char *key) {
    if (!isMap(map))
        return false;
    rt_string runtimeKey = rt_const_cstr(key);
    bool present = rt_map_has(map, runtimeKey) != 0;
    rt_string_unref(runtimeKey);
    return present;
}

bool jsonStringValue(void *value, std::string &out) {
    if (!value || !rt_string_is_handle(value))
        return false;
    out = toStd(static_cast<rt_string>(value));
    return true;
}

bool jsonString(void *map, const char *key, std::string &out) {
    return jsonStringValue(mapGet(map, key), out);
}

bool jsonNumberValue(void *value, double &out) {
    if (!value || rt_obj_class_id(value) != RT_BOX_CLASS_ID)
        return false;
    if (rt_box_type(value) == RT_BOX_F64) {
        out = rt_unbox_f64(value);
        return std::isfinite(out);
    }
    if (rt_box_type(value) == RT_BOX_I64) {
        out = static_cast<double>(rt_unbox_i64(value));
        return true;
    }
    return false;
}

bool jsonIntValue(void *value, int64_t &out) {
    double number = 0.0;
    if (!jsonNumberValue(value, number) || std::trunc(number) != number ||
        number < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        number > static_cast<double>(std::numeric_limits<int64_t>::max()))
        return false;
    out = static_cast<int64_t>(number);
    return true;
}

bool jsonInt(void *map, const char *key, int64_t &out) {
    return jsonIntValue(mapGet(map, key), out);
}

bool jsonBoolValue(void *value, bool &out) {
    if (!value || rt_obj_class_id(value) != RT_BOX_CLASS_ID || rt_box_type(value) != RT_BOX_I1)
        return false;
    out = rt_unbox_i1(value) != 0;
    return true;
}

bool jsonBool(void *map, const char *key, bool &out) {
    return jsonBoolValue(mapGet(map, key), out);
}

std::string jsonEscape(const std::string &value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(ch) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }
    out << '"';
    return out.str();
}

std::string jsonDouble(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

enum class ScalarKind { Null, Boolean, Integer, Float, String };

struct Scalar {
    ScalarKind kind{ScalarKind::Null};
    bool boolean{false};
    int64_t integer{0};
    double floating{0.0};
    std::string string;
};

bool scalarEqual(const Scalar &left, const Scalar &right) {
    return left.kind == right.kind && left.boolean == right.boolean &&
           left.integer == right.integer && left.floating == right.floating &&
           left.string == right.string;
}

std::string scalarJson(const Scalar &value) {
    switch (value.kind) {
        case ScalarKind::Null:
            return "null";
        case ScalarKind::Boolean:
            return value.boolean ? "true" : "false";
        case ScalarKind::Integer:
            return std::to_string(value.integer);
        case ScalarKind::Float:
            return jsonDouble(value.floating);
        case ScalarKind::String:
            return jsonEscape(value.string);
    }
    return "null";
}

bool scalarFromJson(void *value, const std::string &declaredType, Scalar &out) {
    if (!value) {
        out = Scalar{};
        return true;
    }
    bool boolean = false;
    if (jsonBoolValue(value, boolean)) {
        out.kind = ScalarKind::Boolean;
        out.boolean = boolean;
        return true;
    }
    std::string string;
    if (jsonStringValue(value, string)) {
        out.kind = ScalarKind::String;
        out.string = std::move(string);
        return out.string.size() <= kMaxStringBytes;
    }
    double number = 0.0;
    if (!jsonNumberValue(value, number))
        return false;
    if (declaredType == "int" || declaredType == "object") {
        if (std::trunc(number) != number || number < static_cast<double>(INT64_MIN) ||
            number > static_cast<double>(INT64_MAX))
            return false;
        out.kind = ScalarKind::Integer;
        out.integer = static_cast<int64_t>(number);
    } else if (std::trunc(number) == number && number >= static_cast<double>(INT64_MIN) &&
               number <= static_cast<double>(INT64_MAX)) {
        out.kind = ScalarKind::Integer;
        out.integer = static_cast<int64_t>(number);
    } else {
        out.kind = ScalarKind::Float;
        out.floating = number;
    }
    return true;
}

bool parseIntegerText(const std::string &text, int64_t &out) {
    if (text.empty())
        return false;
    char *end = nullptr;
    errno = 0;
    long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return false;
    out = static_cast<int64_t>(value);
    return true;
}

bool parseDoubleText(const std::string &text, double &out) {
    if (text.empty())
        return false;
    char *end = nullptr;
    errno = 0;
    double value = std::strtod(text.c_str(), &end);
    if (errno != 0 || !end || *end != '\0' || !std::isfinite(value))
        return false;
    out = value;
    return true;
}

std::string lowerAscii(std::string value) {
    for (char &ch : value) {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return value;
}

std::string normalizeTiledXmlInput(const std::string &text) {
    size_t position = 0;
    if (text.size() >= 3u && static_cast<unsigned char>(text[0]) == 0xEFu &&
        static_cast<unsigned char>(text[1]) == 0xBBu &&
        static_cast<unsigned char>(text[2]) == 0xBFu)
        position = 3u;
    while (position < text.size() && (text[position] == ' ' || text[position] == '\t' ||
                                      text[position] == '\r' || text[position] == '\n'))
        ++position;
    if (text.compare(position, 5u, "<?xml") == 0) {
        size_t end = text.find("?>", position + 5u);
        if (end != std::string::npos)
            position = end + 2u;
    }
    return text.substr(position);
}

struct TileAnimation {
    int64_t baseTile{0};
    int64_t milliseconds{0};
    std::vector<int64_t> frames;
};

struct TileMetadata {
    bool solid{false};
    std::map<std::string, Scalar> properties;
    std::vector<int64_t> animationFrames;
    std::vector<int64_t> animationDurations;
};

struct Tileset {
    int64_t firstGid{1};
    std::string name;
    int64_t tileWidth{0};
    int64_t tileHeight{0};
    int64_t tileCount{0};
    int64_t columns{0};
    int64_t margin{0};
    int64_t spacing{0};
    int64_t imageWidth{0};
    int64_t imageHeight{0};
    std::string image;
    std::map<int64_t, TileMetadata> metadata;
};

struct Layer {
    std::string name;
    bool visible{true};
    std::vector<int64_t> tiles;
    int64_t tileset{-1};
};

struct SceneObject {
    std::string type;
    std::string id;
    int64_t x{0};
    int64_t y{0};
    std::map<std::string, Scalar> properties;
};

struct MapDocument {
    std::string name;
    int64_t width{0};
    int64_t height{0};
    int64_t tileWidth{0};
    int64_t tileHeight{0};
    std::map<std::string, Scalar> properties;
    std::vector<Tileset> tilesets;
    std::vector<Layer> layers;
    std::vector<SceneObject> objects;
    std::set<int64_t> solidTiles;
    std::map<int64_t, std::map<std::string, Scalar>> tileProperties;
    std::map<int64_t, TileAnimation> animations;
    size_t totalCells{0};
};

struct ImportProduct {
    void *scene{nullptr};
    MapDocument document;
    std::string error;
};

class SourceReader {
  public:
    explicit SourceReader(bool assetMode) : assetMode_(assetMode) {}

    bool readRoot(const std::string &path,
                  std::string &resolved,
                  std::string &text,
                  std::string &error) {
        if (path.empty()) {
            error = "Tiled import path is empty";
            return false;
        }
        if (assetMode_) {
            resolved = normalizeAssetRoot(path, error);
            if (resolved.empty())
                return false;
        } else {
            std::error_code ec;
            resolved = std::filesystem::absolute(std::filesystem::path(path), ec)
                           .lexically_normal()
                           .string();
            if (ec) {
                error = "cannot resolve Tiled map path: " + path;
                return false;
            }
        }
        return readResolved(resolved, text, error);
    }

    bool readDependency(const std::string &owner,
                        const std::string &reference,
                        std::string &resolved,
                        std::string &text,
                        std::string &error) {
        if (++dependencyCount_ > kMaxDependencyCount) {
            error = "Tiled dependency count exceeds 4096";
            return false;
        }
        if (reference.empty()) {
            error = "Tiled dependency path is empty";
            return false;
        }
        if (assetMode_) {
            resolved = resolveAsset(owner, reference, error);
            if (resolved.empty())
                return false;
        } else {
            std::filesystem::path ref(reference);
            if (ref.is_absolute() || reference.find(':') != std::string::npos) {
                error = "absolute or URI Tiled dependency paths are not portable: " + reference;
                return false;
            }
            resolved =
                (std::filesystem::path(owner).parent_path() / ref).lexically_normal().string();
        }
        return readResolved(resolved, text, error);
    }

    bool resolveDependencyPath(const std::string &owner,
                               const std::string &reference,
                               std::string &resolved,
                               std::string &error) const {
        if (reference.empty()) {
            error = "Tiled image dependency path is empty";
            return false;
        }
        if (assetMode_) {
            resolved = resolveAsset(owner, reference, error);
            return !resolved.empty();
        }
        std::filesystem::path ref(reference);
        if (ref.is_absolute() || reference.find(':') != std::string::npos) {
            error = "absolute or URI Tiled dependency paths are not portable: " + reference;
            return false;
        }
        resolved = (std::filesystem::path(owner).parent_path() / ref).lexically_normal().string();
        return true;
    }

    bool assetMode() const {
        return assetMode_;
    }

  private:
    static std::string normalizeAssetRoot(std::string path, std::string &error) {
        if (path.rfind("asset://", 0) == 0)
            path.erase(0, 8);
        std::replace(path.begin(), path.end(), '\\', '/');
        if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos) {
            error = "unsafe asset path: " + path;
            return {};
        }
        std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
        std::string result = normalized.generic_string();
        if (result.empty() || result == "." || result == ".." || result.rfind("../", 0) == 0) {
            error = "asset path escapes the logical asset root: " + path;
            return {};
        }
        return result;
    }

    static std::string resolveAsset(const std::string &owner,
                                    std::string reference,
                                    std::string &error) {
        if (reference.rfind("asset://", 0) == 0)
            reference.erase(0, 8);
        std::replace(reference.begin(), reference.end(), '\\', '/');
        if (reference.empty() || reference.front() == '/' ||
            reference.find(':') != std::string::npos) {
            error = "unsafe asset dependency path: " + reference;
            return {};
        }
        std::filesystem::path joined =
            std::filesystem::path(owner).parent_path() / std::filesystem::path(reference);
        std::string result = joined.lexically_normal().generic_string();
        if (result.empty() || result == "." || result == ".." || result.rfind("../", 0) == 0) {
            error = "asset dependency escapes the logical asset root: " + reference;
            return {};
        }
        return result;
    }

    bool readResolved(const std::string &resolved, std::string &text, std::string &error) const {
        if (assetMode_) {
            rt_string path = makeString(resolved);
            size_t size = 0;
            uint8_t *bytes = rt_asset_load_raw(path, &size);
            rt_string_unref(path);
            if (!bytes) {
                error = "cannot load Tiled asset: " + resolved;
                return false;
            }
            if (size > kMaxDocumentBytes) {
                std::free(bytes);
                error = "Tiled document exceeds 16 MiB: " + resolved;
                return false;
            }
            text.assign(reinterpret_cast<const char *>(bytes), size);
            std::free(bytes);
            return true;
        }

        std::ifstream input(resolved, std::ios::binary | std::ios::ate);
        if (!input) {
            error = "cannot open Tiled document: " + resolved;
            return false;
        }
        std::streamoff size = input.tellg();
        if (size < 0 || static_cast<uint64_t>(size) > kMaxDocumentBytes) {
            error = "Tiled document exceeds 16 MiB: " + resolved;
            return false;
        }
        input.seekg(0, std::ios::beg);
        text.resize(static_cast<size_t>(size));
        if (size > 0)
            input.read(text.data(), size);
        if (!input && size > 0) {
            error = "cannot read complete Tiled document: " + resolved;
            return false;
        }
        return true;
    }

    bool assetMode_{false};
    size_t dependencyCount_{0};
};

bool setProperty(std::map<std::string, Scalar> &properties,
                 const std::string &name,
                 Scalar value,
                 std::string &error) {
    if (name.empty() || name.size() > 128u) {
        error = "Tiled property name is empty or exceeds 128 bytes";
        return false;
    }
    if (properties.size() >= kMaxProperties && properties.find(name) == properties.end()) {
        error = "Tiled property count exceeds 16384";
        return false;
    }
    properties[name] = std::move(value);
    return true;
}

bool parseJsonProperties(void *value,
                         std::map<std::string, Scalar> &properties,
                         std::string &error,
                         SourceReader *reader = nullptr,
                         const std::string &owner = {}) {
    if (!value)
        return true;
    if (!isSeq(value)) {
        error = "Tiled properties must be an array";
        return false;
    }
    int64_t count = rt_seq_len(value);
    if (count < 0 || static_cast<uint64_t>(count) > kMaxProperties) {
        error = "Tiled property count exceeds 16384";
        return false;
    }
    for (int64_t index = 0; index < count; ++index) {
        void *entry = rt_seq_get(value, index);
        std::string name;
        std::string type;
        if (!isMap(entry) || !jsonString(entry, "name", name)) {
            error = "Tiled property is missing a string name";
            return false;
        }
        jsonString(entry, "type", type);
        if (type == "class") {
            error = "Tiled class properties are not representable as SceneDocument scalars";
            return false;
        }
        Scalar scalar;
        if (!scalarFromJson(mapGet(entry, "value"), type, scalar)) {
            error = "invalid Tiled property value for '" + name + "'";
            return false;
        }
        if (type == "file" && scalar.kind == ScalarKind::String && !scalar.string.empty() &&
            reader) {
            std::string resolved;
            if (!reader->resolveDependencyPath(owner, scalar.string, resolved, error))
                return false;
            scalar.string = std::move(resolved);
        }
        if (!setProperty(properties, name, std::move(scalar), error))
            return false;
    }
    return true;
}

std::string xmlString(void *node, const char *attribute, bool *present = nullptr) {
    rt_string key = rt_const_cstr(attribute);
    bool has = rt_xml_has_attr(node, key) != 0;
    if (present)
        *present = has;
    if (!has) {
        rt_string_unref(key);
        return {};
    }
    rt_string value = rt_xml_attr(node, key);
    rt_string_unref(key);
    std::string result = toStd(value);
    rt_str_release_maybe(value);
    return result;
}

std::string xmlTag(void *node) {
    rt_string value = rt_xml_tag(node);
    std::string result = toStd(value);
    rt_str_release_maybe(value);
    return result;
}

std::string xmlText(void *node) {
    rt_string value = rt_xml_text_content(node);
    std::string result = toStd(value);
    rt_str_release_maybe(value);
    return result;
}

bool xmlInt(void *node, const char *attribute, int64_t &out, bool required, std::string &error) {
    bool present = false;
    std::string text = xmlString(node, attribute, &present);
    if (!present) {
        if (required)
            error = std::string("TMX element is missing integer attribute '") + attribute + "'";
        return !required;
    }
    if (!parseIntegerText(text, out)) {
        error = std::string("invalid TMX integer attribute '") + attribute + "'";
        return false;
    }
    return true;
}

bool xmlDouble(void *node, const char *attribute, double &out, bool required, std::string &error) {
    bool present = false;
    std::string text = xmlString(node, attribute, &present);
    if (!present) {
        if (required)
            error = std::string("TMX element is missing numeric attribute '") + attribute + "'";
        return !required;
    }
    if (!parseDoubleText(text, out)) {
        error = std::string("invalid TMX numeric attribute '") + attribute + "'";
        return false;
    }
    return true;
}

void *firstXmlChild(void *node, const char *tag) {
    int64_t count = rt_xml_child_count(node);
    for (int64_t index = 0; index < count; ++index) {
        void *child = rt_xml_child_at(node, index);
        if (rt_xml_node_type(child) == XML_NODE_ELEMENT && xmlTag(child) == tag)
            return child;
    }
    return nullptr;
}

bool parseXmlProperties(void *parent,
                        std::map<std::string, Scalar> &properties,
                        std::string &error,
                        SourceReader *reader = nullptr,
                        const std::string &owner = {}) {
    void *container = firstXmlChild(parent, "properties");
    if (!container)
        return true;
    int64_t count = rt_xml_child_count(container);
    for (int64_t index = 0; index < count; ++index) {
        void *property = rt_xml_child_at(container, index);
        if (rt_xml_node_type(property) != XML_NODE_ELEMENT || xmlTag(property) != "property")
            continue;
        std::string name = xmlString(property, "name");
        std::string type = xmlString(property, "type");
        if (type == "class") {
            error = "TMX class properties are not representable as SceneDocument scalars";
            return false;
        }
        bool hasValue = false;
        std::string text = xmlString(property, "value", &hasValue);
        if (!hasValue)
            text = xmlText(property);
        Scalar scalar;
        if (type == "bool") {
            std::string normalized = lowerAscii(text);
            if (normalized != "true" && normalized != "false" && normalized != "1" &&
                normalized != "0") {
                error = "invalid TMX bool property '" + name + "'";
                return false;
            }
            scalar.kind = ScalarKind::Boolean;
            scalar.boolean = normalized == "true" || normalized == "1";
        } else if (type == "int" || type == "object") {
            if (!parseIntegerText(text, scalar.integer)) {
                error = "invalid TMX integer property '" + name + "'";
                return false;
            }
            scalar.kind = ScalarKind::Integer;
        } else if (type == "float") {
            if (!parseDoubleText(text, scalar.floating)) {
                error = "invalid TMX float property '" + name + "'";
                return false;
            }
            scalar.kind = ScalarKind::Float;
        } else if (type.empty() || type == "string" || type == "file" || type == "color") {
            if (text.size() > kMaxStringBytes) {
                error = "TMX string property exceeds 64 KiB";
                return false;
            }
            scalar.kind = ScalarKind::String;
            scalar.string = std::move(text);
        } else {
            error = "unsupported TMX property type '" + type + "'";
            return false;
        }
        if (type == "file" && !scalar.string.empty() && reader) {
            std::string resolved;
            if (!reader->resolveDependencyPath(owner, scalar.string, resolved, error))
                return false;
            scalar.string = std::move(resolved);
        }
        if (!setProperty(properties, name, std::move(scalar), error))
            return false;
    }
    return true;
}

bool decodeBase64(const std::string &text, std::vector<uint8_t> &output, std::string &error) {
    static constexpr signed char decode[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -1, -1, -2, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62,
        -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -3, -1, -1, -1, 0,
        1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
        23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
        39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    std::vector<unsigned char> compact;
    compact.reserve(text.size());
    for (unsigned char ch : text) {
        if (decode[ch] == -2)
            continue;
        if (decode[ch] == -1) {
            error = "invalid character in Tiled Base64 layer data";
            return false;
        }
        compact.push_back(ch);
    }
    if (compact.empty() || compact.size() % 4u != 0u) {
        error = "invalid Tiled Base64 layer length";
        return false;
    }
    output.clear();
    output.reserve((compact.size() / 4u) * 3u);
    for (size_t index = 0; index < compact.size(); index += 4u) {
        bool finalGroup = index + 4u == compact.size();
        int a = decode[compact[index]];
        int b = decode[compact[index + 1u]];
        int c = decode[compact[index + 2u]];
        int d = decode[compact[index + 3u]];
        if (a < 0 || b < 0 || (!finalGroup && (c < 0 || d < 0)) || (c == -3 && d != -3) ||
            (c == -3 && (b & 0x0F) != 0) || (d == -3 && c >= 0 && (c & 0x03) != 0)) {
            error = "invalid padding in Tiled Base64 layer data";
            return false;
        }
        uint32_t bits = static_cast<uint32_t>(a << 18) | static_cast<uint32_t>(b << 12);
        if (c >= 0)
            bits |= static_cast<uint32_t>(c << 6);
        if (d >= 0)
            bits |= static_cast<uint32_t>(d);
        output.push_back(static_cast<uint8_t>(bits >> 16));
        if (c >= 0)
            output.push_back(static_cast<uint8_t>(bits >> 8));
        if (d >= 0)
            output.push_back(static_cast<uint8_t>(bits));
    }
    return true;
}

uint32_t readLe32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) | (static_cast<uint32_t>(data[3]) << 24u);
}

bool gunzipExact(const std::vector<uint8_t> &input,
                 size_t expectedSize,
                 std::vector<uint8_t> &output,
                 std::string &error) {
    if (input.size() < 18u || input[0] != 0x1Fu || input[1] != 0x8Bu || input[2] != 8u ||
        (input[3] & 0xE0u) != 0u) {
        error = "invalid gzip-compressed Tiled layer data";
        return false;
    }
    uint8_t flags = input[3];
    size_t position = 10u;
    if ((flags & 0x04u) != 0u) {
        if (position + 2u > input.size()) {
            error = "truncated gzip header in Tiled layer data";
            return false;
        }
        size_t extra = static_cast<size_t>(input[position]) |
                       (static_cast<size_t>(input[position + 1u]) << 8u);
        position += 2u;
        if (extra > input.size() - position) {
            error = "truncated gzip extra field in Tiled layer data";
            return false;
        }
        position += extra;
    }
    auto skipTerminated = [&]() {
        while (position < input.size() && input[position] != 0u)
            ++position;
        if (position >= input.size())
            return false;
        ++position;
        return true;
    };
    if (((flags & 0x08u) != 0u && !skipTerminated()) ||
        ((flags & 0x10u) != 0u && !skipTerminated())) {
        error = "truncated gzip text field in Tiled layer data";
        return false;
    }
    if ((flags & 0x02u) != 0u) {
        if (position + 2u > input.size()) {
            error = "truncated gzip header checksum in Tiled layer data";
            return false;
        }
        uint16_t expected = static_cast<uint16_t>(input[position]) |
                            static_cast<uint16_t>(input[position + 1u] << 8u);
        uint16_t actual = static_cast<uint16_t>(rt_crc32_compute(input.data(), position));
        if (expected != actual) {
            error = "gzip header checksum mismatch in Tiled layer data";
            return false;
        }
        position += 2u;
    }
    if (position + 8u >= input.size()) {
        error = "truncated gzip payload in Tiled layer data";
        return false;
    }
    size_t payloadSize = input.size() - position - 8u;
    uint8_t *decoded = nullptr;
    size_t decodedSize = 0;
    if (!rt_compress_inflate_raw(
            input.data() + position, payloadSize, expectedSize, &decoded, &decodedSize) ||
        decodedSize != expectedSize) {
        std::free(decoded);
        error = "cannot inflate gzip-compressed Tiled layer data";
        return false;
    }
    uint32_t expectedCrc = readLe32(input.data() + input.size() - 8u);
    uint32_t expectedLength = readLe32(input.data() + input.size() - 4u);
    if (expectedCrc != rt_crc32_compute(decoded, decodedSize) ||
        expectedLength != static_cast<uint32_t>(decodedSize)) {
        std::free(decoded);
        error = "gzip trailer mismatch in Tiled layer data";
        return false;
    }
    output.assign(decoded, decoded + decodedSize);
    std::free(decoded);
    return true;
}

bool decompressLayer(const std::vector<uint8_t> &encoded,
                     const std::string &compression,
                     size_t expectedSize,
                     std::vector<uint8_t> &decoded,
                     std::string &error) {
    if (compression.empty()) {
        if (encoded.size() != expectedSize) {
            error = "uncompressed Tiled Base64 layer has the wrong byte length";
            return false;
        }
        decoded = encoded;
        return true;
    }
    if (compression == "zlib") {
        decoded.resize(expectedSize);
        if (!rt_compress_inflate_zlib_into(
                encoded.data(), encoded.size(), decoded.data(), expectedSize)) {
            error = "cannot inflate zlib-compressed Tiled layer data";
            return false;
        }
        return true;
    }
    if (compression == "gzip")
        return gunzipExact(encoded, expectedSize, decoded, error);
    if (compression == "zstd") {
        uint8_t *data = nullptr;
        size_t size = 0;
        if (!rt_zstd_decompress_raw(encoded.data(), encoded.size(), expectedSize, &data, &size) ||
            size != expectedSize) {
            std::free(data);
            error = "cannot decompress zstd-compressed Tiled layer data";
            return false;
        }
        decoded.assign(data, data + size);
        std::free(data);
        return true;
    }
    error = "unsupported Tiled layer compression '" + compression + "'";
    return false;
}

bool parseCsv(const std::string &text,
              size_t expectedCells,
              std::vector<int64_t> &gids,
              std::string &error) {
    gids.clear();
    size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() &&
               (text[position] == ' ' || text[position] == '\t' || text[position] == '\r' ||
                text[position] == '\n' || text[position] == ','))
            ++position;
        if (position == text.size())
            break;
        size_t start = position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9')
            ++position;
        if (start == position) {
            error = "invalid token in Tiled CSV layer data";
            return false;
        }
        int64_t gid = 0;
        if (!parseIntegerText(text.substr(start, position - start), gid) || gid < 0 ||
            static_cast<uint64_t>(gid) > UINT32_MAX) {
            error = "Tiled CSV GID is outside the uint32 range";
            return false;
        }
        gids.push_back(gid);
        while (position < text.size() && (text[position] == ' ' || text[position] == '\t' ||
                                          text[position] == '\r' || text[position] == '\n'))
            ++position;
        if (position < text.size() && text[position] != ',') {
            error = "Tiled CSV values must be comma-separated";
            return false;
        }
    }
    if (gids.size() != expectedCells) {
        error = "Tiled layer cell count does not match its dimensions";
        return false;
    }
    return true;
}

bool decodeBase64Gids(const std::string &text,
                      const std::string &compression,
                      size_t expectedCells,
                      std::vector<int64_t> &gids,
                      std::string &error) {
    if (expectedCells > SIZE_MAX / 4u) {
        error = "Tiled layer byte count overflows";
        return false;
    }
    std::vector<uint8_t> encoded;
    if (!decodeBase64(text, encoded, error))
        return false;
    std::vector<uint8_t> decoded;
    if (!decompressLayer(encoded, compression, expectedCells * 4u, decoded, error))
        return false;
    gids.resize(expectedCells);
    for (size_t index = 0; index < expectedCells; ++index)
        gids[index] = static_cast<int64_t>(readLe32(decoded.data() + index * 4u));
    return true;
}

bool jsonCollisionIsSolid(
    void *objectGroup, int64_t tileWidth, int64_t tileHeight, bool &solid, std::string &error) {
    solid = false;
    if (!objectGroup)
        return true;
    void *objects = mapGet(objectGroup, "objects");
    if (!isSeq(objects)) {
        error = "Tiled tile collision objectgroup is missing objects";
        return false;
    }
    int64_t count = rt_seq_len(objects);
    for (int64_t index = 0; index < count; ++index) {
        void *object = rt_seq_get(objects, index);
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        if (!isMap(object) || !jsonNumberValue(mapGet(object, "x"), x) ||
            !jsonNumberValue(mapGet(object, "y"), y) ||
            !jsonNumberValue(mapGet(object, "width"), width) ||
            !jsonNumberValue(mapGet(object, "height"), height) || x != 0.0 || y != 0.0 ||
            width != static_cast<double>(tileWidth) || height != static_cast<double>(tileHeight) ||
            mapHas(object, "ellipse") || mapHas(object, "polygon") || mapHas(object, "polyline") ||
            mapHas(object, "point") || mapHas(object, "text") || mapHas(object, "gid")) {
            error = "Tiled tile collision is not an unambiguous full-tile rectangle";
            return false;
        }
        solid = true;
    }
    return true;
}

bool xmlCollisionIsSolid(
    void *objectGroup, int64_t tileWidth, int64_t tileHeight, bool &solid, std::string &error) {
    solid = false;
    if (!objectGroup)
        return true;
    int64_t count = rt_xml_child_count(objectGroup);
    for (int64_t index = 0; index < count; ++index) {
        void *object = rt_xml_child_at(objectGroup, index);
        if (rt_xml_node_type(object) != XML_NODE_ELEMENT || xmlTag(object) != "object")
            continue;
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        if (!xmlDouble(object, "x", x, false, error) || !xmlDouble(object, "y", y, false, error) ||
            !xmlDouble(object, "width", width, true, error) ||
            !xmlDouble(object, "height", height, true, error) || x != 0.0 || y != 0.0 ||
            width != static_cast<double>(tileWidth) || height != static_cast<double>(tileHeight))
            return false;
        int64_t childCount = rt_xml_child_count(object);
        for (int64_t childIndex = 0; childIndex < childCount; ++childIndex) {
            void *shape = rt_xml_child_at(object, childIndex);
            if (rt_xml_node_type(shape) == XML_NODE_ELEMENT && xmlTag(shape) != "properties") {
                error = "TMX tile collision is not an unambiguous full-tile rectangle";
                return false;
            }
        }
        solid = true;
    }
    return true;
}

class ImportParser {
  public:
    explicit ImportParser(bool assetMode) : reader_(assetMode) {}

    bool parse(const std::string &path, MapDocument &document, std::string &error) {
        std::string owner;
        std::string text;
        if (!reader_.readRoot(path, owner, text, error))
            return false;
        document.name = std::filesystem::path(owner).stem().string();
        if (!parseMapText(owner, text, document, error))
            return false;
        return finalize(document, error);
    }

  private:
    bool enter(const std::string &owner, std::string &error) {
        if (active_.size() >= 16u) {
            error = "Tiled dependency depth exceeds 16 at: " + owner;
            return false;
        }
        if (!active_.insert(owner).second) {
            error = "cyclic Tiled dependency: " + owner;
            return false;
        }
        return true;
    }

    void leave(const std::string &owner) {
        active_.erase(owner);
    }

    bool parseMapText(const std::string &owner,
                      const std::string &text,
                      MapDocument &document,
                      std::string &error) {
        size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            error = "Tiled map is empty: " + owner;
            return false;
        }
        if (!enter(owner, error))
            return false;
        bool ok = text[first] == '<' ? parseTmxMap(owner, text, document, error)
                                     : parseJsonMap(owner, text, document, error);
        leave(owner);
        return ok;
    }

    bool validateMapDimensions(MapDocument &document, std::string &error) {
        if (document.width <= 0 || document.height <= 0 || document.tileWidth <= 0 ||
            document.tileHeight <= 0) {
            error = "Tiled map dimensions and tile dimensions must be positive";
            return false;
        }
        if (document.width > static_cast<int64_t>(kMaxCellsPerLayer) / document.height) {
            error = "Tiled map layer exceeds 1048576 cells";
            return false;
        }
        return true;
    }

    bool parseJsonMap(const std::string &owner,
                      const std::string &text,
                      MapDocument &document,
                      std::string &error) {
        rt_string source = makeString(text);
        void *rawRoot = nullptr;
        rt_string message = nullptr;
        int64_t line = 0;
        int64_t column = 0;
        bool parsed = rt_json_try_parse(source, &rawRoot, &message, &line, &column) != 0;
        rt_string_unref(source);
        OwnedObject root(rawRoot);
        if (!parsed || !isMap(root.get())) {
            error = "invalid Tiled JSON map";
            if (message) {
                error += ": " + toStd(message);
                rt_string_unref(message);
            }
            if (line > 0)
                error += " at " + std::to_string(line) + ":" + std::to_string(column);
            return false;
        }
        std::string rootType;
        if (!jsonString(root.get(), "type", rootType) || rootType != "map") {
            error = "Tiled JSON root must identify type 'map'";
            return false;
        }
        std::string orientation;
        if (!jsonString(root.get(), "orientation", orientation) || orientation != "orthogonal") {
            error = "only orthogonal Tiled maps are supported";
            return false;
        }
        bool infinite = false;
        if (mapHas(root.get(), "infinite") && !jsonBool(root.get(), "infinite", infinite)) {
            error = "Tiled map 'infinite' must be boolean";
            return false;
        }
        if (infinite) {
            error = "infinite Tiled maps are not supported";
            return false;
        }
        if (!jsonInt(root.get(), "width", document.width) ||
            !jsonInt(root.get(), "height", document.height) ||
            !jsonInt(root.get(), "tilewidth", document.tileWidth) ||
            !jsonInt(root.get(), "tileheight", document.tileHeight) ||
            !validateMapDimensions(document, error))
            return false;
        if (!parseJsonProperties(
                mapGet(root.get(), "properties"), document.properties, error, &reader_, owner))
            return false;

        void *tilesets = mapGet(root.get(), "tilesets");
        if (!isSeq(tilesets)) {
            error = "Tiled map must contain a tilesets array";
            return false;
        }
        int64_t tilesetCount = rt_seq_len(tilesets);
        for (int64_t index = 0; index < tilesetCount; ++index) {
            void *entry = rt_seq_get(tilesets, index);
            int64_t firstGid = 0;
            if (!isMap(entry) || !jsonInt(entry, "firstgid", firstGid) || firstGid <= 0) {
                error = "Tiled tileset reference has an invalid firstgid";
                return false;
            }
            Tileset tileset;
            tileset.firstGid = firstGid;
            std::string dependency;
            if (jsonString(entry, "source", dependency)) {
                std::string resolved;
                std::string dependencyText;
                if (!reader_.readDependency(owner, dependency, resolved, dependencyText, error) ||
                    !parseTilesetText(resolved, dependencyText, tileset, error))
                    return false;
            } else if (!parseJsonTileset(owner, entry, tileset, error)) {
                return false;
            }
            document.tilesets.push_back(std::move(tileset));
        }
        if (document.tilesets.empty()) {
            error = "Tiled map has no tilesets";
            return false;
        }
        std::sort(document.tilesets.begin(),
                  document.tilesets.end(),
                  [](const Tileset &left, const Tileset &right) {
                      return left.firstGid < right.firstGid;
                  });

        void *layers = mapGet(root.get(), "layers");
        if (!isSeq(layers)) {
            error = "Tiled map must contain a layers array";
            return false;
        }
        return parseJsonLayers(owner, layers, "", true, 0.0, 0.0, document, error);
    }

    bool parseTilesetText(const std::string &owner,
                          const std::string &text,
                          Tileset &tileset,
                          std::string &error) {
        size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            error = "Tiled tileset is empty: " + owner;
            return false;
        }
        if (!enter(owner, error))
            return false;
        bool ok = false;
        if (text[first] == '<') {
            std::string normalizedXml = normalizeTiledXmlInput(text);
            rt_string source = makeString(normalizedXml);
            OwnedObject document(rt_xml_parse(source));
            rt_string_unref(source);
            void *root = document.get() ? rt_xml_root(document.get()) : nullptr;
            if (!root || xmlTag(root) != "tileset") {
                error = "external TSX root must be <tileset>";
            } else {
                ok = parseXmlTileset(owner, root, tileset, error);
            }
        } else {
            rt_string source = makeString(text);
            void *rawRoot = nullptr;
            rt_string message = nullptr;
            bool parsed = rt_json_try_parse(source, &rawRoot, &message, nullptr, nullptr) != 0;
            rt_string_unref(source);
            OwnedObject root(rawRoot);
            if (!parsed || !isMap(root.get())) {
                error = "invalid external TSJ tileset";
                if (message) {
                    error += ": " + toStd(message);
                    rt_string_unref(message);
                }
            } else {
                ok = parseJsonTileset(owner, root.get(), tileset, error);
            }
        }
        leave(owner);
        return ok;
    }

    bool parseJsonTileset(const std::string &owner,
                          void *root,
                          Tileset &tileset,
                          std::string &error) {
        std::string rootType;
        if (mapHas(root, "type") &&
            (!jsonString(root, "type", rootType) || rootType != "tileset")) {
            error = "Tiled JSON tileset root has the wrong type";
            return false;
        }
        jsonString(root, "name", tileset.name);
        if (!jsonInt(root, "tilewidth", tileset.tileWidth) ||
            !jsonInt(root, "tileheight", tileset.tileHeight) || tileset.tileWidth <= 0 ||
            tileset.tileHeight <= 0) {
            error = "Tiled tileset has invalid tile dimensions";
            return false;
        }
        if (mapHas(root, "tilecount") &&
            (!jsonInt(root, "tilecount", tileset.tileCount) || tileset.tileCount <= 0)) {
            error = "Tiled tileset has invalid tilecount";
            return false;
        }
        if (mapHas(root, "columns") &&
            (!jsonInt(root, "columns", tileset.columns) || tileset.columns <= 0)) {
            error = "Tiled tileset has invalid columns";
            return false;
        }
        if (mapHas(root, "margin") &&
            (!jsonInt(root, "margin", tileset.margin) || tileset.margin < 0)) {
            error = "Tiled tileset has invalid margin";
            return false;
        }
        if (mapHas(root, "spacing") &&
            (!jsonInt(root, "spacing", tileset.spacing) || tileset.spacing < 0)) {
            error = "Tiled tileset has invalid spacing";
            return false;
        }
        void *tileOffset = mapGet(root, "tileoffset");
        int64_t offsetX = 0;
        int64_t offsetY = 0;
        if (tileOffset && (!jsonInt(tileOffset, "x", offsetX) ||
                           !jsonInt(tileOffset, "y", offsetY) || offsetX != 0 || offsetY != 0)) {
            error = "nonzero Tiled tileset tile offsets are not supported";
            return false;
        }
        std::string imageReference;
        if (!jsonString(root, "image", imageReference)) {
            error = "image-collection Tiled tilesets are not supported";
            return false;
        }
        if (!reader_.resolveDependencyPath(owner, imageReference, tileset.image, error))
            return false;
        jsonInt(root, "imagewidth", tileset.imageWidth);
        jsonInt(root, "imageheight", tileset.imageHeight);

        void *tiles = mapGet(root, "tiles");
        if (!tiles)
            return true;
        if (!isSeq(tiles)) {
            error = "Tiled tileset 'tiles' must be an array";
            return false;
        }
        int64_t count = rt_seq_len(tiles);
        for (int64_t index = 0; index < count; ++index) {
            void *tile = rt_seq_get(tiles, index);
            int64_t id = 0;
            if (!isMap(tile) || !jsonInt(tile, "id", id) || id < 0) {
                error = "Tiled tile metadata has an invalid id";
                return false;
            }
            if (mapHas(tile, "image")) {
                error = "per-tile images are not supported in Tiled tilesets";
                return false;
            }
            TileMetadata metadata;
            if (!parseJsonProperties(
                    mapGet(tile, "properties"), metadata.properties, error, &reader_, owner))
                return false;
            for (const auto &[name, scalar] : metadata.properties) {
                if (scalar.kind != ScalarKind::Integer && scalar.kind != ScalarKind::Boolean) {
                    error = "Tiled tile property '" + name + "' must be int or bool";
                    return false;
                }
            }
            if (!jsonCollisionIsSolid(mapGet(tile, "objectgroup"),
                                      tileset.tileWidth,
                                      tileset.tileHeight,
                                      metadata.solid,
                                      error))
                return false;
            void *animation = mapGet(tile, "animation");
            if (animation) {
                if (!isSeq(animation) || rt_seq_len(animation) <= 0) {
                    error = "Tiled tile animation must contain frames";
                    return false;
                }
                int64_t frameCount = rt_seq_len(animation);
                for (int64_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
                    void *frame = rt_seq_get(animation, frameIndex);
                    int64_t frameTile = 0;
                    int64_t duration = 0;
                    if (!isMap(frame) || !jsonInt(frame, "tileid", frameTile) || frameTile < 0 ||
                        !jsonInt(frame, "duration", duration) || duration <= 0) {
                        error = "Tiled tile animation frame is invalid";
                        return false;
                    }
                    metadata.animationFrames.push_back(frameTile + 1);
                    metadata.animationDurations.push_back(duration);
                }
            }
            tileset.metadata[id] = std::move(metadata);
        }
        return true;
    }

    bool parseXmlTileset(const std::string &owner,
                         void *root,
                         Tileset &tileset,
                         std::string &error) {
        tileset.name = xmlString(root, "name");
        if (!xmlInt(root, "tilewidth", tileset.tileWidth, true, error) ||
            !xmlInt(root, "tileheight", tileset.tileHeight, true, error) ||
            tileset.tileWidth <= 0 || tileset.tileHeight <= 0) {
            if (error.empty())
                error = "TMX tileset has invalid tile dimensions";
            return false;
        }
        if (!xmlInt(root, "tilecount", tileset.tileCount, false, error) ||
            !xmlInt(root, "columns", tileset.columns, false, error) ||
            !xmlInt(root, "margin", tileset.margin, false, error) ||
            !xmlInt(root, "spacing", tileset.spacing, false, error) || tileset.tileCount < 0 ||
            tileset.columns < 0 || tileset.margin < 0 || tileset.spacing < 0)
            return false;
        void *tileOffset = firstXmlChild(root, "tileoffset");
        if (tileOffset) {
            int64_t offsetX = 0;
            int64_t offsetY = 0;
            if (!xmlInt(tileOffset, "x", offsetX, false, error) ||
                !xmlInt(tileOffset, "y", offsetY, false, error) || offsetX != 0 || offsetY != 0) {
                error = "nonzero TMX tileset tile offsets are not supported";
                return false;
            }
        }
        void *image = firstXmlChild(root, "image");
        std::string imageReference = image ? xmlString(image, "source") : std::string();
        if (!image || imageReference.empty()) {
            error = "image-collection TMX tilesets are not supported";
            return false;
        }
        if (!reader_.resolveDependencyPath(owner, imageReference, tileset.image, error) ||
            !xmlInt(image, "width", tileset.imageWidth, false, error) ||
            !xmlInt(image, "height", tileset.imageHeight, false, error))
            return false;

        int64_t childCount = rt_xml_child_count(root);
        for (int64_t index = 0; index < childCount; ++index) {
            void *tile = rt_xml_child_at(root, index);
            if (rt_xml_node_type(tile) != XML_NODE_ELEMENT || xmlTag(tile) != "tile")
                continue;
            int64_t id = 0;
            if (!xmlInt(tile, "id", id, true, error) || id < 0) {
                error = "TMX tile metadata has an invalid id";
                return false;
            }
            if (firstXmlChild(tile, "image")) {
                error = "per-tile images are not supported in TMX tilesets";
                return false;
            }
            TileMetadata metadata;
            if (!parseXmlProperties(tile, metadata.properties, error, &reader_, owner))
                return false;
            for (const auto &[name, scalar] : metadata.properties) {
                if (scalar.kind != ScalarKind::Integer && scalar.kind != ScalarKind::Boolean) {
                    error = "TMX tile property '" + name + "' must be int or bool";
                    return false;
                }
            }
            if (!xmlCollisionIsSolid(firstXmlChild(tile, "objectgroup"),
                                     tileset.tileWidth,
                                     tileset.tileHeight,
                                     metadata.solid,
                                     error))
                return false;
            void *animation = firstXmlChild(tile, "animation");
            if (animation) {
                int64_t animationChildCount = rt_xml_child_count(animation);
                for (int64_t frameIndex = 0; frameIndex < animationChildCount; ++frameIndex) {
                    void *frame = rt_xml_child_at(animation, frameIndex);
                    if (rt_xml_node_type(frame) != XML_NODE_ELEMENT || xmlTag(frame) != "frame")
                        continue;
                    int64_t tileId = 0;
                    int64_t duration = 0;
                    if (!xmlInt(frame, "tileid", tileId, true, error) || tileId < 0 ||
                        !xmlInt(frame, "duration", duration, true, error) || duration <= 0) {
                        error = "TMX tile animation frame is invalid";
                        return false;
                    }
                    metadata.animationFrames.push_back(tileId + 1);
                    metadata.animationDurations.push_back(duration);
                }
                if (metadata.animationFrames.empty()) {
                    error = "TMX tile animation must contain frames";
                    return false;
                }
            }
            tileset.metadata[id] = std::move(metadata);
        }
        return true;
    }

    bool normalizeGids(std::vector<int64_t> &gids,
                       MapDocument &document,
                       int64_t &tilesetIndex,
                       std::string &error) {
        tilesetIndex = -1;
        for (int64_t &raw : gids) {
            if (raw < 0 || static_cast<uint64_t>(raw) > UINT32_MAX) {
                error = "Tiled GID is outside the uint32 range";
                return false;
            }
            uint64_t gid = static_cast<uint64_t>(raw);
            if ((gid & kTiledTransformMask) != 0u) {
                error = "Tiled tile transform flags are not representable by Tilemap";
                return false;
            }
            if (gid == 0u)
                continue;
            int64_t selected = -1;
            for (size_t index = 0; index < document.tilesets.size(); ++index) {
                if (static_cast<uint64_t>(document.tilesets[index].firstGid) <= gid)
                    selected = static_cast<int64_t>(index);
                else
                    break;
            }
            if (selected < 0) {
                error = "Tiled GID does not belong to a declared tileset";
                return false;
            }
            Tileset &tileset = document.tilesets[static_cast<size_t>(selected)];
            int64_t local = static_cast<int64_t>(gid) - tileset.firstGid + 1;
            if (local <= 0 || (tileset.tileCount > 0 && local > tileset.tileCount)) {
                error = "Tiled GID exceeds its tileset tilecount";
                return false;
            }
            if (tilesetIndex >= 0 && tilesetIndex != selected) {
                error = "a Tiled tile layer uses more than one tileset";
                return false;
            }
            tilesetIndex = selected;
            raw = local;
        }
        if (tilesetIndex < 0)
            tilesetIndex = 0;
        return true;
    }

    bool addLayer(std::string name,
                  bool visible,
                  std::vector<int64_t> gids,
                  MapDocument &document,
                  std::string &error) {
        if (document.layers.size() >= kMaxLayers) {
            error = "Tiled tile layer count exceeds Tilemap's 16-layer limit";
            return false;
        }
        if (gids.size() != static_cast<size_t>(document.width * document.height)) {
            error = "Tiled layer cell count does not match map dimensions";
            return false;
        }
        if (document.totalCells > kMaxTotalCells - gids.size()) {
            error = "Tiled map exceeds the 4194304 total-cell limit";
            return false;
        }
        Layer layer;
        layer.name =
            name.empty() ? "Layer" + std::to_string(document.layers.size()) : std::move(name);
        layer.visible = visible;
        if (!normalizeGids(gids, document, layer.tileset, error))
            return false;
        layer.tiles = std::move(gids);
        document.totalCells += layer.tiles.size();
        document.layers.push_back(std::move(layer));
        return true;
    }

    bool placeLayerGids(const std::vector<int64_t> &source,
                        int64_t sourceWidth,
                        int64_t sourceHeight,
                        int64_t offsetX,
                        int64_t offsetY,
                        const MapDocument &document,
                        std::vector<int64_t> &placed,
                        std::string &error) {
        if (sourceWidth <= 0 || sourceHeight <= 0 ||
            sourceWidth > static_cast<int64_t>(kMaxCellsPerLayer) / sourceHeight ||
            source.size() != static_cast<size_t>(sourceWidth * sourceHeight)) {
            error = "Tiled tile layer dimensions or cell count are invalid";
            return false;
        }
        placed.assign(static_cast<size_t>(document.width * document.height), 0);
        for (int64_t y = 0; y < sourceHeight; ++y) {
            for (int64_t x = 0; x < sourceWidth; ++x) {
                int64_t gid = source[static_cast<size_t>(y * sourceWidth + x)];
                int64_t destinationX = x + offsetX;
                int64_t destinationY = y + offsetY;
                if (destinationX < 0 || destinationY < 0 || destinationX >= document.width ||
                    destinationY >= document.height) {
                    if (gid != 0) {
                        error = "offset Tiled layer places a nonempty tile outside the finite map";
                        return false;
                    }
                    continue;
                }
                placed[static_cast<size_t>(destinationY * document.width + destinationX)] = gid;
            }
        }
        return true;
    }

    bool validateLayerRenderingJson(void *layer, std::string &error) {
        double opacity = 1.0;
        double parallaxX = 1.0;
        double parallaxY = 1.0;
        std::string tint;
        if ((mapHas(layer, "opacity") &&
             (!jsonNumberValue(mapGet(layer, "opacity"), opacity) || opacity != 1.0)) ||
            (mapHas(layer, "parallaxx") &&
             (!jsonNumberValue(mapGet(layer, "parallaxx"), parallaxX) || parallaxX != 1.0)) ||
            (mapHas(layer, "parallaxy") &&
             (!jsonNumberValue(mapGet(layer, "parallaxy"), parallaxY) || parallaxY != 1.0)) ||
            (jsonString(layer, "tintcolor", tint) && !tint.empty())) {
            error = "Tiled layer opacity, tint, and parallax are not representable by Tilemap";
            return false;
        }
        return true;
    }

    bool parseJsonTileLayer(void *layer,
                            const std::string &name,
                            bool visible,
                            double inheritedX,
                            double inheritedY,
                            MapDocument &document,
                            std::string &error) {
        int64_t width = 0;
        int64_t height = 0;
        int64_t layerX = 0;
        int64_t layerY = 0;
        double offsetX = inheritedX;
        double offsetY = inheritedY;
        if (!jsonInt(layer, "width", width) || !jsonInt(layer, "height", height) || width <= 0 ||
            height <= 0 || width > static_cast<int64_t>(kMaxCellsPerLayer) / height) {
            error = "finite Tiled tile layer dimensions are invalid";
            return false;
        }
        if (mapHas(layer, "x") && !jsonInt(layer, "x", layerX)) {
            error = "Tiled tile layer x offset must be integral";
            return false;
        }
        if (mapHas(layer, "y") && !jsonInt(layer, "y", layerY)) {
            error = "Tiled tile layer y offset must be integral";
            return false;
        }
        double ownOffset = 0.0;
        if (mapHas(layer, "offsetx")) {
            if (!jsonNumberValue(mapGet(layer, "offsetx"), ownOffset)) {
                error = "Tiled tile layer offsetx must be numeric";
                return false;
            }
            offsetX += ownOffset;
        }
        ownOffset = 0.0;
        if (mapHas(layer, "offsety")) {
            if (!jsonNumberValue(mapGet(layer, "offsety"), ownOffset)) {
                error = "Tiled tile layer offsety must be numeric";
                return false;
            }
            offsetY += ownOffset;
        }
        if (offsetX < static_cast<double>(INT64_MIN) || offsetX > static_cast<double>(INT64_MAX) ||
            offsetY < static_cast<double>(INT64_MIN) || offsetY > static_cast<double>(INT64_MAX) ||
            std::trunc(offsetX) != offsetX || std::trunc(offsetY) != offsetY ||
            static_cast<int64_t>(offsetX) % document.tileWidth != 0 ||
            static_cast<int64_t>(offsetY) % document.tileHeight != 0) {
            error = "Tiled tile-layer pixel offsets must be integral tile offsets";
            return false;
        }
        size_t cells = static_cast<size_t>(width * height);
        void *data = mapGet(layer, "data");
        std::vector<int64_t> gids;
        if (isSeq(data)) {
            if (mapHas(layer, "encoding") || mapHas(layer, "compression")) {
                error = "array-backed Tiled layer data cannot declare encoding/compression";
                return false;
            }
            int64_t count = rt_seq_len(data);
            if (count < 0 || static_cast<size_t>(count) != cells) {
                error = "Tiled layer cell count does not match its dimensions";
                return false;
            }
            gids.resize(cells);
            for (int64_t index = 0; index < count; ++index) {
                if (!jsonIntValue(rt_seq_get(data, index), gids[static_cast<size_t>(index)])) {
                    error = "Tiled layer data contains a non-integer GID";
                    return false;
                }
            }
        } else {
            std::string encoded;
            std::string encoding;
            std::string compression;
            if (!jsonStringValue(data, encoded) || !jsonString(layer, "encoding", encoding)) {
                error = "Tiled layer data must be an array, CSV string, or Base64 string";
                return false;
            }
            jsonString(layer, "compression", compression);
            if (encoding == "csv") {
                if (!compression.empty()) {
                    error = "Tiled CSV layer data cannot be compressed";
                    return false;
                }
                if (!parseCsv(encoded, cells, gids, error))
                    return false;
            } else if (encoding == "base64") {
                if (!decodeBase64Gids(encoded, compression, cells, gids, error))
                    return false;
            } else {
                error = "unsupported Tiled layer encoding '" + encoding + "'";
                return false;
            }
        }
        std::vector<int64_t> placed;
        int64_t tileOffsetX = layerX + static_cast<int64_t>(offsetX) / document.tileWidth;
        int64_t tileOffsetY = layerY + static_cast<int64_t>(offsetY) / document.tileHeight;
        if (!placeLayerGids(gids, width, height, tileOffsetX, tileOffsetY, document, placed, error))
            return false;
        return addLayer(name, visible, std::move(placed), document, error);
    }

    bool addReservedProperty(std::map<std::string, Scalar> &properties,
                             const std::string &name,
                             Scalar value,
                             std::string &error) {
        if (properties.find(name) != properties.end()) {
            error = "Tiled property collides with reserved import key '" + name + "'";
            return false;
        }
        return setProperty(properties, name, std::move(value), error);
    }

    bool preserveLayerPropertiesJson(const std::string &owner,
                                     void *layer,
                                     const std::string &layerName,
                                     MapDocument &document,
                                     std::string &error) {
        std::map<std::string, Scalar> properties;
        if (!parseJsonProperties(mapGet(layer, "properties"), properties, error, &reader_, owner))
            return false;
        for (auto &[name, value] : properties) {
            std::string key = "tiled.layer." + layerName + "." + name;
            if (document.properties.find(key) != document.properties.end()) {
                error = "Tiled layer property collides with scene property '" + key + "'";
                return false;
            }
            if (!setProperty(document.properties, key, std::move(value), error))
                return false;
        }
        return true;
    }

    bool preserveLayerPropertiesXml(const std::string &owner,
                                    void *layer,
                                    const std::string &layerName,
                                    MapDocument &document,
                                    std::string &error) {
        std::map<std::string, Scalar> properties;
        if (!parseXmlProperties(layer, properties, error, &reader_, owner))
            return false;
        for (auto &[name, value] : properties) {
            std::string key = "tiled.layer." + layerName + "." + name;
            if (document.properties.find(key) != document.properties.end()) {
                error = "TMX layer property collides with scene property '" + key + "'";
                return false;
            }
            if (!setProperty(document.properties, key, std::move(value), error))
                return false;
        }
        return true;
    }

    Scalar integerScalar(int64_t value) const {
        Scalar scalar;
        scalar.kind = ScalarKind::Integer;
        scalar.integer = value;
        return scalar;
    }

    Scalar floatScalar(double value) const {
        Scalar scalar;
        scalar.kind = ScalarKind::Float;
        scalar.floating = value;
        return scalar;
    }

    Scalar boolScalar(bool value) const {
        Scalar scalar;
        scalar.kind = ScalarKind::Boolean;
        scalar.boolean = value;
        return scalar;
    }

    Scalar stringScalar(std::string value) const {
        Scalar scalar;
        scalar.kind = ScalarKind::String;
        scalar.string = std::move(value);
        return scalar;
    }

    bool chooseObjectId(const std::string &authoredName,
                        int64_t numericId,
                        std::string &result,
                        std::string &error) {
        result = authoredName;
        if (result.empty() || objectIds_.find(result) != objectIds_.end())
            result = std::to_string(numericId);
        if (result.empty() || !objectIds_.insert(result).second) {
            error = "Tiled object names and numeric IDs do not provide a unique SceneDocument ID";
            return false;
        }
        return true;
    }

    bool canonicalJsonProperty(void *value,
                               std::map<std::string, Scalar> &properties,
                               const std::string &name,
                               std::string &error) {
        rt_string formatted = rt_json_format(value);
        if (!formatted) {
            error = "cannot preserve Tiled object shape data";
            return false;
        }
        std::string text = toStd(formatted);
        rt_string_unref(formatted);
        if (text.size() > kMaxStringBytes) {
            error = "Tiled object shape data exceeds 64 KiB";
            return false;
        }
        return addReservedProperty(properties, name, stringScalar(std::move(text)), error);
    }

    bool parseJsonTemplateObject(const std::string &owner,
                                 void *instance,
                                 const std::string &templateReference,
                                 const std::string &layerName,
                                 bool layerVisible,
                                 double offsetX,
                                 double offsetY,
                                 MapDocument &document,
                                 std::string &error) {
        std::string resolved;
        std::string text;
        if (!reader_.readDependency(owner, templateReference, resolved, text, error))
            return false;
        size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || text[first] == '<') {
            error = "a JSON Tiled object requires a JSON object template";
            return false;
        }
        if (!enter(resolved, error))
            return false;
        rt_string source = makeString(text);
        void *rawRoot = nullptr;
        rt_string message = nullptr;
        bool parsed = rt_json_try_parse(source, &rawRoot, &message, nullptr, nullptr) != 0;
        rt_string_unref(source);
        OwnedObject root(rawRoot);
        if (!parsed || !isMap(root.get())) {
            error = "invalid Tiled JSON object template: " + resolved;
            if (message) {
                error += ": " + toStd(message);
                rt_string_unref(message);
            }
            leave(resolved);
            return false;
        }
        std::string type;
        void *templateObject = mapGet(root.get(), "object");
        if (!jsonString(root.get(), "type", type) || type != "template" || !isMap(templateObject)) {
            error = "Tiled JSON template root must identify type 'template' and contain object";
            leave(resolved);
            return false;
        }
        OwnedObject merged(rt_map_clone(templateObject));
        if (!merged.get()) {
            error = "cannot allocate merged Tiled object template";
            leave(resolved);
            return false;
        }
        OwnedObject mergedProperties(rt_seq_new_owned());
        auto appendProperties = [&](void *properties) {
            if (!isSeq(properties))
                return;
            int64_t count = rt_seq_len(properties);
            for (int64_t index = 0; index < count; ++index)
                rt_seq_push(mergedProperties.get(), rt_seq_get(properties, index));
        };
        appendProperties(mapGet(templateObject, "properties"));
        appendProperties(mapGet(instance, "properties"));
        rt_string propertiesKey = rt_const_cstr("properties");
        rt_map_set(merged.get(), propertiesKey, mergedProperties.get());
        rt_string_unref(propertiesKey);

        OwnedObject keys(rt_map_keys(instance));
        int64_t keyCount = rt_seq_len(keys.get());
        for (int64_t index = 0; index < keyCount; ++index) {
            rt_string key = rt_seq_get_str(keys.get(), index);
            std::string name = toStd(key);
            if (name != "template" && name != "properties")
                rt_map_set(merged.get(), key, rt_map_get(instance, key));
            rt_string_unref(key);
        }
        rt_string markerKey = rt_const_cstr("__zannaTemplatePath");
        rt_string markerValue = makeString(resolved);
        rt_map_set_str(merged.get(), markerKey, markerValue);
        rt_string_unref(markerKey);
        rt_string_unref(markerValue);
        bool ok = parseJsonObject(
            resolved, merged.get(), layerName, layerVisible, offsetX, offsetY, document, error);
        leave(resolved);
        return ok;
    }

    bool parseJsonObject(const std::string &owner,
                         void *object,
                         const std::string &layerName,
                         bool layerVisible,
                         double offsetX,
                         double offsetY,
                         MapDocument &document,
                         std::string &error) {
        if (!isMap(object)) {
            error = "Tiled object layer contains a non-object entry";
            return false;
        }
        std::string templateReference;
        if (jsonString(object, "template", templateReference))
            return parseJsonTemplateObject(owner,
                                           object,
                                           templateReference,
                                           layerName,
                                           layerVisible,
                                           offsetX,
                                           offsetY,
                                           document,
                                           error);
        if (document.objects.size() >= kMaxObjects) {
            error = "Tiled object count exceeds 65536";
            return false;
        }
        double x = 0.0;
        double y = 0.0;
        if ((mapHas(object, "x") && !jsonNumberValue(mapGet(object, "x"), x)) ||
            (mapHas(object, "y") && !jsonNumberValue(mapGet(object, "y"), y))) {
            error = "Tiled object coordinates must be numeric";
            return false;
        }
        x += offsetX;
        y += offsetY;
        if (x < static_cast<double>(INT64_MIN) || x > static_cast<double>(INT64_MAX) ||
            y < static_cast<double>(INT64_MIN) || y > static_cast<double>(INT64_MAX)) {
            error = "Tiled object coordinates are outside the SceneDocument integer range";
            return false;
        }
        SceneObject result;
        result.x = static_cast<int64_t>(x);
        result.y = static_cast<int64_t>(y);
        if (!jsonString(object, "class", result.type) || result.type.empty())
            jsonString(object, "type", result.type);
        if (result.type.empty())
            result.type = "TiledObject";
        std::string authoredName;
        jsonString(object, "name", authoredName);
        int64_t numericId = 0;
        if (mapHas(object, "id") && (!jsonInt(object, "id", numericId) || numericId < 0)) {
            error = "Tiled object id must be a nonnegative integer";
            return false;
        }
        if (!chooseObjectId(authoredName, numericId, result.id, error))
            return false;
        if (!parseJsonProperties(
                mapGet(object, "properties"), result.properties, error, &reader_, owner) ||
            !addReservedProperty(
                result.properties, "tiled.layer", stringScalar(layerName), error) ||
            !addReservedProperty(
                result.properties, "tiled.numericId", integerScalar(numericId), error) ||
            !addReservedProperty(
                result.properties, "tiled.layerVisible", boolScalar(layerVisible), error))
            return false;
        if (!addReservedProperty(result.properties, "tiled.sourceX", floatScalar(x), error) ||
            !addReservedProperty(result.properties, "tiled.sourceY", floatScalar(y), error))
            return false;
        std::string templatePath;
        if (jsonString(object, "__zannaTemplatePath", templatePath) &&
            !addReservedProperty(
                result.properties, "tiled.template", stringScalar(templatePath), error))
            return false;

        double width = 0.0;
        double height = 0.0;
        double rotation = 0.0;
        if ((mapHas(object, "width") && !jsonNumberValue(mapGet(object, "width"), width)) ||
            (mapHas(object, "height") && !jsonNumberValue(mapGet(object, "height"), height)) ||
            (mapHas(object, "rotation") &&
             !jsonNumberValue(mapGet(object, "rotation"), rotation))) {
            error = "Tiled object geometry must be numeric";
            return false;
        }
        if (!addReservedProperty(result.properties, "tiled.width", floatScalar(width), error) ||
            !addReservedProperty(result.properties, "tiled.height", floatScalar(height), error) ||
            !addReservedProperty(result.properties, "tiled.rotation", floatScalar(rotation), error))
            return false;
        bool objectVisible = true;
        if (mapHas(object, "visible") && !jsonBool(object, "visible", objectVisible)) {
            error = "Tiled object visibility must be boolean";
            return false;
        }
        if (!addReservedProperty(
                result.properties, "tiled.visible", boolScalar(objectVisible), error))
            return false;

        int64_t gid = 0;
        if (mapHas(object, "gid")) {
            if (!jsonInt(object, "gid", gid) || gid < 0 ||
                static_cast<uint64_t>(gid) > UINT32_MAX) {
                error = "Tiled tile object GID is invalid";
                return false;
            }
            if ((static_cast<uint64_t>(gid) & kTiledTransformMask) != 0u) {
                error = "Tiled object transform flags are not representable by SceneDocument";
                return false;
            }
            if (!addReservedProperty(result.properties, "tiled.gid", integerScalar(gid), error))
                return false;
        }
        const char *shapeKeys[] = {"ellipse", "point", "polygon", "polyline", "text"};
        for (const char *key : shapeKeys) {
            if (mapHas(object, key) &&
                !canonicalJsonProperty(
                    mapGet(object, key), result.properties, std::string("tiled.") + key, error))
                return false;
        }
        document.objects.push_back(std::move(result));
        (void)owner;
        return true;
    }

    bool parseJsonObjectLayer(const std::string &owner,
                              void *layer,
                              const std::string &name,
                              bool visible,
                              double inheritedX,
                              double inheritedY,
                              MapDocument &document,
                              std::string &error) {
        double offsetX = inheritedX;
        double offsetY = inheritedY;
        double value = 0.0;
        if (mapHas(layer, "offsetx")) {
            if (!jsonNumberValue(mapGet(layer, "offsetx"), value)) {
                error = "Tiled object layer offsetx must be numeric";
                return false;
            }
            offsetX += value;
        }
        value = 0.0;
        if (mapHas(layer, "offsety")) {
            if (!jsonNumberValue(mapGet(layer, "offsety"), value)) {
                error = "Tiled object layer offsety must be numeric";
                return false;
            }
            offsetY += value;
        }
        void *objects = mapGet(layer, "objects");
        if (!isSeq(objects)) {
            error = "Tiled object layer is missing an objects array";
            return false;
        }
        int64_t count = rt_seq_len(objects);
        for (int64_t index = 0; index < count; ++index) {
            if (!parseJsonObject(owner,
                                 rt_seq_get(objects, index),
                                 name,
                                 visible,
                                 offsetX,
                                 offsetY,
                                 document,
                                 error))
                return false;
        }
        return true;
    }

    bool parseJsonImageLayer(const std::string &owner,
                             void *layer,
                             const std::string &name,
                             bool visible,
                             double inheritedX,
                             double inheritedY,
                             MapDocument &document,
                             std::string &error) {
        std::string reference;
        if (!jsonString(layer, "image", reference)) {
            error = "Tiled image layer is missing an image";
            return false;
        }
        std::string resolved;
        if (!reader_.resolveDependencyPath(owner, reference, resolved, error))
            return false;
        double x = inheritedX;
        double y = inheritedY;
        double value = 0.0;
        if (mapHas(layer, "offsetx")) {
            if (!jsonNumberValue(mapGet(layer, "offsetx"), value)) {
                error = "Tiled image layer offsetx must be numeric";
                return false;
            }
            x += value;
        }
        value = 0.0;
        if (mapHas(layer, "offsety")) {
            if (!jsonNumberValue(mapGet(layer, "offsety"), value)) {
                error = "Tiled image layer offsety must be numeric";
                return false;
            }
            y += value;
        }
        if (std::trunc(x) != x || std::trunc(y) != y) {
            error = "fractional Tiled image-layer offsets are not supported";
            return false;
        }
        SceneObject object;
        object.type = "tiled.image-layer";
        object.id = name;
        object.x = static_cast<int64_t>(x);
        object.y = static_cast<int64_t>(y);
        if (!parseJsonProperties(
                mapGet(layer, "properties"), object.properties, error, &reader_, owner) ||
            !addReservedProperty(object.properties, "tiled.image", stringScalar(resolved), error) ||
            !addReservedProperty(object.properties, "tiled.visible", boolScalar(visible), error))
            return false;
        bool repeatX = false;
        bool repeatY = false;
        if ((mapHas(layer, "repeatx") && !jsonBool(layer, "repeatx", repeatX)) ||
            (mapHas(layer, "repeaty") && !jsonBool(layer, "repeaty", repeatY))) {
            error = "Tiled image-layer repeat flags must be boolean";
            return false;
        }
        if (!addReservedProperty(object.properties, "tiled.repeatX", boolScalar(repeatX), error) ||
            !addReservedProperty(object.properties, "tiled.repeatY", boolScalar(repeatY), error))
            return false;
        document.objects.push_back(std::move(object));
        return true;
    }

    bool parseJsonLayers(const std::string &owner,
                         void *layers,
                         const std::string &prefix,
                         bool inheritedVisible,
                         double inheritedX,
                         double inheritedY,
                         MapDocument &document,
                         std::string &error) {
        int64_t count = rt_seq_len(layers);
        for (int64_t index = 0; index < count; ++index) {
            void *layer = rt_seq_get(layers, index);
            std::string type;
            std::string ownName;
            if (!isMap(layer) || !jsonString(layer, "type", type)) {
                error = "Tiled layer is missing a string type";
                return false;
            }
            jsonString(layer, "name", ownName);
            std::string name = prefix.empty() ? ownName : prefix + "/" + ownName;
            bool ownVisible = true;
            if (mapHas(layer, "visible") && !jsonBool(layer, "visible", ownVisible)) {
                error = "Tiled layer visibility must be boolean";
                return false;
            }
            bool visible = inheritedVisible && ownVisible;
            if (!validateLayerRenderingJson(layer, error))
                return false;
            if (!preserveLayerPropertiesJson(owner, layer, name, document, error))
                return false;
            if (type == "tilelayer") {
                if (!parseJsonTileLayer(
                        layer, name, visible, inheritedX, inheritedY, document, error))
                    return false;
            } else if (type == "objectgroup") {
                if (!parseJsonObjectLayer(
                        owner, layer, name, visible, inheritedX, inheritedY, document, error))
                    return false;
            } else if (type == "imagelayer") {
                if (!parseJsonImageLayer(
                        owner, layer, name, visible, inheritedX, inheritedY, document, error))
                    return false;
            } else if (type == "group") {
                double groupX = inheritedX;
                double groupY = inheritedY;
                double offset = 0.0;
                if (mapHas(layer, "offsetx")) {
                    if (!jsonNumberValue(mapGet(layer, "offsetx"), offset)) {
                        error = "Tiled group offsetx must be numeric";
                        return false;
                    }
                    groupX += offset;
                }
                offset = 0.0;
                if (mapHas(layer, "offsety")) {
                    if (!jsonNumberValue(mapGet(layer, "offsety"), offset)) {
                        error = "Tiled group offsety must be numeric";
                        return false;
                    }
                    groupY += offset;
                }
                void *children = mapGet(layer, "layers");
                if (!isSeq(children) ||
                    !parseJsonLayers(
                        owner, children, name, visible, groupX, groupY, document, error))
                    return false;
            } else {
                error = "unsupported Tiled layer type '" + type + "'";
                return false;
            }
        }
        return true;
    }

    bool validateLayerRenderingXml(void *layer, std::string &error) {
        double opacity = 1.0;
        double parallaxX = 1.0;
        double parallaxY = 1.0;
        if (!xmlDouble(layer, "opacity", opacity, false, error) ||
            !xmlDouble(layer, "parallaxx", parallaxX, false, error) ||
            !xmlDouble(layer, "parallaxy", parallaxY, false, error))
            return false;
        if (opacity != 1.0 || parallaxX != 1.0 || parallaxY != 1.0 ||
            !xmlString(layer, "tintcolor").empty()) {
            error = "TMX layer opacity, tint, and parallax are not representable by Tilemap";
            return false;
        }
        return true;
    }

    bool parseXmlTileLayer(void *layer,
                           const std::string &name,
                           bool visible,
                           double inheritedX,
                           double inheritedY,
                           MapDocument &document,
                           std::string &error) {
        int64_t width = 0;
        int64_t height = 0;
        int64_t layerX = 0;
        int64_t layerY = 0;
        double offsetX = inheritedX;
        double offsetY = inheritedY;
        double ownOffset = 0.0;
        if (!xmlInt(layer, "width", width, true, error) ||
            !xmlInt(layer, "height", height, true, error) || width <= 0 || height <= 0 ||
            width > static_cast<int64_t>(kMaxCellsPerLayer) / height ||
            !xmlInt(layer, "x", layerX, false, error) ||
            !xmlInt(layer, "y", layerY, false, error) ||
            !xmlDouble(layer, "offsetx", ownOffset, false, error)) {
            if (error.empty())
                error = "finite TMX tile layer dimensions are invalid";
            return false;
        }
        offsetX += ownOffset;
        ownOffset = 0.0;
        if (!xmlDouble(layer, "offsety", ownOffset, false, error))
            return false;
        offsetY += ownOffset;
        if (offsetX < static_cast<double>(INT64_MIN) || offsetX > static_cast<double>(INT64_MAX) ||
            offsetY < static_cast<double>(INT64_MIN) || offsetY > static_cast<double>(INT64_MAX) ||
            std::trunc(offsetX) != offsetX || std::trunc(offsetY) != offsetY ||
            static_cast<int64_t>(offsetX) % document.tileWidth != 0 ||
            static_cast<int64_t>(offsetY) % document.tileHeight != 0) {
            error = "TMX tile-layer pixel offsets must be integral tile offsets";
            return false;
        }
        void *data = firstXmlChild(layer, "data");
        if (!data) {
            error = "TMX tile layer is missing <data>";
            return false;
        }
        size_t cells = static_cast<size_t>(width * height);
        std::string encoding = xmlString(data, "encoding");
        std::string compression = xmlString(data, "compression");
        std::vector<int64_t> gids;
        if (encoding.empty()) {
            if (!compression.empty()) {
                error = "unencoded TMX layer data cannot be compressed";
                return false;
            }
            int64_t childCount = rt_xml_child_count(data);
            for (int64_t index = 0; index < childCount; ++index) {
                void *tile = rt_xml_child_at(data, index);
                if (rt_xml_node_type(tile) != XML_NODE_ELEMENT || xmlTag(tile) != "tile")
                    continue;
                int64_t gid = 0;
                if (!xmlInt(tile, "gid", gid, true, error))
                    return false;
                gids.push_back(gid);
            }
            if (gids.size() != cells) {
                error = "TMX layer cell count does not match its dimensions";
                return false;
            }
        } else if (encoding == "csv") {
            if (!compression.empty()) {
                error = "TMX CSV layer data cannot be compressed";
                return false;
            }
            if (!parseCsv(xmlText(data), cells, gids, error))
                return false;
        } else if (encoding == "base64") {
            if (!decodeBase64Gids(xmlText(data), compression, cells, gids, error))
                return false;
        } else {
            error = "unsupported TMX layer encoding '" + encoding + "'";
            return false;
        }
        std::vector<int64_t> placed;
        int64_t tileOffsetX = layerX + static_cast<int64_t>(offsetX) / document.tileWidth;
        int64_t tileOffsetY = layerY + static_cast<int64_t>(offsetY) / document.tileHeight;
        if (!placeLayerGids(gids, width, height, tileOffsetX, tileOffsetY, document, placed, error))
            return false;
        return addLayer(name, visible, std::move(placed), document, error);
    }

    bool preserveXmlShape(void *shape,
                          std::map<std::string, Scalar> &properties,
                          const std::string &name,
                          std::string &error) {
        rt_string formatted = rt_xml_format(shape);
        if (!formatted) {
            error = "cannot preserve TMX object shape data";
            return false;
        }
        std::string text = toStd(formatted);
        rt_string_unref(formatted);
        if (text.size() > kMaxStringBytes) {
            error = "TMX object shape data exceeds 64 KiB";
            return false;
        }
        return addReservedProperty(properties, name, stringScalar(std::move(text)), error);
    }

    void copyXmlAttributes(void *from, void *to, bool skipTemplate) {
        OwnedObject names(rt_xml_attr_names(from));
        int64_t count = names.get() ? rt_seq_len(names.get()) : 0;
        for (int64_t index = 0; index < count; ++index) {
            rt_string name = rt_seq_get_str(names.get(), index);
            std::string key = toStd(name);
            if (!skipTemplate || key != "template") {
                rt_string value = rt_xml_attr(from, name);
                rt_xml_set_attr(to, name, value);
                rt_str_release_maybe(value);
            }
            rt_string_unref(name);
        }
    }

    void *cloneXmlNode(void *node) {
        int64_t type = rt_xml_node_type(node);
        if (type == XML_NODE_ELEMENT) {
            rt_string tag = rt_xml_tag(node);
            void *clone = rt_xml_element(tag);
            rt_str_release_maybe(tag);
            if (!clone)
                return nullptr;
            copyXmlAttributes(node, clone, false);
            int64_t count = rt_xml_child_count(node);
            for (int64_t index = 0; index < count; ++index) {
                void *childClone = cloneXmlNode(rt_xml_child_at(node, index));
                if (!childClone) {
                    releaseObject(clone);
                    return nullptr;
                }
                rt_xml_append(clone, childClone);
                releaseObject(childClone);
            }
            return clone;
        }
        rt_string content = rt_xml_content(node);
        void *clone = nullptr;
        if (type == XML_NODE_TEXT)
            clone = rt_xml_text(content);
        else if (type == XML_NODE_COMMENT)
            clone = rt_xml_comment(content);
        else if (type == XML_NODE_CDATA)
            clone = rt_xml_cdata(content);
        rt_str_release_maybe(content);
        return clone;
    }

    void appendXmlProperties(void *object, void *mergedProperties) {
        void *properties = firstXmlChild(object, "properties");
        if (!properties)
            return;
        int64_t count = rt_xml_child_count(properties);
        for (int64_t index = 0; index < count; ++index) {
            void *property = rt_xml_child_at(properties, index);
            if (rt_xml_node_type(property) == XML_NODE_ELEMENT && xmlTag(property) == "property") {
                void *clone = cloneXmlNode(property);
                if (clone) {
                    rt_xml_append(mergedProperties, clone);
                    releaseObject(clone);
                }
            }
        }
    }

    bool parseXmlTemplateObject(const std::string &owner,
                                void *instance,
                                const std::string &templateReference,
                                const std::string &layerName,
                                bool layerVisible,
                                double offsetX,
                                double offsetY,
                                MapDocument &document,
                                std::string &error) {
        std::string resolved;
        std::string text;
        if (!reader_.readDependency(owner, templateReference, resolved, text, error))
            return false;
        size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || text[first] != '<') {
            error = "a TMX object requires an XML object template";
            return false;
        }
        if (!enter(resolved, error))
            return false;
        std::string normalized = normalizeTiledXmlInput(text);
        rt_string source = makeString(normalized);
        OwnedObject templateDocument(rt_xml_parse(source));
        rt_string_unref(source);
        void *root = templateDocument.get() ? rt_xml_root(templateDocument.get()) : nullptr;
        void *templateObject = root ? firstXmlChild(root, "object") : nullptr;
        if (!root || xmlTag(root) != "template" || !templateObject) {
            error = "TMX template root must be <template> containing <object>";
            leave(resolved);
            return false;
        }
        rt_string objectTag = rt_const_cstr("object");
        OwnedObject merged(rt_xml_element(objectTag));
        rt_string_unref(objectTag);
        if (!merged.get()) {
            error = "cannot allocate merged TMX object template";
            leave(resolved);
            return false;
        }
        copyXmlAttributes(templateObject, merged.get(), false);
        copyXmlAttributes(instance, merged.get(), true);

        rt_string propertiesTag = rt_const_cstr("properties");
        OwnedObject mergedProperties(rt_xml_element(propertiesTag));
        rt_string_unref(propertiesTag);
        appendXmlProperties(templateObject, mergedProperties.get());
        appendXmlProperties(instance, mergedProperties.get());
        if (rt_xml_child_count(mergedProperties.get()) > 0)
            rt_xml_append(merged.get(), mergedProperties.get());

        auto appendShapes = [&](void *object) {
            int64_t count = rt_xml_child_count(object);
            for (int64_t index = 0; index < count; ++index) {
                void *child = rt_xml_child_at(object, index);
                if (rt_xml_node_type(child) == XML_NODE_ELEMENT && xmlTag(child) != "properties") {
                    void *clone = cloneXmlNode(child);
                    if (clone) {
                        rt_xml_append(merged.get(), clone);
                        releaseObject(clone);
                    }
                }
            }
        };
        appendShapes(templateObject);
        appendShapes(instance);
        rt_string markerKey = rt_const_cstr("__zannaTemplatePath");
        rt_string markerValue = makeString(resolved);
        rt_xml_set_attr(merged.get(), markerKey, markerValue);
        rt_string_unref(markerKey);
        rt_string_unref(markerValue);
        bool ok = parseXmlObject(
            resolved, merged.get(), layerName, layerVisible, offsetX, offsetY, document, error);
        leave(resolved);
        return ok;
    }

    bool parseXmlObject(const std::string &owner,
                        void *object,
                        const std::string &layerName,
                        bool layerVisible,
                        double offsetX,
                        double offsetY,
                        MapDocument &document,
                        std::string &error) {
        std::string templateReference = xmlString(object, "template");
        if (!templateReference.empty())
            return parseXmlTemplateObject(owner,
                                          object,
                                          templateReference,
                                          layerName,
                                          layerVisible,
                                          offsetX,
                                          offsetY,
                                          document,
                                          error);
        if (document.objects.size() >= kMaxObjects) {
            error = "TMX object count exceeds 65536";
            return false;
        }
        double x = 0.0;
        double y = 0.0;
        if (!xmlDouble(object, "x", x, false, error) || !xmlDouble(object, "y", y, false, error))
            return false;
        x += offsetX;
        y += offsetY;
        if (x < static_cast<double>(INT64_MIN) || x > static_cast<double>(INT64_MAX) ||
            y < static_cast<double>(INT64_MIN) || y > static_cast<double>(INT64_MAX)) {
            error = "TMX object coordinates are outside the SceneDocument integer range";
            return false;
        }
        SceneObject result;
        result.x = static_cast<int64_t>(x);
        result.y = static_cast<int64_t>(y);
        result.type = xmlString(object, "class");
        if (result.type.empty())
            result.type = xmlString(object, "type");
        if (result.type.empty())
            result.type = "TiledObject";
        std::string authoredName = xmlString(object, "name");
        int64_t numericId = 0;
        if (!xmlInt(object, "id", numericId, false, error) || numericId < 0) {
            error = "TMX object id must be a nonnegative integer";
            return false;
        }
        if (!chooseObjectId(authoredName, numericId, result.id, error))
            return false;
        if (!parseXmlProperties(object, result.properties, error, &reader_, owner) ||
            !addReservedProperty(
                result.properties, "tiled.layer", stringScalar(layerName), error) ||
            !addReservedProperty(
                result.properties, "tiled.numericId", integerScalar(numericId), error) ||
            !addReservedProperty(
                result.properties, "tiled.layerVisible", boolScalar(layerVisible), error))
            return false;
        if (!addReservedProperty(result.properties, "tiled.sourceX", floatScalar(x), error) ||
            !addReservedProperty(result.properties, "tiled.sourceY", floatScalar(y), error))
            return false;
        std::string templatePath = xmlString(object, "__zannaTemplatePath");
        if (!templatePath.empty() &&
            !addReservedProperty(
                result.properties, "tiled.template", stringScalar(templatePath), error))
            return false;
        double width = 0.0;
        double height = 0.0;
        double rotation = 0.0;
        if (!xmlDouble(object, "width", width, false, error) ||
            !xmlDouble(object, "height", height, false, error) ||
            !xmlDouble(object, "rotation", rotation, false, error) ||
            !addReservedProperty(result.properties, "tiled.width", floatScalar(width), error) ||
            !addReservedProperty(result.properties, "tiled.height", floatScalar(height), error) ||
            !addReservedProperty(result.properties, "tiled.rotation", floatScalar(rotation), error))
            return false;
        int64_t visible = 1;
        if (!xmlInt(object, "visible", visible, false, error) ||
            !addReservedProperty(
                result.properties, "tiled.visible", boolScalar(visible != 0), error))
            return false;
        int64_t gid = 0;
        bool hasGid = false;
        std::string gidText = xmlString(object, "gid", &hasGid);
        if (hasGid) {
            if (!parseIntegerText(gidText, gid) || gid < 0 ||
                static_cast<uint64_t>(gid) > UINT32_MAX) {
                error = "TMX tile object GID is invalid";
                return false;
            }
            if ((static_cast<uint64_t>(gid) & kTiledTransformMask) != 0u) {
                error = "TMX object transform flags are not representable by SceneDocument";
                return false;
            }
            if (!addReservedProperty(result.properties, "tiled.gid", integerScalar(gid), error))
                return false;
        }
        int64_t childCount = rt_xml_child_count(object);
        for (int64_t index = 0; index < childCount; ++index) {
            void *shape = rt_xml_child_at(object, index);
            if (rt_xml_node_type(shape) != XML_NODE_ELEMENT)
                continue;
            std::string tag = xmlTag(shape);
            if (tag == "properties")
                continue;
            if (tag != "ellipse" && tag != "point" && tag != "polygon" && tag != "polyline" &&
                tag != "text") {
                error = "unsupported TMX object child <" + tag + ">";
                return false;
            }
            if (!preserveXmlShape(shape, result.properties, "tiled." + tag + "Xml", error))
                return false;
        }
        document.objects.push_back(std::move(result));
        return true;
    }

    bool parseXmlObjectLayer(const std::string &owner,
                             void *layer,
                             const std::string &name,
                             bool visible,
                             double inheritedX,
                             double inheritedY,
                             MapDocument &document,
                             std::string &error) {
        double offsetX = inheritedX;
        double offsetY = inheritedY;
        double value = 0.0;
        if (!xmlDouble(layer, "offsetx", value, false, error))
            return false;
        offsetX += value;
        value = 0.0;
        if (!xmlDouble(layer, "offsety", value, false, error))
            return false;
        offsetY += value;
        int64_t count = rt_xml_child_count(layer);
        for (int64_t index = 0; index < count; ++index) {
            void *object = rt_xml_child_at(layer, index);
            if (rt_xml_node_type(object) != XML_NODE_ELEMENT || xmlTag(object) != "object")
                continue;
            if (!parseXmlObject(owner, object, name, visible, offsetX, offsetY, document, error))
                return false;
        }
        return true;
    }

    bool parseXmlImageLayer(const std::string &owner,
                            void *layer,
                            const std::string &name,
                            bool visible,
                            double inheritedX,
                            double inheritedY,
                            MapDocument &document,
                            std::string &error) {
        void *image = firstXmlChild(layer, "image");
        std::string reference = image ? xmlString(image, "source") : std::string();
        if (!image || reference.empty()) {
            error = "TMX image layer is missing an image";
            return false;
        }
        std::string resolved;
        if (!reader_.resolveDependencyPath(owner, reference, resolved, error))
            return false;
        double x = inheritedX;
        double y = inheritedY;
        double offset = 0.0;
        if (!xmlDouble(layer, "offsetx", offset, false, error))
            return false;
        x += offset;
        offset = 0.0;
        if (!xmlDouble(layer, "offsety", offset, false, error))
            return false;
        y += offset;
        if (std::trunc(x) != x || std::trunc(y) != y) {
            error = "fractional TMX image-layer offsets are not supported";
            return false;
        }
        SceneObject object;
        object.type = "tiled.image-layer";
        object.id = name;
        object.x = static_cast<int64_t>(x);
        object.y = static_cast<int64_t>(y);
        int64_t repeatX = 0;
        int64_t repeatY = 0;
        if (!xmlInt(layer, "repeatx", repeatX, false, error) ||
            !xmlInt(layer, "repeaty", repeatY, false, error) ||
            !parseXmlProperties(layer, object.properties, error, &reader_, owner) ||
            !addReservedProperty(object.properties, "tiled.image", stringScalar(resolved), error) ||
            !addReservedProperty(object.properties, "tiled.visible", boolScalar(visible), error) ||
            !addReservedProperty(
                object.properties, "tiled.repeatX", boolScalar(repeatX != 0), error) ||
            !addReservedProperty(
                object.properties, "tiled.repeatY", boolScalar(repeatY != 0), error))
            return false;
        document.objects.push_back(std::move(object));
        return true;
    }

    bool parseXmlLayers(const std::string &owner,
                        void *parent,
                        const std::string &prefix,
                        bool inheritedVisible,
                        double inheritedX,
                        double inheritedY,
                        MapDocument &document,
                        std::string &error) {
        int64_t count = rt_xml_child_count(parent);
        for (int64_t index = 0; index < count; ++index) {
            void *layer = rt_xml_child_at(parent, index);
            if (rt_xml_node_type(layer) != XML_NODE_ELEMENT)
                continue;
            std::string tag = xmlTag(layer);
            if (tag != "layer" && tag != "objectgroup" && tag != "imagelayer" && tag != "group")
                continue;
            std::string ownName = xmlString(layer, "name");
            std::string name = prefix.empty() ? ownName : prefix + "/" + ownName;
            int64_t ownVisible = 1;
            if (!xmlInt(layer, "visible", ownVisible, false, error) ||
                !validateLayerRenderingXml(layer, error))
                return false;
            bool visible = inheritedVisible && ownVisible != 0;
            if (!preserveLayerPropertiesXml(owner, layer, name, document, error))
                return false;
            if (tag == "layer") {
                if (!parseXmlTileLayer(
                        layer, name, visible, inheritedX, inheritedY, document, error))
                    return false;
            } else if (tag == "objectgroup") {
                if (!parseXmlObjectLayer(
                        owner, layer, name, visible, inheritedX, inheritedY, document, error))
                    return false;
            } else if (tag == "imagelayer") {
                if (!parseXmlImageLayer(
                        owner, layer, name, visible, inheritedX, inheritedY, document, error))
                    return false;
            } else {
                double groupX = inheritedX;
                double groupY = inheritedY;
                double offset = 0.0;
                if (!xmlDouble(layer, "offsetx", offset, false, error))
                    return false;
                groupX += offset;
                offset = 0.0;
                if (!xmlDouble(layer, "offsety", offset, false, error))
                    return false;
                groupY += offset;
                if (!parseXmlLayers(owner, layer, name, visible, groupX, groupY, document, error))
                    return false;
            }
        }
        return true;
    }

    bool parseTmxMap(const std::string &owner,
                     const std::string &text,
                     MapDocument &document,
                     std::string &error) {
        std::string normalizedXml = normalizeTiledXmlInput(text);
        rt_string source = makeString(normalizedXml);
        OwnedObject xmlDocument(rt_xml_parse(source));
        rt_string_unref(source);
        void *root = xmlDocument.get() ? rt_xml_root(xmlDocument.get()) : nullptr;
        if (!root || xmlTag(root) != "map") {
            rt_string xmlError = rt_xml_error();
            error = "invalid TMX map";
            std::string detail = toStd(xmlError);
            rt_str_release_maybe(xmlError);
            if (!detail.empty())
                error += ": " + detail;
            return false;
        }
        if (xmlString(root, "orientation") != "orthogonal") {
            error = "only orthogonal TMX maps are supported";
            return false;
        }
        int64_t infinite = 0;
        if (!xmlInt(root, "infinite", infinite, false, error) || infinite != 0) {
            if (error.empty())
                error = "infinite TMX maps are not supported";
            return false;
        }
        if (!xmlInt(root, "width", document.width, true, error) ||
            !xmlInt(root, "height", document.height, true, error) ||
            !xmlInt(root, "tilewidth", document.tileWidth, true, error) ||
            !xmlInt(root, "tileheight", document.tileHeight, true, error) ||
            !validateMapDimensions(document, error) ||
            !parseXmlProperties(root, document.properties, error, &reader_, owner))
            return false;

        int64_t count = rt_xml_child_count(root);
        for (int64_t index = 0; index < count; ++index) {
            void *entry = rt_xml_child_at(root, index);
            if (rt_xml_node_type(entry) != XML_NODE_ELEMENT || xmlTag(entry) != "tileset")
                continue;
            int64_t firstGid = 0;
            if (!xmlInt(entry, "firstgid", firstGid, true, error) || firstGid <= 0) {
                error = "TMX tileset reference has an invalid firstgid";
                return false;
            }
            Tileset tileset;
            tileset.firstGid = firstGid;
            std::string dependency = xmlString(entry, "source");
            if (!dependency.empty()) {
                std::string resolved;
                std::string dependencyText;
                if (!reader_.readDependency(owner, dependency, resolved, dependencyText, error) ||
                    !parseTilesetText(resolved, dependencyText, tileset, error))
                    return false;
            } else if (!parseXmlTileset(owner, entry, tileset, error)) {
                return false;
            }
            document.tilesets.push_back(std::move(tileset));
        }
        if (document.tilesets.empty()) {
            error = "TMX map has no tilesets";
            return false;
        }
        std::sort(document.tilesets.begin(),
                  document.tilesets.end(),
                  [](const Tileset &left, const Tileset &right) {
                      return left.firstGid < right.firstGid;
                  });
        return parseXmlLayers(owner, root, "", true, 0.0, 0.0, document, error);
    }

    bool mergeMetadata(MapDocument &document,
                       int64_t canonicalTile,
                       const TileMetadata &metadata,
                       std::string &error) {
        if (metadata.solid)
            document.solidTiles.insert(canonicalTile);
        if (!metadata.properties.empty()) {
            auto &target = document.tileProperties[canonicalTile];
            for (const auto &[name, value] : metadata.properties) {
                auto found = target.find(name);
                if (found != target.end() && !scalarEqual(found->second, value)) {
                    error = "different Tiled tilesets assign conflicting tile property '" + name +
                            "' to local tile " + std::to_string(canonicalTile);
                    return false;
                }
                target[name] = value;
            }
        }
        if (!metadata.animationFrames.empty()) {
            if (metadata.animationDurations.size() != metadata.animationFrames.size()) {
                error = "Tiled animation frame/duration count mismatch";
                return false;
            }
            int64_t tick = 0;
            for (int64_t duration : metadata.animationDurations)
                tick = std::gcd(tick, duration);
            if (tick <= 0) {
                error = "Tiled animation has an invalid frame duration";
                return false;
            }
            TileAnimation animation;
            animation.baseTile = canonicalTile;
            animation.milliseconds = tick;
            for (size_t index = 0; index < metadata.animationFrames.size(); ++index) {
                int64_t repeats = metadata.animationDurations[index] / tick;
                if (repeats <= 0 || repeats > 8 ||
                    animation.frames.size() > 8u - static_cast<size_t>(repeats)) {
                    error = "Tiled animation cannot fit Tilemap's eight-frame runtime limit";
                    return false;
                }
                for (int64_t repeat = 0; repeat < repeats; ++repeat)
                    animation.frames.push_back(metadata.animationFrames[index]);
            }
            auto found = document.animations.find(canonicalTile);
            if (found != document.animations.end() &&
                (found->second.milliseconds != animation.milliseconds ||
                 found->second.frames != animation.frames)) {
                error = "different Tiled tilesets assign conflicting animations to local tile " +
                        std::to_string(canonicalTile);
                return false;
            }
            document.animations[canonicalTile] = std::move(animation);
        }
        return true;
    }

    bool finalize(MapDocument &document, std::string &error) {
        for (size_t index = 1; index < document.tilesets.size(); ++index) {
            if (document.tilesets[index - 1u].firstGid == document.tilesets[index].firstGid) {
                error = "Tiled tilesets have duplicate firstgid values";
                return false;
            }
        }
        if (document.layers.empty()) {
            std::vector<int64_t> empty(static_cast<size_t>(document.width * document.height), 0);
            if (!addLayer("base", true, std::move(empty), document, error))
                return false;
        }
        std::set<int64_t> usedTilesets;
        for (const Layer &layer : document.layers)
            usedTilesets.insert(layer.tileset);
        if (usedTilesets.size() > 1u) {
            for (int64_t index : usedTilesets) {
                const Tileset &tileset = document.tilesets[static_cast<size_t>(index)];
                for (const auto &[_, metadata] : tileset.metadata) {
                    if (metadata.solid || !metadata.properties.empty() ||
                        !metadata.animationFrames.empty()) {
                        error = "Tiled tile collision, properties, and animation metadata are "
                                "ambiguous across multiple used tilesets";
                        return false;
                    }
                }
            }
        }
        for (int64_t index : usedTilesets) {
            if (index < 0 || static_cast<size_t>(index) >= document.tilesets.size()) {
                error = "internal Tiled tileset selection failed";
                return false;
            }
            const Tileset &tileset = document.tilesets[static_cast<size_t>(index)];
            if (tileset.tileWidth > document.tileWidth ||
                tileset.tileHeight > document.tileHeight) {
                error = "Tiled tileset tiles cannot be larger than the map tile dimensions";
                return false;
            }
            for (const auto &[zeroBasedTile, metadata] : tileset.metadata) {
                int64_t canonicalTile = zeroBasedTile + 1;
                if (tileset.tileCount > 0 && canonicalTile > tileset.tileCount) {
                    error = "Tiled tile metadata id exceeds tileset tilecount";
                    return false;
                }
                for (int64_t frame : metadata.animationFrames) {
                    if (frame <= 0 || (tileset.tileCount > 0 && frame > tileset.tileCount)) {
                        error = "Tiled animation frame exceeds tileset tilecount";
                        return false;
                    }
                }
                if (!mergeMetadata(document, canonicalTile, metadata, error))
                    return false;
            }
        }
        return true;
    }

    SourceReader reader_;
    std::set<std::string> active_;
    std::set<std::string> objectIds_;
};

void writeScalarMap(std::ostringstream &out, const std::map<std::string, Scalar> &properties) {
    out << '{';
    bool first = true;
    for (const auto &[name, value] : properties) {
        if (!first)
            out << ',';
        first = false;
        out << jsonEscape(name) << ':' << scalarJson(value);
    }
    out << '}';
}

std::string toSceneJson(const MapDocument &document) {
    std::ostringstream out;
    out << '{';
    out << "\"version\":1";
    out << ",\"name\":" << jsonEscape(document.name);
    out << ",\"width\":" << document.width;
    out << ",\"height\":" << document.height;
    out << ",\"tileWidth\":" << document.tileWidth;
    out << ",\"tileHeight\":" << document.tileHeight;
    std::string defaultAsset;
    if (!document.layers.empty() && document.layers.front().tileset >= 0 &&
        static_cast<size_t>(document.layers.front().tileset) < document.tilesets.size())
        defaultAsset =
            document.tilesets[static_cast<size_t>(document.layers.front().tileset)].image;
    out << ",\"tilesetAsset\":" << jsonEscape(defaultAsset);
    out << ",\"properties\":";
    writeScalarMap(out, document.properties);
    out << ",\"layers\":[";
    for (size_t layerIndex = 0; layerIndex < document.layers.size(); ++layerIndex) {
        if (layerIndex > 0)
            out << ',';
        const Layer &layer = document.layers[layerIndex];
        const Tileset &tileset = document.tilesets[static_cast<size_t>(layer.tileset)];
        out << '{';
        out << "\"name\":" << jsonEscape(layer.name);
        out << ",\"visible\":" << (layer.visible ? "true" : "false");
        out << ",\"asset\":" << jsonEscape(tileset.image);
        out << ",\"tiles\":[";
        for (size_t tileIndex = 0; tileIndex < layer.tiles.size(); ++tileIndex) {
            if (tileIndex > 0)
                out << ',';
            out << layer.tiles[tileIndex];
        }
        out << "]}";
    }
    out << ']';
    out << ",\"objects\":[";
    for (size_t objectIndex = 0; objectIndex < document.objects.size(); ++objectIndex) {
        if (objectIndex > 0)
            out << ',';
        const SceneObject &object = document.objects[objectIndex];
        out << '{';
        out << "\"type\":" << jsonEscape(object.type);
        out << ",\"id\":" << jsonEscape(object.id);
        out << ",\"x\":" << object.x;
        out << ",\"y\":" << object.y;
        out << ",\"properties\":";
        writeScalarMap(out, object.properties);
        out << '}';
    }
    out << ']';
    out << ",\"collision\":{\"layer\":0,\"solid\":[";
    bool first = true;
    for (int64_t tile : document.solidTiles) {
        if (!first)
            out << ',';
        first = false;
        out << tile;
    }
    out << "]}";
    out << ",\"tileProperties\":{";
    first = true;
    for (const auto &[tile, properties] : document.tileProperties) {
        if (!first)
            out << ',';
        first = false;
        out << jsonEscape(std::to_string(tile)) << ':';
        writeScalarMap(out, properties);
    }
    out << '}';
    out << ",\"animations\":[";
    first = true;
    for (const auto &[tile, animation] : document.animations) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"baseTile\":" << tile;
        out << ",\"frameCount\":" << animation.frames.size();
        out << ",\"msPerFrame\":" << animation.milliseconds;
        out << ",\"frames\":[";
        for (size_t frame = 0; frame < animation.frames.size(); ++frame) {
            if (frame > 0)
                out << ',';
            out << animation.frames[frame];
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

ImportProduct importTiled(const std::string &path, bool assetMode) {
    ImportProduct product;
    ImportParser parser(assetMode);
    if (!parser.parse(path, product.document, product.error))
        return product;
    std::string json = toSceneJson(product.document);
    rt_string source = makeString(json);
    product.scene = rt_game_scene_load_json(source);
    rt_string_unref(source);
    if (!product.scene) {
        product.error = "failed to allocate imported SceneDocument";
        return product;
    }
    if (rt_game_scene_has_errors(product.scene)) {
        rt_string message = rt_game_scene_last_error(product.scene);
        product.error = "imported Tiled map is not a valid SceneDocument";
        std::string detail = toStd(message);
        rt_str_release_maybe(message);
        if (!detail.empty())
            product.error += ": " + detail;
        releaseObject(product.scene);
        product.scene = nullptr;
    }
    return product;
}

void *makeErrorResult(const std::string &error) {
    rt_string message = makeString(error.empty() ? "Tiled import failed" : error);
    void *result = rt_result_err_str(message);
    rt_string_unref(message);
    return result;
}

void *makeOkResult(void *value) {
    void *result = rt_result_ok(value);
    releaseObject(value);
    return result;
}

void *loadTilesetPixels(const Tileset &tileset,
                        bool assetMode,
                        int64_t mapTileWidth,
                        int64_t mapTileHeight,
                        std::string &error) {
    void *source = nullptr;
    rt_string path = makeString(tileset.image);
    if (assetMode)
        source = rt_asset_load(path);
    else
        source = rt_pixels_load(path);
    rt_string_unref(path);
    if (!source || rt_obj_class_id(source) != RT_PIXELS_CLASS_ID) {
        releaseObject(source);
        error = "cannot load Tiled tileset image as Pixels: " + tileset.image;
        return nullptr;
    }
    int64_t sourceWidth = rt_pixels_width(source);
    int64_t sourceHeight = rt_pixels_height(source);
    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        (tileset.imageWidth > 0 && tileset.imageWidth != sourceWidth) ||
        (tileset.imageHeight > 0 && tileset.imageHeight != sourceHeight)) {
        releaseObject(source);
        error = "Tiled tileset image dimensions do not match its metadata: " + tileset.image;
        return nullptr;
    }
    if (tileset.tileWidth > mapTileWidth || tileset.tileHeight > mapTileHeight) {
        releaseObject(source);
        error = "Tiled tileset tiles are larger than the map tile dimensions";
        return nullptr;
    }
    if (sourceWidth < tileset.margin * 2 + tileset.tileWidth ||
        sourceHeight < tileset.margin * 2 + tileset.tileHeight) {
        releaseObject(source);
        error = "Tiled tileset image is too small for its margin and tile dimensions";
        return nullptr;
    }
    int64_t derivedColumns = (sourceWidth - tileset.margin * 2 + tileset.spacing) /
                             (tileset.tileWidth + tileset.spacing);
    int64_t derivedRows = (sourceHeight - tileset.margin * 2 + tileset.spacing) /
                          (tileset.tileHeight + tileset.spacing);
    int64_t columns = tileset.columns > 0 ? tileset.columns : derivedColumns;
    if (columns <= 0 || columns > derivedColumns || derivedRows <= 0) {
        releaseObject(source);
        error = "Tiled tileset columns do not fit its image";
        return nullptr;
    }
    int64_t tileCount = tileset.tileCount > 0 ? tileset.tileCount : columns * derivedRows;
    if (tileCount <= 0 || tileCount > columns * derivedRows) {
        releaseObject(source);
        error = "Tiled tileset tilecount does not fit its image";
        return nullptr;
    }
    int64_t rows = (tileCount + columns - 1) / columns;
    if (columns > INT64_MAX / mapTileWidth || rows > INT64_MAX / mapTileHeight) {
        releaseObject(source);
        error = "Tiled repacked tileset dimensions overflow";
        return nullptr;
    }
    void *packed = rt_pixels_new(columns * mapTileWidth, rows * mapTileHeight);
    if (!packed) {
        releaseObject(source);
        error = "cannot allocate repacked Tiled tileset Pixels";
        return nullptr;
    }
    for (int64_t tile = 0; tile < tileCount; ++tile) {
        int64_t sourceX = tileset.margin + (tile % columns) * (tileset.tileWidth + tileset.spacing);
        int64_t sourceY =
            tileset.margin + (tile / columns) * (tileset.tileHeight + tileset.spacing);
        int64_t destinationX = (tile % columns) * mapTileWidth;
        int64_t destinationY = (tile / columns) * mapTileHeight;
        rt_pixels_copy(packed,
                       destinationX,
                       destinationY,
                       source,
                       sourceX,
                       sourceY,
                       tileset.tileWidth,
                       tileset.tileHeight);
    }
    releaseObject(source);
    return packed;
}

void *buildRenderTilemap(ImportProduct &product, bool assetMode, std::string &error) {
    if (!product.scene) {
        error =
            product.error.empty() ? "Tiled import did not produce a SceneDocument" : product.error;
        return nullptr;
    }
    void *tilemap = rt_game_scene_build_tilemap(product.scene);
    if (!tilemap) {
        error = "cannot build Tilemap from imported SceneDocument";
        return nullptr;
    }
    std::map<int64_t, void *> pixelsByTileset;
    for (size_t layerIndex = 0; layerIndex < product.document.layers.size(); ++layerIndex) {
        int64_t tilesetIndex = product.document.layers[layerIndex].tileset;
        void *pixels = nullptr;
        auto found = pixelsByTileset.find(tilesetIndex);
        if (found != pixelsByTileset.end()) {
            pixels = found->second;
        } else {
            pixels = loadTilesetPixels(product.document.tilesets[static_cast<size_t>(tilesetIndex)],
                                       assetMode,
                                       product.document.tileWidth,
                                       product.document.tileHeight,
                                       error);
            if (!pixels) {
                for (const auto &[_, value] : pixelsByTileset)
                    releaseObject(value);
                releaseObject(tilemap);
                return nullptr;
            }
            pixelsByTileset[tilesetIndex] = pixels;
        }
        if (layerIndex == 0)
            rt_tilemap_set_tileset(tilemap, pixels);
        else
            rt_tilemap_set_layer_tileset(tilemap, static_cast<int64_t>(layerIndex), pixels);
    }
    for (const auto &[_, value] : pixelsByTileset)
        releaseObject(value);
    return tilemap;
}

void *sceneImportRaw(rt_string path, bool assetMode) {
    ImportProduct product = importTiled(toStd(path), assetMode);
    return product.scene;
}

void *sceneImportResult(rt_string path, bool assetMode) {
    ImportProduct product = importTiled(toStd(path), assetMode);
    if (!product.scene)
        return makeErrorResult(product.error);
    return makeOkResult(product.scene);
}

void *loaderImportRaw(void *loader, rt_string path, bool assetMode) {
    if (!loader || rt_obj_class_id(loader) != kTiledMapLoaderClassId)
        return nullptr;
    ImportProduct product = importTiled(toStd(path), assetMode);
    if (!product.scene)
        return nullptr;
    std::string error;
    void *tilemap = buildRenderTilemap(product, assetMode, error);
    releaseObject(product.scene);
    product.scene = nullptr;
    return tilemap;
}

void *loaderImportResult(void *loader, rt_string path, bool assetMode) {
    if (!loader || rt_obj_class_id(loader) != kTiledMapLoaderClassId)
        return makeErrorResult("TiledMapLoader.Load requires a TiledMapLoader receiver");
    ImportProduct product = importTiled(toStd(path), assetMode);
    if (!product.scene)
        return makeErrorResult(product.error);
    std::string error;
    void *tilemap = buildRenderTilemap(product, assetMode, error);
    releaseObject(product.scene);
    product.scene = nullptr;
    if (!tilemap)
        return makeErrorResult(error);
    return makeOkResult(tilemap);
}

} // namespace

extern "C" {

void *rt_game_scene_import_tiled(rt_string path) {
    try {
        return sceneImportRaw(path, false);
    } catch (...) {
        return nullptr;
    }
}

void *rt_game_scene_import_tiled_result(rt_string path) {
    try {
        return sceneImportResult(path, false);
    } catch (...) {
        return makeErrorResult("Tiled filesystem import failed unexpectedly");
    }
}

void *rt_game_scene_import_tiled_asset(rt_string path) {
    try {
        return sceneImportRaw(path, true);
    } catch (...) {
        return nullptr;
    }
}

void *rt_game_scene_import_tiled_asset_result(rt_string path) {
    try {
        return sceneImportResult(path, true);
    } catch (...) {
        return makeErrorResult("Tiled asset import failed unexpectedly");
    }
}

void *rt_tiledmaploader_load(void *loader, rt_string path) {
    try {
        return loaderImportRaw(loader, path, false);
    } catch (...) {
        return nullptr;
    }
}

void *rt_tiledmaploader_load_result(void *loader, rt_string path) {
    try {
        return loaderImportResult(loader, path, false);
    } catch (...) {
        return makeErrorResult("TiledMapLoader filesystem load failed unexpectedly");
    }
}

void *rt_tiledmaploader_load_asset(void *loader, rt_string path) {
    try {
        return loaderImportRaw(loader, path, true);
    } catch (...) {
        return nullptr;
    }
}

void *rt_tiledmaploader_load_asset_result(void *loader, rt_string path) {
    try {
        return loaderImportResult(loader, path, true);
    } catch (...) {
        return makeErrorResult("TiledMapLoader asset load failed unexpectedly");
    }
}

} // extern "C"
