// Theory core tests (PLAN.md R2, §8): every white key's chord in all 12 keys × all scales ×
// all Chord Groups, against the music21 reference in fixtures/theory_reference.json
// (written by Tools/gen_theory_fixture.py).
#include "TestUtil.h"
#include "MiniJson.h"
#include "core/theory/ChordGroups.h"
#include <algorithm>
#include <set>
#include <sstream>

using namespace chordism;
using testutil::check;

static std::string fixturePath()
{
    if (const char* p = std::getenv ("CHORDISM_FIXTURES"))
        return std::string (p) + "/theory_reference.json";
    return "Tests/fixtures/theory_reference.json";
}

static std::string list (const std::vector<int>& v)
{
    std::ostringstream o;
    for (size_t i = 0; i < v.size(); ++i)
        o << (i ? " " : "") << v[i];
    return o.str();
}

static void testScaleOffsets()
{
    std::cout << "== scale offsets ==\n";
    check (scaleOffset (Scale::Major, 0) == 0 && scaleOffset (Scale::Major, 7) == 12
               && scaleOffset (Scale::Major, 9) == 16, "major: tonic, octave, 10th");
    check (scaleOffset (Scale::Major, -1) == -1 && scaleOffset (Scale::Major, -3) == -5
               && scaleOffset (Scale::Major, -7) == -12 && scaleOffset (Scale::Major, -8) == -13,
           "major: indices below the tonic");

    bool monotonic = true;
    for (int s = 0; s < numScales; ++s)
        for (int i = -14; i < 21; ++i)
            monotonic &= scaleOffset ((Scale) s, i + 1) > scaleOffset ((Scale) s, i);
    check (monotonic, "every scale rises strictly from index -14 to 21");
}

static void testModel()
{
    std::cout << "== chord model ==\n";
    ChordSlot slot = makeGroupSlot (ChordGroup::Triads, Scale::Major, 0);
    ChordContext ctx;

    auto sorted = [] (const ChordNotes& n) { std::vector<int> v (n.begin(), n.end()); std::sort (v.begin(), v.end()); return v; };

    check (sorted (chordNotes (slot, ctx)) == std::vector<int> { 48, 52, 55 }, "C major I = C3 E3 G3 (MIDI 48 52 55)");

    ctx.outputOctave = 1;
    check (sorted (chordNotes (slot, ctx)) == std::vector<int> { 60, 64, 67 }, "Chord Octave +1 moves the chord up 12");
    ctx.outputOctave = 0;

    slot.steps[1].accidental = -1;
    check (sorted (chordNotes (slot, ctx)) == std::vector<int> { 48, 51, 55 }, "a flat on the 3rd makes it minor");
    slot.steps[1].accidental = 0;

    slot.steps[0].octaveMask = (uint8_t) (octaveBit (-1) | octaveBit (0) | octaveBit (1));
    check (sorted (chordNotes (slot, ctx)) == std::vector<int> { 36, 48, 52, 55, 60 }, "octave stacking on the root");

    // Notes off the MIDI range are dropped, never wrapped.
    ctx.outputOctave = 7;
    const auto high = chordNotes (makeGroupSlot (ChordGroup::Thirteenths, Scale::Major, 6), ctx);
    bool inRange = true;
    for (auto n : high) inRange &= n <= 127;
    check (inRange && high.size < 6, "notes above 127 are dropped");

    ChordSlot octaves;
    octaves.steps[0] = { true, 0, (uint8_t) (octaveBit (0) | octaveBit (1) | octaveBit (2)) };
    ctx.outputOctave = 0;
    check (sorted (chordNotes (octaves, ctx)) == std::vector<int> { 48, 60, 72 }, "each octave of a step is a separate note");

    // A pitch reached by two steps (root, and a double-flat 9th an octave down) is listed once.
    ChordSlot dup;
    dup.steps[0] = { true, 0, octaveBit (0) };
    dup.steps[4] = { true, -2, octaveBit (-1) };
    check (sorted (chordNotes (dup, ctx)) == std::vector<int> { 48 }, "a pitch reached twice is played once");

    check (makeGroupSlot (ChordGroup::Triads, Scale::NaturalMinor, 4).steps[1].accidental == 1,
           "natural minor V: the 3rd is raised by default");
    check (makeGroupSlot (ChordGroup::Triads, Scale::NaturalMinor, 3).steps[1].accidental == 0,
           "natural minor iv: no default accidental");
}

