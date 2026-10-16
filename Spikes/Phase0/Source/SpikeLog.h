#pragma once

#include <JuceHeader.h>
#include <mutex>
#include <unistd.h>

// Diagnostic log for the Live checklist: save/restore, hosting and port events go
// to ~/Library/Logs/ChordismSpike.log, so a failed run in Live can be read back
// afterwards. Never call from the audio thread.
namespace SpikeLog
{
    inline juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Logs/ChordismSpike.log");
    }

    inline void write (int instanceId, const juce::String& text)
    {
        static std::mutex lock;
        const std::lock_guard<std::mutex> g (lock);

        const auto thread = juce::MessageManager::existsAndIsCurrentThread() ? "msg" : "other";
        const auto line = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S")
                        + juce::String::formatted (".%03d", juce::Time::getCurrentTime().getMilliseconds())
                        + "  pid " + juce::String ((int) getpid())
                        + "  #" + juce::String (instanceId) + " [" + thread + "]  " + text + "\n";

        file().appendText (line, false, false, "\n");
    }
}
