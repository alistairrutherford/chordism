#include "VirtualMidiOut.h"

#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <algorithm>
#include <mutex>
#include <set>

namespace chordism
{
    namespace
    {
        // One CoreMIDI client per process, shared by all instances, and the port numbers in use,
        // so each instance gets its own stable name. The client is never disposed: disposing a
        // process's last client can make the MIDI server exit.
        struct Registry
        {
            std::mutex lock;
            MIDIClientRef client = 0;
            std::set<int> used;

            static Registry& get()
            {
                static Registry r;
                return r;
            }
        };

        const mach_timebase_info_data_t& timebase()
        {
            static const mach_timebase_info_data_t tb = [] { mach_timebase_info_data_t t; mach_timebase_info (&t); return t; }();
            return tb;
        }

        MIDITimeStamp toHostTicks (int64_t ns)
        {
            if (ns <= 0)
                return 0;
            return (MIDITimeStamp) ((__int128) ns * timebase().denom / timebase().numer);
        }

        int64_t ticksToNs (uint64_t ticks)
        {
            return (int64_t) ((__int128) ticks * timebase().numer / timebase().denom);
        }

        // A stable unique ID per port name, so Live remembers each port's Track/Remote switches.
        SInt32 uniqueIdFor (int index)
        {
            return (SInt32) (0x43440000 | (index & 0xffff));   // 'CD' + n
        }

        constexpr UInt32 umpWord (uint8_t status, uint8_t d1, uint8_t d2)
        {
            // Universal MIDI Packet: MIDI 1.0 channel voice message (type 2), group 0.
            return (0x2u << 28) | ((UInt32) status << 16) | ((UInt32) d1 << 8) | (UInt32) d2;
        }
    }

    VirtualMidiOut::VirtualMidiOut() : juce::Thread ("Chordism MIDI port") {}

    VirtualMidiOut::~VirtualMidiOut()
    {
        close();
    }

    int64_t VirtualMidiOut::nowNs() noexcept
    {
        return ticksToNs (mach_absolute_time());
    }

    bool VirtualMidiOut::open (int preferredIndex)
    {
        close();

        auto& reg = Registry::get();
        std::lock_guard<std::mutex> guard (reg.lock);

        if (reg.client == 0)
        {
            const OSStatus err = MIDIClientCreateWithBlock (CFSTR ("Chordism"), &reg.client, nullptr);
            if (err != noErr)
            {
                reg.client = 0;
                lastError.store ((int32_t) err);
                return false;
            }
        }

        int chosen = preferredIndex >= 1 && reg.used.count (preferredIndex) == 0 ? preferredIndex : 0;
        for (int i = 1; chosen == 0; ++i)
            if (reg.used.count (i) == 0)
                chosen = i;

        MIDIEndpointRef source = 0;
        const CFStringRef name = nameForIndex (chosen).toCFString();
        const OSStatus err = MIDISourceCreateWithProtocol (reg.client, name, kMIDIProtocol_1_0, &source);
        CFRelease (name);
        if (err != noErr || source == 0)
        {
            lastError.store (err != noErr ? (int32_t) err : -1);
            return false;
        }

        MIDIObjectSetIntegerProperty (source, kMIDIPropertyUniqueID, uniqueIdFor (chosen));   // may be taken; harmless
        MIDIObjectSetStringProperty (source, kMIDIPropertyManufacturer, CFSTR ("Chordism"));

        reg.used.insert (chosen);
        lastError.store (0);
        index = chosen;
        heldNotes = {};
        latestStamp = 0;
        endpoint.store ((uint32_t) source);
        startThread (juce::Thread::Priority::highest);
        return true;
    }

    void VirtualMidiOut::close()
    {
        const auto source = (MIDIEndpointRef) endpoint.load();
        if (source == 0)
            return;

        // Flush what's queued (final note-offs included), then release whatever is left.
        signalThreadShouldExit();
        stopThread (500);
        drain();
        releaseHeldNotes();

        endpoint.store (0);
        MIDIEndpointDispose (source);

        auto& reg = Registry::get();
        std::lock_guard<std::mutex> guard (reg.lock);
        reg.used.erase (index);
        index = 0;
    }

