// The Chord Editor's data as plug-in state (PLAN.md §4.2, §5): a ValueTree saved with the
// host parameters, and a lock-free snapshot of it for the audio thread.
//
//   <ChordEditor group="5">                    the Chord Group the edits were made on
//     <Degree index="4">                       one child per edited degree (0..6)
//       <Step index="0" on="1" acc="0" oct="4"/>   × 7: steps 1 3 5 7 9 11 13; oct = mask, bit 2 = 0
//     </Degree>
//   </ChordEditor>
//
// Every change goes through this class on the message thread (or whichever thread the host
// restores state on): it edits a plain ChordEdits model with the core's editor functions,
// writes the tree from it, and publishes a copy through a SnapshotExchange. The audio thread
// only ever calls read(): no locks or allocation there.
//
// Choosing a Chord Group resets the edits. The engine stops applying them the moment it sees
// the group change; sync() (on a timer) then clears them here too.
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "../core/engine/SnapshotExchange.h"
#include "../core/theory/ChordEditor.h"
#include "../core/theory/ChordTypes.h"

namespace chordism
{
    class ChordEditorState
    {
    public:
        static const juce::Identifier treeType;

        ChordEditorState();

        // Edits (message thread). `group` and `scale` are the current parameter values: the
        // first edit of a degree starts from that group's shape.
        void setStep (ChordGroup group, Scale scale, int degree, int step, Step value);
        void setOn (ChordGroup group, Scale scale, int degree, int step, bool on);
        void setAccidental (ChordGroup group, Scale scale, int degree, int step, int accidental);
        void setOctave (ChordGroup group, Scale scale, int degree, int step, int octave, bool on);
        void resetDegree (int degree);

        // The Active Chord menu: rewrites `degree`'s steps to chord type `type` (ChordTypes.h).
        void setChordType (ChordGroup group, Scale scale, int degree, int type);
        void resetAll (ChordGroup group);

        ChordEdits getEdits() const;

        // Bumped by every change (edits, reset, restore): the processor tells the host when it moves.
        uint32_t getVersion() const;

        // State. copyTree() for getStateInformation; restore() from setStateInformation, after
        // the host parameters (an invalid tree means no edits).
        juce::ValueTree copyTree() const;
        void restore (const juce::ValueTree& tree, ChordGroup group);

        // Timer: clears the edits once the Chord Group has moved on, from `group` (the parameter)
        // and the engine's discard counter and version (ChordismEngine::getEditsDiscarded).
        void sync (ChordGroup group, uint32_t engineDiscards, uint32_t discardedVersion);

        // Audio thread: the latest snapshot.
        const ChordEdits& read() noexcept { return exchange.read(); }

        static ChordEdits fromTree (const juce::ValueTree& tree);
        static juce::ValueTree toTree (const ChordEdits& edits);

    private:
        void commit();   // writes the tree and publishes; caller holds `lock`

        juce::CriticalSection lock;       // writers only (message thread, host state calls)
        ChordEdits model;
        juce::ValueTree tree { treeType };
        uint32_t version = 0;
        uint32_t seenDiscards = 0;
        SnapshotExchange<ChordEdits> exchange;
    };
}
