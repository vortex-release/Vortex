#include <awareness/FovRing.hpp>
#include <cstdio>
#include <limits>
int main() {
    using namespace awareness;
    int failures{};
    const auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL %s\n", why);
        }
    };
    const auto near = [](float a, float b) { return std::abs(a - b) < .05f; };
    Matrix4x4 m{};
    m.m[0][0] = 1;
    m.m[1][1] = 2;
    m.m[2][2] = 1;
    m.m[3][2] = 1;
    Viewport v{20, 30, 800, 400};
    auto r = ProjectFovRing(m, v, 30);
    check(r && near(r->center.x, 420) && near(r->center.y, 230) && near(r->radius.x, 230.94f) &&
              near(r->radius.y, 230.94f),
          "projection produces a round half-angle cone at viewport center");
    auto small = ProjectFovRing(m, v, 15);
    check(small && small->radius.x < r->radius.x * .5f, "angular radius follows tangent, not linear pixels");
    v.width *= 2;
    v.height *= 2;
    auto large = ProjectFovRing(m, v, 30);
    check(large && near(large->radius.x, r->radius.x * 2), "resolution scaling");
    m.m[0][2] = .2f;
    auto shifted = ProjectFovRing(m, v, 30);
    check(shifted && near(shifted->center.x, 980) && near(shifted->radius.x, large->radius.x),
          "off-axis projection principal point");
    m.m[3][2] = 0;
    check(!ProjectFovRing(m, v, 30), "orthographic projection has no angular ring");
    m.m[3][2] = 1;
    check(!ProjectFovRing(m, v, 90) && !ProjectFovRing(m, v, 0) &&
              !ProjectFovRing(m, v, std::numeric_limits<float>::quiet_NaN()),
          "invalid or infinite boundaries omitted");
    check(!ProjectFovRing(m, v, 89), "ring containing viewport is entirely offscreen");
    std::printf("FOV projection: %d failures\n", failures);
    return failures ? 1 : 0;
}