    bool VirtualMidiOut::push (const uint8_t* bytes, int numBytes, int64_t hostTimeNs) noexcept
    {
        if (endpoint.load (std::memory_order_relaxed) == 0 || numBytes < 1 || numBytes > 3)
            return false;

        const auto scope = fifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 < 1)
            return false;

        auto& m = messages[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        m.hostTimeNs = hostTimeNs;
        m.size = (uint8_t) numBytes;
        for (int i = 0; i < 3; ++i)
            m.bytes[i] = i < numBytes ? bytes[i] : 0;
        return true;
    }

    void VirtualMidiOut::run()
    {
        while (! threadShouldExit())
        {
            drain();
            wait (1);
        }
    }

    void VirtualMidiOut::drain()
    {
        const auto source = (MIDIEndpointRef) endpoint.load();
        if (source == 0)
            return;

        alignas (MIDIEventList) uint8_t storage[4096];
        auto* list = reinterpret_cast<MIDIEventList*> (storage);

        while (fifo.getNumReady() > 0)
        {
            MIDIEventPacket* packet = MIDIEventListInit (list, kMIDIProtocol_1_0);
            int taken = 0;
            const auto scope = fifo.read (std::min (fifo.getNumReady(), 64));

            auto add = [&] (int start, int size)
            {
                for (int i = start; i < start + size && packet != nullptr; ++i)
                {
                    const auto& m = messages[(size_t) i];
                    const int status = m.bytes[0] & 0xf0, ch = m.bytes[0] & 0x0f, note = m.bytes[1] & 0x7f;
                    if (status == 0x90 && m.bytes[2] > 0)
                        heldNotes[(size_t) ch].set ((size_t) note);
                    else if (status == 0x80 || status == 0x90)
                        heldNotes[(size_t) ch].reset ((size_t) note);

                    const MIDITimeStamp stamp = toHostTicks (m.hostTimeNs);
                    latestStamp = std::max<uint64_t> (latestStamp, stamp);
                    const UInt32 word = umpWord (m.bytes[0], m.bytes[1], m.bytes[2]);
                    packet = MIDIEventListAdd (list, sizeof (storage), packet, stamp, 1, &word);
                    ++taken;
                }
            };
            add (scope.startIndex1, scope.blockSize1);
            add (scope.startIndex2, scope.blockSize2);

            if (taken > 0 && MIDIReceivedEventList (source, list) == noErr)
                numSent.fetch_add ((uint32_t) taken);
        }
    }

    void VirtualMidiOut::releaseHeldNotes()
    {
        const auto source = (MIDIEndpointRef) endpoint.load();
        if (source == 0)
            return;

        // CoreMIDI holds future-stamped events until their time and drops what's pending when
        // the source goes away. So release now (notes already delivered) and again just after
        // the latest scheduled event (note-ons still pending), and let the latter go out.
        const MIDITimeStamp now = mach_absolute_time();
        const MIDITimeStamp after = std::max<uint64_t> (now, latestStamp + 1);

        auto sendReleases = [&] (MIDITimeStamp when)
        {
            alignas (MIDIEventList) uint8_t storage[4096];
            auto* list = reinterpret_cast<MIDIEventList*> (storage);
            MIDIEventPacket* packet = MIDIEventListInit (list, kMIDIProtocol_1_0);
            int count = 0;
            for (int ch = 0; ch < 16; ++ch)
                for (int note = 0; note < 128 && packet != nullptr; ++note)
                    if (heldNotes[(size_t) ch].test ((size_t) note))
                    {
                        const UInt32 word = umpWord ((uint8_t) (0x80 | ch), (uint8_t) note, 0);
                        packet = MIDIEventListAdd (list, sizeof (storage), packet, when, 1, &word);
                        ++count;
                    }
            if (count > 0)
                MIDIReceivedEventList (source, list);
            return count;
        };

        if (sendReleases (now) > 0 && after > now)
        {
            sendReleases (after);
            const int64_t waitNs = std::min<int64_t> (250'000'000, ticksToNs (after - now));
            juce::Thread::sleep ((int) (waitNs / 1'000'000) + 2);
        }
        heldNotes = {};
    }
}
