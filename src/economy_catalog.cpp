#include "economy_catalog.hpp"
#include "weapon_catalog.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace awareness::cosmetics {
namespace {
constexpr std::size_t MaxBytes = 64 * 1024 * 1024, MaxNodes = 500000;
struct Node {
    std::string key, value;
    std::vector<Node> children;
    const Node *Find(std::string_view searchKey) const noexcept {
        for (const auto &n : children)
            if (n.key == searchKey)
                return &n;
        return nullptr;
    }
    std::string_view Value(std::string_view searchKey) const noexcept {
        const auto p = Find(searchKey);
        return p ? std::string_view(p->value) : std::string_view{};
    }
};
void MergeNode(Node &target, const Node &source) {
    if (source.children.empty()) {
        target.value = source.value;
        return;
    }
    std::unordered_map<std::string, std::size_t> indices;
    for (std::size_t i = 0; i < target.children.size(); ++i)
        indices[target.children[i].key] = i;
    for (const auto &child : source.children) {
        const auto found = indices.find(child.key);
        if (found == indices.end()) {
            indices.emplace(child.key, target.children.size());
            target.children.push_back(child);
        } else
            MergeNode(target.children[found->second], child);
    }
}
Node MergedSection(const Node &root, std::string_view name) {
    Node out;
    for (const auto &child : root.children)
        if (child.key == name)
            MergeNode(out, child);
    return out;
}
class Parser {
    std::string_view text_;
    std::size_t pos_{}, nodes_{};
    bool quoted_{};
    std::string Token() {
        quoted_ = false;
        for (;;) {
            while (pos_ < text_.size() && static_cast<unsigned char>(text_[pos_]) <= 32)
                ++pos_;
            if (text_.substr(pos_, 2) != "//")
                break;
            const auto end = text_.find('\n', pos_);
            pos_ = end == text_.npos ? text_.size() : end + 1;
        }
        if (pos_ == text_.size())
            return {};
        const char c = text_[pos_++];
        if (c == '{' || c == '}')
            return std::string(1, c);
        std::string token;
        if (c == '"') {
            quoted_ = true;
            while (pos_ < text_.size()) {
                const auto ch = text_[pos_++];
                if (ch == '"')
                    return token;
                if (ch == '\\' && pos_ < text_.size() && (text_[pos_] == '"' || text_[pos_] == '\\'))
                    token += text_[pos_++];
                else
                    token += ch;
                if (token.size() > 32768)
                    throw std::runtime_error("Catalog token is too long.");
            }
            throw std::runtime_error("Unterminated catalog string.");
        }
        token += c;
        while (pos_ < text_.size() && static_cast<unsigned char>(text_[pos_]) > 32 && text_[pos_] != '{' &&
               text_[pos_] != '}')
            token += text_[pos_++];
        return token;
    }
    void Children(Node &parent, unsigned depth) {
        if (depth > 32)
            throw std::runtime_error("Catalog nesting limit exceeded.");
        for (;;) {
            auto key = Token();
            if (key.empty()) {
                if (depth)
                    throw std::runtime_error("Truncated catalog object.");
                return;
            }
            if (key == "}") {
                if (!depth)
                    throw std::runtime_error("Unexpected catalog brace.");
                return;
            }
            if (!quoted_ && key.front() == '[' && key.back() == ']')
                continue; // Valve platform condition after a value.
            if (++nodes_ > MaxNodes || key == "{")
                throw std::runtime_error("Invalid catalog object at byte " + std::to_string(pos_) + " (nodes " +
                                         std::to_string(nodes_) + ").");
            Node n;
            n.key = std::move(key);
            auto value = Token();
            if (value == "{")
                Children(n, depth + 1);
            else if (value == "}")
                throw std::runtime_error("Missing catalog value.");
            else
                n.value = std::move(value);
            parent.children.push_back(std::move(n));
        }
    }

