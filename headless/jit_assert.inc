// SPDX-License-Identifier: GPL-3.0-or-later
// Keep every upstream assertion, but outline only the exceptional path.
// JIT IR construction invokes these checks repeatedly for each compiled block.
#undef ASSERT_MSG
#define ASSERT_MSG(condition, ...)                                      \
    ([&]() {                                                           \
        auto&& assert_condition = (condition);                         \
        if (!(assert_condition)) [[unlikely]] {                        \
            [&]() YUZU_NO_INLINE {                                     \
                LOG_CRITICAL(Debug, __FILE__ ": assert " __VA_ARGS__); \
                AssertFailSoftImpl();                                  \
            }();                                                       \
        }                                                              \
    }())
