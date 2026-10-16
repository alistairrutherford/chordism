#pragma once

#include <array>
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

    // Registry of live instances, so each editor can show every instance's key and
    // sync setting. id == 0 marks a free slot.
    struct Member
    {
        std::atomic<int> id { 0 };
        std::atomic<int> key { 0 };
        std::atomic<bool> sync { true };
    };

    static constexpr int maxMembers = 32;
    std::array<Member, maxMembers> members;

    Member* join (int id) noexcept
    {
        for (auto& m : members)
        {
            int expected = 0;
            if (m.id.compare_exchange_strong (expected, id))
                return &m;
        }
        return nullptr;
    }

    void leave (Member* m) noexcept
    {
        if (m != nullptr)
            m->id.store (0);
    }

private:
    SharedSession() = default;
};
