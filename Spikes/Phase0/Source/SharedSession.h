#pragma once

#include <atomic>
#include <cstdint>

// Process-wide state shared by every Chordism Spike instance that was loaded from
// the same plugin binary. Spike (4): do two instances in one Live set see the same
// object? (An AU instance and a VST3 instance are different binaries, so they each
// get their own copy even inside one process.)
struct SharedSession
{
    static SharedSession& get()
    {
        static SharedSession session;
        return session;
    }

    std::atomic<int> key { 0 };              // 0 = C … 11 = B
    std::atomic<uint32_t> version { 0 };     // bumped on every write
    std::atomic<int> lastWriterId { 0 };
    std::atomic<int> instanceCount { 0 };
    std::atomic<int> nextInstanceId { 1 };

    void setKey (int newKey, int writerId) noexcept
    {
        key.store (newKey);
        lastWriterId.store (writerId);
        version.fetch_add (1);
    }

private:
    SharedSession() = default;
};