  public:
    explicit Parser(std::string_view s) : text_(s) {}
    Node Parse() {
        if (text_.size() > MaxBytes)
            throw std::runtime_error("Catalog size limit exceeded.");
        Node n;
        Children(n, 0);
        return n;
    }
};
std::string Lower(std::string_view value) {
    std::string out(value);
    for (auto &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    return out;
}
unsigned Number(std::string_view s, unsigned fallback = 0) {
    unsigned value{};
    const auto r = std::from_chars(s.data(), s.data() + s.size(), value);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size() ? value : fallback;
}
float Decimal(std::string_view s, float fallback) {
    float value{};
    const auto r = std::from_chars(s.data(), s.data() + s.size(), value);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size() && std::isfinite(value) ? value : fallback;
}
std::string Utf8(std::string_view text) {
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0xff &&
        static_cast<unsigned char>(text[1]) == 0xfe) {
        if (text.size() % 2)
            throw std::runtime_error("Malformed localization encoding.");
        std::wstring wide((text.size() - 2) / 2, L'\0');
        std::memcpy(wide.data(), text.data() + 2, text.size() - 2);
        const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                               static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (count <= 0)
            throw std::runtime_error("Invalid localization text.");
        std::string out(count, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), out.data(),
                            count, nullptr, nullptr);
        return out;
    }
    if (text.substr(0, 3) == "\xef\xbb\xbf")
        text.remove_prefix(3);
    return std::string(text);
}
std::string Pretty(std::string_view name) {
    if (name.starts_with("weapon_"))
        name.remove_prefix(7);
    std::string out(name);
    bool upper = true;
    for (auto &c : out) {
        if (c == '_') {
            c = ' ';
            upper = true;
        } else if (upper) {
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 32);
            upper = false;
        }
    }
    return out;
}
void Resolve(const Node &n, const Node &prefabs, std::map<std::string, const Node *> &fields, unsigned depth = 0) {
    if (depth > 12)
        throw std::runtime_error("Cyclic item prefab.");
    auto names = n.Value("prefab");
    while (!names.empty()) {
        const auto end = names.find(' ');
        const auto token = names.substr(0, end);
        if (const auto p = prefabs.Find(token))
            Resolve(*p, prefabs, fields, depth + 1);
        if (end == names.npos)
            break;
        names.remove_prefix(end + 1);
    }
    for (const auto &child : n.children)
        fields[child.key] = &child;
}
void SortPairs(Catalog &c) {
    std::sort(c.pairs.begin(), c.pairs.end(), [](const auto &a, const auto &b) {
        return a.definition != b.definition ? a.definition < b.definition : a.paintKit < b.paintKit;
    });
    c.pairs.erase(std::unique(c.pairs.begin(), c.pairs.end(),
                              [](const auto &a, const auto &b) {
                                  return a.definition == b.definition && a.paintKit == b.paintKit;
                              }),
                  c.pairs.end());
}
void CollectPairs(const Node &n, const std::unordered_map<std::string, std::uint16_t> &items,
                  const std::unordered_map<std::string, std::uint32_t> &paints, Catalog &out) {
    if (n.key.starts_with('[')) {
        const auto end = n.key.find(']');
        if (end != n.key.npos) {
            const auto p = paints.find(n.key.substr(1, end - 1));
            const auto d = items.find(n.key.substr(end + 1));
            if (p != paints.end() && d != items.end())
                out.pairs.push_back({d->second, p->second});
        }
    }
    for (const auto &child : n.children)
        CollectPairs(child, items, paints, out);
}
struct VpkEntry {
    std::uint16_t archive{};
    std::uint32_t offset{}, length{};
    std::string preload;
};
class Vpk {
    std::filesystem::path directory_;
    std::uint64_t dataStart_{};
    std::map<std::string, VpkEntry> needed_;

