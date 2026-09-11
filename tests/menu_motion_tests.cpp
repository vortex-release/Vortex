#include "menu_motion.hpp"
#include <cstdio>
int main() {
    awareness::MenuMotion motion, slow, fast;
    unsigned failures{};
    const auto check = [&](bool v) {
        if (!v)
            ++failures;
    };
    check(!motion.Visible());
    const float opening = motion.Update(true, .09f);
    check(opening > .49f && opening < .51f && motion.Visible());
    const float reversed = motion.Update(false, .02f);
    check(reversed > 0 && reversed < opening);
    check(motion.Update(true, .18f) == 1);
    check(motion.Update(false, .18f) == 0 && !motion.Visible());
    check(motion.Update(true, 0, false) == 1 && motion.Update(false, 0, false) == 0);
    float a{}, b{};
    for (int i = 0; i < 6; ++i)
        a = slow.Update(true, .01f);
    for (int i = 0; i < 3; ++i)
        b = fast.Update(true, .02f);
    check(std::abs(a - b) < .0001f);
    std::printf("Menu motion checks: %u failures\n", failures);
    return failures ? 1 : 0;
}
