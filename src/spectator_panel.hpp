#pragma once
#include "settings.hpp"
#include "spectator_reader.hpp"
#include "steam_profile.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace awareness {
inline ImDrawList *DrawSpectatorPanel(const cs2::SpectatorFrame *frame, VisualOptions &v, SteamProfile &profiles,
                                      ID3D11Device *device, bool menuOpen, bool &edited) {
    const unsigned count = frame ? std::min<unsigned>(frame->count, MaxEntities) : 0;
    if (!v.spectators || (!count && !menuOpen))
        return nullptr;
    const auto display = ImGui::GetIO().DisplaySize;
    const float s = v.spectatorScale, width = std::min(300.f * s, display.x - 16);
    const float availableX = std::max(1.f, display.x - width - 16);
    const float x = 8 + v.spectatorX * availableX;
    const float top = v.sessionBadge && x + width > display.x - 410 * v.badgeScale ? 60 * v.badgeScale : 8;
    const float height =
        std::min((58.f + std::max(1u, std::min(count, 8u)) * 44.f) * s, std::max(50.f, display.y - top - 8));
    const float availableY = std::max(1.f, display.y - height - 16);
    ImGui::SetNextWindowPos({x, std::max(top, 8 + v.spectatorY * availableY)}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(v.spectatorOpacity);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16 * s, 12 * s});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(19, 20, 24, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(52, 53, 61, 190));
    auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoFocusOnAppearing;
    if (!menuOpen)
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Spectators##floating", nullptr, flags);
    auto *draw = ImGui::GetWindowDrawList();
    auto *font = ImGui::GetFont();
    const float textSize = 15 * s;
    auto at = ImGui::GetCursorScreenPos();
    char heading[64]{};
    std::snprintf(heading, sizeof(heading), "%s  %u", frame && frame->observing ? "Watching this player" : "Spectators",
                  count);
    draw->AddText(font, textSize, at, IM_COL32(240, 241, 245, 255), heading);
    ImGui::InvisibleButton("Move spectators", {width - 32 * s, 28 * s});
    if (menuOpen && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const auto delta = ImGui::GetIO().MouseDelta;
        v.spectatorX = std::clamp(v.spectatorX + delta.x / availableX, 0.f, 1.f);
        v.spectatorY = std::clamp(v.spectatorY + delta.y / availableY, 0.f, 1.f);
        edited = true;
    }
    draw->AddLine({at.x, at.y + 29 * s}, {at.x + width - 32 * s, at.y + 29 * s}, IM_COL32(64, 64, 74, 150));
    ImGui::Dummy({0, 10 * s});
    if (!count) {
        draw->AddText(font, 13 * s, ImGui::GetCursorScreenPos(), IM_COL32(150, 152, 164, 255), "Nobody is watching");
        ImGui::Dummy({0, 36 * s});
    }
    const auto now = GetTickCount64();
    for (unsigned i = 0; i < count; ++i) {
        const auto &entry = frame->entries[i];
        at = ImGui::GetCursorScreenPos();
        const ImVec2 end{at.x + 34 * s, at.y + 34 * s};
        auto *avatar = profiles.AvatarFor(device, entry.steamId, now);
        if (avatar)
            draw->AddImageRounded(ImTextureRef(reinterpret_cast<ImTextureID>(avatar)), at, end, {0, 0}, {1, 1},
                                  IM_COL32_WHITE, 7 * s);
        else {
            const auto accent = ImGui::ColorConvertFloat4ToU32({v.accent.r, v.accent.g, v.accent.b, .18f});
            draw->AddRectFilled(at, end, accent, 7 * s);
            char initial[2]{entry.name[0] > 32 && static_cast<unsigned char>(entry.name[0]) < 127 ? entry.name[0] : '?',
                            0};
            const auto size = font->CalcTextSizeA(textSize, FLT_MAX, 0, initial);
            draw->AddText(font, textSize, {at.x + (34 * s - size.x) * .5f, at.y + (34 * s - size.y) * .5f},
                          IM_COL32(230, 230, 238, 255), initial);
        }
        const ImVec2 text{at.x + 46 * s, at.y + 1 * s};
        char label[72]{};
        std::snprintf(label, sizeof(label), "%s", entry.name);
        const float maximum = width - 82 * s;
        std::size_t length = std::strlen(label);
        bool clipped{};
        while (length && font->CalcTextSizeA(textSize, FLT_MAX, 0, label).x > maximum) {
            do {
                --length;
            } while (length && (static_cast<unsigned char>(label[length]) & 0xc0) == 0x80);
            label[length] = 0;
            clipped = true;
        }
        if (clipped && length > 3) {
            // Reserve the final character positions for an ellipsis.
            for (int n = 0; n < 3 && length; ++n) {
                do {
                    --length;
                } while (length && (static_cast<unsigned char>(label[length]) & 0xc0) == 0x80);
            }
            std::memcpy(label + length, "...", 4);
        }
        draw->AddText(font, textSize, text, IM_COL32(231, 232, 239, 255), label);
        draw->AddText(font, 11 * s, {text.x, at.y + 21 * s}, IM_COL32(150, 152, 164, 255),
                      entry.mode == 3 ? "Chase camera" : "First person");
        ImGui::Dummy({width - 32 * s, 44 * s});
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return draw;
}
} // namespace awareness