  public:
    std::vector<std::string> generated;
    explicit Vpk(const std::filesystem::path &directory) : directory_(directory) {
        std::ifstream file(directory_ / L"pak01_dir.vpk", std::ios::binary);
        std::array<std::uint32_t, 3> h{};
        if (!file.read(reinterpret_cast<char *>(h.data()), sizeof(h)) || h[0] != 0x55aa1234 ||
            (h[1] != 1 && h[1] != 2) || h[2] > MaxBytes)
            throw std::runtime_error("The local game archive is unavailable or invalid.");
        const auto header = h[1] == 2 ? 28 : 12;
        dataStart_ = static_cast<std::uint64_t>(header) + h[2];
        file.seekg(header);
        std::string tree(h[2], '\0');
        if (!file.read(tree.data(), tree.size()))
            throw std::runtime_error("Truncated archive directory.");
        std::size_t pos{};
        auto string = [&]() {
            const auto end = tree.find('\0', pos);
            if (end == tree.npos || end - pos > 1024)
                throw std::runtime_error("Invalid archive path.");
            auto out = tree.substr(pos, end - pos);
            pos = end + 1;
            return out;
        };
        auto u16 = [&](std::size_t at) {
            std::uint16_t n{};
            std::memcpy(&n, tree.data() + at, 2);
            return n;
        };
        auto u32 = [&](std::size_t at) {
            std::uint32_t n{};
            std::memcpy(&n, tree.data() + at, 4);
            return n;
        };
        for (auto ext = string(); !ext.empty(); ext = string())
            for (auto dir = string(); !dir.empty(); dir = string())
                for (auto name = string(); !name.empty(); name = string()) {
                    if (pos + 18 > tree.size())
                        throw std::runtime_error("Truncated archive entry.");
                    const auto preload = u16(pos + 4);
                    VpkEntry entry{u16(pos + 6), u32(pos + 8), u32(pos + 12), {}};
                    if (u16(pos + 16) != 0xffff || pos + 18 + preload > tree.size())
                        throw std::runtime_error("Invalid archive entry.");
                    pos += 18;
                    entry.preload = tree.substr(pos, preload);
                    pos += preload;
                    const auto path = (dir == " " ? "" : dir + "/") + name + "." + ext;
                    if (path == "scripts/items/items_game.txt" || path == "resource/csgo_english.txt")
                        needed_.emplace(path, std::move(entry));
                    if (dir == "panorama/images/econ/default_generated" && generated.size() < 50000)
                        generated.push_back(std::move(name));
                }
    }
    std::string Read(const std::string &name) const {
        const auto it = needed_.find(name);
        if (it == needed_.end())
            throw std::runtime_error("The game catalog is missing from its archive.");
        const auto &e = it->second;
        if (e.length > MaxBytes || e.length + e.preload.size() > MaxBytes)
            throw std::runtime_error("Archive file is too large.");
        std::filesystem::path path = directory_ / L"pak01_dir.vpk";
        std::uint64_t offset = e.offset;
        if (e.archive == 0x7fff)
            offset += dataStart_;
        else {
            char archive[32]{};
            std::snprintf(archive, sizeof(archive), "pak01_%03u.vpk", e.archive);
            path = directory_ / archive;
        }
        std::ifstream file(path, std::ios::binary);
        file.seekg(static_cast<std::streamoff>(offset));
        std::string out = e.preload;
        out.resize(e.preload.size() + e.length);
        if (!file.read(out.data() + e.preload.size(), e.length))
            throw std::runtime_error("The game catalog archive is incomplete.");
        return out;
    }
};
} // namespace
const Definition *Catalog::Find(std::uint32_t id) const noexcept {
    const auto it =
        std::lower_bound(definitions.begin(), definitions.end(), id, [](const auto &a, auto b) { return a.id < b; });
    return it != definitions.end() && it->id == id ? &*it : nullptr;
}
const PaintKit *Catalog::Paint(std::uint32_t id) const noexcept {
    const auto it = std::lower_bound(paints.begin(), paints.end(), id, [](const auto &a, auto b) { return a.id < b; });
    return it != paints.end() && it->id == id ? &*it : nullptr;
}
bool Catalog::Supports(std::uint32_t definition, std::uint32_t paint) const noexcept {
    if (!Find(definition))
        return false;
    if (!paint)
        return true;
    const auto it = std::lower_bound(
        pairs.begin(), pairs.end(), FinishPair{static_cast<std::uint16_t>(definition), paint},
        [](const auto &a, const auto &b) {
            return a.definition != b.definition ? a.definition < b.definition : a.paintKit < b.paintKit;
        });
    return it != pairs.end() && it->definition == definition && it->paintKit == paint;
}
bool ParseCatalog(std::string_view itemsGame, std::string_view english, Catalog &out, std::string &error) {
    try {
        const auto parsed = Parser(itemsGame).Parse();
        const auto root = parsed.Find("items_game");
        if (!root || !root->Find("items") || !root->Find("paint_kits"))
            throw std::runtime_error("Missing game catalog sections.");
        const Node empty;
        const auto prefabs = MergedSection(*root, "prefabs");
        const auto itemsSection = MergedSection(*root, "items");
        const auto paintsSection = MergedSection(*root, "paint_kits");
        std::unordered_map<std::string, std::string> localized;
        if (!english.empty()) {
            const auto utf = Utf8(english);
            const auto lang = Parser(utf).Parse();
            if (const auto l = lang.Find("lang"))
                if (const auto tokens = l->Find("Tokens"))
                    for (const auto &t : tokens->children)
                        localized[Lower(t.key)] = t.value;
        }
        auto label = [&](std::string_view token, std::string_view fallback) {
            if (token.starts_with('#'))
                token.remove_prefix(1);
            const auto found = localized.find(Lower(token));
            return found != localized.end() ? found->second : Pretty(fallback);
        };
        Catalog next;
        for (const auto &item : itemsSection.children) {
            const auto id = Number(item.key);
            if (!id || id > 65535)
                continue;
            std::map<std::string, const Node *> f;
            Resolve(item, prefabs, f);
            auto value = [&](const char *key) -> std::string_view {
                const auto it = f.find(key);
                return it == f.end() ? std::string_view{} : it->second->value;
            };
            Definition d;
            d.id = static_cast<std::uint16_t>(id);
            d.internalName = value("name");
            d.model = value("model_player");
            const auto type = value("item_class");
            const auto category = value("flexible_loadout_category");
            if (FindWeaponIcon(id))
                d.kind = ItemKind::Weapon;
            else if (type == "weapon_knife" && (id == 42 || id == 59 || (id >= 500 && id <= 526)))
                d.kind = ItemKind::Knife;
            else if (category == "hands" || value("flexible_loadout_slot") == "clothing_hands")
                d.kind = ItemKind::Glove;
            else if (type == "customplayer" || category == "customplayer")
                d.kind = ItemKind::Agent;
            else
                continue;
            if (d.model.empty() || d.model.size() > 240 || d.model.find("..") != d.model.npos ||
                d.model.find(':') != d.model.npos)
                continue;
            d.name = label(value("item_name"), d.internalName);
            if (const auto used = f.find("used_by_classes"); used != f.end()) {
                const bool t = Number(used->second->Value("terrorists")) != 0,
                           ct = Number(used->second->Value("counter-terrorists")) != 0;
                d.team = t && !ct ? 2 : ct && !t ? 3 : 0;
            }
            next.definitions.push_back(std::move(d));
        }
        for (const auto &paint : paintsSection.children) {
            const auto id = Number(paint.key, 100001);
            if (id > 100000)
                continue;
            PaintKit p;
            p.id = id;
            p.internalName = paint.Value("name");
            if (p.internalName.empty())
                continue;
            p.name = label(paint.Value("description_tag"), p.internalName);
            p.legacy = Number(paint.Value("use_legacy_model")) != 0;
            p.minWear = std::clamp(Decimal(paint.Value("wear_remap_min"), 0), 0.f, 1.f);
            p.maxWear = std::clamp(Decimal(paint.Value("wear_remap_max"), 1), p.minWear, 1.f);
            next.paints.push_back(std::move(p));
        }
        std::sort(next.definitions.begin(), next.definitions.end(),
                  [](const auto &a, const auto &b) { return a.id < b.id; });
        std::sort(next.paints.begin(), next.paints.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        std::unordered_map<std::string, std::uint16_t> items;
        std::unordered_map<std::string, std::uint32_t> paints;
        for (const auto &d : next.definitions)
            items[d.internalName] = d.id;
        for (const auto &p : next.paints)
            paints[p.internalName] = p.id;
        CollectPairs(*root, items, paints, next);
        SortPairs(next);
        if (next.definitions.empty() || next.paints.empty())
            throw std::runtime_error("No usable item definitions found.");
        out = std::move(next);
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
bool LoadCatalog(const std::filesystem::path &directory, Catalog &out, std::string &error) {
    try {
        Vpk vpk(directory);
        Catalog next;
        if (!ParseCatalog(vpk.Read("scripts/items/items_game.txt"), vpk.Read("resource/csgo_english.txt"), next, error))
            return false;
        std::unordered_map<std::string, std::uint32_t> paints;
        for (const auto &p : next.paints)
            paints[p.internalName] = p.id;
        for (const auto &image : vpk.generated)
            for (const auto &d : next.definitions) {
                const auto prefix = d.internalName + "_";
                if (!image.starts_with(prefix))
                    continue;
                auto skin = std::string_view(image).substr(prefix.size());
                const auto end = skin.rfind("_light");
                if (end == skin.npos)
                    continue;
                skin = skin.substr(0, end);
                if (const auto p = paints.find(std::string(skin)); p != paints.end())
                    next.pairs.push_back({d.id, p->second});
            }
        SortPairs(next);
        out = std::move(next);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
CatalogController::~CatalogController() {
    Stop();
}
bool CatalogController::Start(const std::filesystem::path &directory) {
    if (busy_.exchange(true))
        return false;
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard lock(mutex_);
        catalog_.reset();
        error_.clear();
    }
    try {
        worker_ = std::thread([this, directory] {
            std::string error;
            auto result = std::make_shared<Catalog>();
            const bool ok = LoadCatalog(directory, *result, error);
            {
                std::lock_guard lock(mutex_);
                if (ok)
                    catalog_ = std::move(result);
                error_ = std::move(error);
            }
            busy_.store(false, std::memory_order_release);
        });
        return true;
    } catch (...) {
        busy_.store(false);
        std::lock_guard lock(mutex_);
        error_ = "Could not start catalog discovery.";
        return false;
    }
}
void CatalogController::Stop() {
    if (worker_.joinable())
        worker_.join();
}
std::shared_ptr<const Catalog> CatalogController::Snapshot() const {
    std::lock_guard lock(mutex_);
    return catalog_;
}
std::string CatalogController::Error() const {
    std::lock_guard lock(mutex_);
    return error_;
}
} // namespace awareness::cosmetics
