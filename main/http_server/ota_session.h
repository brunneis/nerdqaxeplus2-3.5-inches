#pragma once

#include <atomic>

// Serializes manual uploads and the background GitHub update task.
class OtaSession {
    inline static std::atomic_flag active = ATOMIC_FLAG_INIT;
    const bool acquired = !active.test_and_set(std::memory_order_acquire);
public:
    OtaSession() = default;
    OtaSession(const OtaSession &) = delete;
    OtaSession &operator=(const OtaSession &) = delete;
    explicit operator bool() const { return acquired; }
    ~OtaSession() { if (acquired) active.clear(std::memory_order_release); }
};
