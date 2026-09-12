#pragma once
#include <imgui.h>
#include <algorithm>
#include <string>
#include <string_view>
namespace vortex::brand {
inline void Mark(ImDrawList *draw, ImVec2 center, float size, ImU32 color, ImU32 accent) {
    // The two facets share one 32-unit grid; this is also the installer/app mark.
    const auto at = [&](float x, float y) {
        return ImVec2{center.x + (x - 16) * size / 32.f, center.y + (y - 16) * size / 32.f};
    };
    const ImVec2 left[]{at(3, 5), at(10, 5), at(16, 19), at(12.5f, 27)};
    const ImVec2 right[]{at(22, 5), at(29, 5), at(18, 27), at(12.5f, 27)};
    draw->AddConvexPolyFilled(left, 4, color);
    draw->AddConvexPolyFilled(right, 4, accent);
}
inline std::string FitText(ImFont *font, float size, std::string_view text, float width) {
    if (width <= 0)
        return {};
    auto count = std::min<std::size_t>(text.size(), 256);
    while (count < text.size() && count && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80)
        --count;
    std::string result(text.substr(0, count));
    for (char &c : result)
        if (static_cast<unsigned char>(c) < 32)
            c = ' ';
    if (font->CalcTextSizeA(size, FLT_MAX, 0, result.c_str()).x <= width)
        return result;
    const float dots = font->CalcTextSizeA(size, FLT_MAX, 0, "...").x;
    if (dots > width)
        return {};
    while (!result.empty() && font->CalcTextSizeA(size, FLT_MAX, 0, result.c_str()).x + dots > width) {
        auto n = result.size() - 1;
        while (n && (static_cast<unsigned char>(result[n]) & 0xc0) == 0x80)
            --n;
        result.resize(n);
    }
    return result + "...";
}
inline void Avatar(ImDrawList *draw, ImTextureRef image, ImVec2 at, float size, bool available,
                   ImU32 accent = IM_COL32(166, 144, 244, 255)) {
    if (available)
        draw->AddImageRounded(image, at, {at.x + size, at.y + size}, {0, 0}, {1, 1}, IM_COL32_WHITE, size * .5f);
    else {
        draw->AddCircleFilled({at.x + size * .5f, at.y + size * .5f}, size * .5f, IM_COL32(43, 43, 52, 255), 32);
        Mark(draw, {at.x + size * .5f, at.y + size * .5f}, size * .57f, IM_COL32(245, 245, 250, 255), accent);
    }
}
} // namespace vortex::brand
