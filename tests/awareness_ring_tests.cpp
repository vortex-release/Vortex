#include <awareness/AwarenessRing.hpp>
#include <cstdio>
#include <limits>
using namespace awareness;
int main() {
    int failures{};
    const auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", why);
        }
    };
    const auto near = [](float a, float b) { return std::abs(a - b) < .001f; };
    auto matrix = Matrix4x4::Identity();
    matrix.m[3][2] = 1;
    matrix.m[3][3] = 0;
    const Vector3 up{0, 1, 0};
    auto direction = AwarenessDirection({}, {0, 0, 10}, matrix, up);
    check(direction && near(direction->x, 0) && near(direction->y, -1), "ahead points up");
    direction = AwarenessDirection({}, {10, 0, 0}, matrix, up);
    check(direction && near(direction->x, 1) && near(direction->y, 0), "right points right");
    direction = AwarenessDirection({}, {0, 0, -10}, matrix, up);
    check(direction && near(direction->x, 0) && near(direction->y, 1),
          "behind points down without inverted projection");
    direction = AwarenessDirection({}, {-10, 20, 0}, matrix, up);
    check(direction && near(direction->x, -1), "height does not change horizontal bearing");
    matrix.m[0][2] = .35f;
    direction = AwarenessDirection({}, {0, 0, 10}, matrix, up);
    check(direction && near(direction->x, 0), "off-center projection does not rotate the arrows");
    matrix.m[0][2] = 0;
    matrix.m[3][2] = 0;
    matrix.m[3][1] = 1;
    direction = AwarenessDirection({}, {0, 0, -10}, matrix, up);
    check(direction && near(direction->y, 1), "vertical view uses stable horizontal fallback");
    Matrix4x4 cs2{};
    cs2.m[0][1] = 2;
    cs2.m[3][0] = 1;
    direction = AwarenessDirection({1, 2, 3}, {1, 12, 3}, cs2, {0, 0, 1});
    check(direction && near(direction->x, 1), "CS2 Z-up uses the camera right axis");
    cs2.m[0][0] = -1;
    cs2.m[0][1] = 0;
    cs2.m[3][0] = 0;
    cs2.m[3][1] = 1;
    direction = AwarenessDirection({}, {10, 0, 0}, cs2, {0, 0, 1});
    check(direction && near(direction->x, -1), "rotating camera rotates target bearing immediately");
    check(!AwarenessDirection({}, {}, matrix, up), "coincident positions have no direction");
    check(!AwarenessDirection({}, {0, 10, 0}, matrix, up), "directly overhead has no horizontal direction");
    matrix.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    check(!AwarenessDirection({}, {1, 0, 0}, matrix, up), "invalid matrix rejected");
    const auto arrow = PlaceAwarenessArrow({0, -1}, {100, 50, 1920, 1080}, 150, 14);
    check(arrow && near(arrow->tip.x, 1060) && near(arrow->tip.y, 440) && arrow->left.y > arrow->tip.y,
          "arrow is centered in the camera viewport and points outwards");
    const auto small = PlaceAwarenessArrow({1, 0}, {0, 0, 960, 540}, 150, 14);
    check(small && near(small->tip.x, 555) && near(small->tip.y, 270), "ring scales with viewport height");
    check(!PlaceAwarenessArrow({0, 0}, {0, 0, 1920, 1080}, 150, 14), "zero direction rejected");
    check(!PlaceAwarenessArrow({1, 0}, {0, 0, 0, 1080}, 150, 14), "invalid viewport rejected");
    std::printf("Awareness direction/placement: %d failures\n", failures);
    return failures ? 1 : 0;
}
