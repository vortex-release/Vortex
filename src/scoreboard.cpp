#include "scoreboard.hpp"
#include "scoreboard_script.hpp"
#include <algorithm>
namespace awareness::scoreboard {
std::string Script(const Frame &frame, const Options &options, bool clear) {
    std::string s(ScriptPrefix);
    s.reserve(24000);
    s += '[';
    bool firstPlayer = true;
    if (!clear && Valid(options))
        for (std::uint32_t n = 0; n < std::min<std::size_t>(frame.count, frame.players.size()); ++n) {
            const auto &p = frame.players[n];
            if (!p.controller || p.controller > 64)
                continue;
            if (!firstPlayer)
                s += ',';
            firstPlayer = false;
            s += "[\"" + std::to_string(p.steamId) + "\"," + std::to_string(p.controller) + ",[";
            bool firstItem = true;
            auto add = [&](const char *name, bool active, bool compact) {
                if (!firstItem)
                    s += ',';
                firstItem = false;
                s += "[\"";
                s += name;
                s += "\",";
                s += active ? "true" : "false";
                s += ',';
                s += compact ? "true" : "false";
                s += ']';
            };
            for (std::uint32_t i = 0; i < std::min<std::size_t>(p.count, p.items.size()); ++i) {
                const auto &item = p.items[i];
                if (item.definition == 49 ? !options.objective : !options.weapons)
                    continue;
                if (const auto icon = Equipment(item.definition))
                    add(icon, item.active, item.definition >= 43 && item.definition <= 49);
            }
            if (options.armor && p.armor)
                add(p.helmet ? "armor_helmet" : "armor", true, true);
            if (options.objective && p.defuser)
                add("defuser", true, true);
            s += "]," + std::to_string(p.handle) + "]";
        }
    s += "];var scale=" + std::to_string(Valid(options) ? options.scale : 1.f);
    s += ScriptBody;
    return s;
}
} // namespace awareness::scoreboard
