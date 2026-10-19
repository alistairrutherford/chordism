// The plug-in's defines for the snapshot app, which compiles Source/plugin and Source/ui outside
// a plug-in target (forced include, ChordismSnapshot.jucer). Keep in step with Chordism.jucer.
#pragma once
#define JucePlugin_Name "Chordism"
#define JucePlugin_IsMidiEffect 0
#define JucePlugin_VersionString "0.1.0"

// The test runs the message loop between blocks (runDispatchLoopUntil).
#define JUCE_MODAL_LOOPS_PERMITTED 1
