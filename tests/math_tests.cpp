#if defined(_WIN32)
#include <Windows.h> // Public headers must also tolerate Windows min/max macros.
#endif
#include <awareness/EntitySource.hpp>
#include <awareness/HudLayout.hpp>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace awareness;
namespace {
int failures{}, checks{};
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
bool Near(float a, float b) { return std::abs(a - b) < .01f; }
}
int main() {
    const Viewport v{0,0,1920,1080};
    const auto identity = Matrix4x4::Identity();
    Vector2 s{99,99};
    Check(WorldToScreen({0,0,.5f}, identity, v, s) && Near(s.x,960) && Near(s.y,540), "center projection");
    Check(WorldToScreen({-1,1,0}, identity, v, s) && Near(s.x,0) && Near(s.y,0), "upper-left and near plane");
    Check(WorldToScreen({1,-1,1}, identity, v, s) && Near(s.x,1920) && Near(s.y,1080), "lower-right and far plane");
    Check(!WorldToScreen({0,0,-.1f}, identity, v, s), "before near plane rejected");
    Check(!WorldToScreen({0,0,1.1f}, identity, v, s), "beyond far plane rejected");
    Check(WorldToScreen({2,0,.5f}, identity, v, s) && s.x > v.width, "off-screen projection retained for clipping");
    Check(WorldToScreen({0,0,.5f}, identity, {100,50,800,600}, s) && Near(s.x,500) && Near(s.y,350), "viewport offset");
    Check(!WorldToScreen({0,0,.5f}, identity, {0,0,0,600}, s), "zero width rejected");
    auto bad = identity;
    bad.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    Check(!WorldToScreen({0,0,.5f}, bad, v, s), "NaN matrix rejected");
    Check(!WorldToScreen({0,0,.5f}, Matrix4x4{}, v, s), "zero w rejected");
    Matrix4x4 perspective{}; // LH, 90 degree vertical FOV, aspect 1, near 1, far 10.
    perspective.m[0][0] = perspective.m[1][1] = 1.f;
    perspective.m[2][2] = 10.f/9.f;
    perspective.m[2][3] = -10.f/9.f;
    perspective.m[3][2] = 1.f;
    Check(WorldToScreen({1,1,2}, perspective, {0,0,100,100}, s) && Near(s.x,75) && Near(s.y,25), "perspective division");
    Check(!WorldToScreen({0,0,-2}, perspective, v, s), "behind camera rejected");
    ScreenBox box;
    Check(CalculateBoundingBox({0,0,.5f}, {-.5f,-.5f,-.1f}, {.5f,.5f,.1f}, identity, v, box) &&
        Near(box.min.x,480) && Near(box.min.y,270) && Near(box.max.x,1440) && Near(box.max.y,810), "eight-corner AABB");
    Check(CalculateBoundingBox({0,0,1}, {-.2f,-.3f,-.5f}, {.2f,.3f,.5f}, perspective, v, box), "near-plane intersection retained");
    Check(CalculateBoundingBox({0,0,10}, {-.2f,-.3f,-.5f}, {.2f,.3f,.5f}, perspective, v, box), "far-plane intersection retained");
    Check(!CalculateBoundingBox({0,0,-2}, {-.2f,-.3f,-.5f}, {.2f,.3f,.5f}, perspective, v, box), "behind-camera AABB rejected");
    Check(!CalculateBoundingBox({20,0,.5f}, {-.5f,-.5f,0}, {.5f,.5f,0}, identity, v, box), "off-screen AABB rejected");
    Check(CalculateBoundingBox({.9f,0,.5f}, {-.5f,-.5f,0}, {.5f,.5f,0}, identity, v, box) &&
        Near(box.max.x,1920), "partially off-screen box clipped");
    Check(!CalculateBoundingBox({}, {1,0,0}, {-1,1,1}, identity, v, box), "inverted bounds rejected");
    Check(!CalculateBoundingBox({}, {}, {}, identity, v, box), "degenerate box rejected");
    Check(Near(HealthFraction(50,100),.5f), "normal health");
    Check(Near(HealthFraction(150,100),1), "overheal clamped");
    Check(Near(HealthFraction(-1,100),0), "negative health clamped");
    Check(Near(HealthFraction(50,0),0), "zero maximum health");
    Check(Near(HealthFraction(std::numeric_limits<float>::infinity(),100),0), "infinite health rejected");
    Check(Near(Distance({0,0,0},{3,4,12}),13), "3D distance");
    Check(Near(DistanceAlpha(10,60,120),1), "close entity opaque");
    Check(Near(DistanceAlpha(60,60,120),1), "fade starts continuously");
    Check(Near(DistanceAlpha(90,60,120),.5f), "fade midpoint");
    Check(Near(DistanceAlpha(120,60,120),0), "maximum distance invisible");
    Check(Near(DistanceAlpha(std::numeric_limits<float>::quiet_NaN(),60,120),0), "invalid distance invisible");
    Check(Near(DistanceAlpha(10,std::numeric_limits<float>::quiet_NaN(),120),0), "nonfinite fade start rejected");
    Check(Near(DistanceAlpha(10,60,std::numeric_limits<float>::infinity()),0), "infinite maximum rejected");
    Check(Near(DistanceAlpha(10,120,60),0), "inverted fade range rejected");
    const auto huge=(std::numeric_limits<float>::max)();
    Check(!Valid(Viewport{huge,0,huge,1080}), "overflowing viewport rejected");
    const auto bottom=CalculateHudFooter({{50,80},{80,100}},{0,0,100,100},10,16,true,true);
    Check(bottom.weaponY>=bottom.distanceY+19 && bottom.weaponY+bottom.weaponHeight<=100,"HUD footer remains inside bottom edge without text/icon overlap");
    Check(bottom.weaponX>=0 && bottom.weaponX+bottom.weaponWidth<=100,"HUD footer stays inside horizontal viewport");
    const auto hudNear=CalculateHudFooter({{0,0},{200,300}},{0,0,1920,1080},3,16,true,true);
    Check(hudNear.weaponWidth==90,"near-player weapon size is capped");
    const auto hudFar=CalculateHudFooter({{0,0},{8,20}},{0,0,1920,1080},3,16,true,true);
    Check(hudFar.weaponWidth==36 && hudFar.weaponWidth<hudNear.weaponWidth,"far-player icon scales down while staying legible");
    struct Source final : IEntitySource {
        mutable std::uint32_t largestRead{};
        std::uint32_t SlotCount() const noexcept override { return 70; }
        bool ReadEntity(std::uint32_t index, EntitySnapshot& out) const noexcept override {
            largestRead = index;
            out.valid = 1;
            for (auto& ch : out.name) ch = 'x';
            return index != 7;
        }
    } source;
    FrameSnapshot frame;
    frame.weaponDefinitionIndices[0]=7;
    CopyEntityList(source, frame);
    Check(frame.weaponDefinitionIndices[0]==0,"generic entity adapter clears unsupported weapon metadata");
    Check(frame.entityCount == 64 && source.largestRead == 63, "adapter caps source at 64 slots");
    Check(frame.entities[7].valid == 0 && frame.entities[7].name[0] == '\0', "adapter clears all fields of failed reads");
    Check(frame.entities[63].valid == 1, "adapter retains successful reads");
    Check(frame.entities[63].name[63] == '\0', "adapter terminates fixed-length names");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