static void testAgainstReference()
{
    std::cout << "== reference (music21) ==\n";
    minijson::Value doc;
    try
    {
        doc = minijson::parseFile (fixturePath());
    }
    catch (const std::exception& e)
    {
        check (false, std::string ("read fixture: ") + e.what());
        return;
    }

    check (doc["base_midi"].asInt() == chordBaseNote, "fixture base note matches chordBaseNote (48)");

    // Scales: same names, same order, same semitones.
    const auto& scales = doc["scales"];
    bool scalesMatch = (int) scales.size() == numScales;
    for (int s = 0; s < numScales && scalesMatch; ++s)
    {
        const auto& info = scaleInfo ((Scale) s);
        scalesMatch &= scales[(size_t) s]["name"].str == info.name;
        for (int d = 0; d < 7; ++d)
            scalesMatch &= scales[(size_t) s]["semitones"][(size_t) d].asInt() == info.semitones[(size_t) d];
        if (! scalesMatch)
            std::cout << "        first mismatch: scale " << info.name << "\n";
    }
    check (scalesMatch, "all " + std::to_string (numScales) + " scales match music21 (names, order, semitones)");

    const auto& groups = doc["groups"];
    bool groupsMatch = (int) groups.size() == numChordGroups;
    for (int g = 0; g < numChordGroups && groupsMatch; ++g)
        groupsMatch &= groups[(size_t) g].str == chordGroupInfo ((ChordGroup) g).name;
    check (groupsMatch, "all " + std::to_string (numChordGroups) + " chord groups listed in the same order");

    // Every chord.
    const auto& sets = doc["chords"];
    std::set<std::string> covered;
    int compared = 0, mismatches = 0;
    for (size_t i = 0; i < sets.size(); ++i)
    {
        const auto& e = sets[i];
        const int key = e["key"].asInt();
        int scale = -1, group = -1;
        for (int s = 0; s < numScales; ++s)
            if (e["scale"].str == scaleInfo ((Scale) s).name) scale = s;
        for (int g = 0; g < numChordGroups; ++g)
            if (e["group"].str == chordGroupInfo ((ChordGroup) g).name) group = g;
        if (scale < 0 || group < 0)
        {
            check (false, "fixture names a scale or group the code lacks: " + e["scale"].str + " / " + e["group"].str);
            continue;
        }
        covered.insert (std::to_string (key) + "/" + std::to_string (scale) + "/" + std::to_string (group));

        const ChordSet chords = makeGroupChords ((ChordGroup) group, (Scale) scale);
        for (int d = 0; d < numChordSlots; ++d)
        {
            const auto notes = chordNotes (chords[(size_t) d], { key, (Scale) scale, 0 });
            std::vector<int> got (notes.begin(), notes.end());
            std::sort (got.begin(), got.end());

            std::vector<int> want;
            for (const auto& n : e["chords"][(size_t) d].arr)
                want.push_back (n.asInt());

            ++compared;
            if (got != want && ++mismatches <= 10)
                std::cout << "        key " << keyName (key) << ", " << scaleInfo ((Scale) scale).name << ", "
                          << chordGroupInfo ((ChordGroup) group).name << ", degree " << d + 1
                          << ": got [" << list (got) << "], music21 [" << list (want) << "]\n";
        }
    }

    const int expected = numKeys * numScales * numChordGroups;
    check ((int) covered.size() == expected,
           "fixture covers all 12 keys x " + std::to_string (numScales) + " scales x " + std::to_string (numChordGroups)
               + " groups (" + std::to_string (covered.size()) + " of " + std::to_string (expected) + ")");
    check (mismatches == 0 && compared == expected * numChordSlots,
           std::to_string (compared) + " chords compared with music21, " + std::to_string (mismatches) + " differ");
}

int main()
{
    testScaleOffsets();
    testModel();
    testAgainstReference();
    return testutil::finish ("test_theory");
}
