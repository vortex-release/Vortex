#pragma once
#include <memory>
#include <type_traits>

namespace awareness {
// Reset large scratch records directly in their existing storage. Aggregate
// assignment from {} can reserve a second, equally large object on the host
// thread's fixed-size stack. Value construction preserves member defaults.
template <class T> void ResetInPlace(T &value) noexcept {
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_nothrow_default_constructible_v<T>);
    std::destroy_at(std::addressof(value));
    std::construct_at(std::addressof(value));
}
} // namespace awareness
