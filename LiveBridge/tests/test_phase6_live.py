"""Phase 6 checks in Live (PLAN.md §7, row 6): Melody, presets and Export.

Uses the Phase 1 rig (tracks "Chordism P1 Src" / "Chordism P1 Rec", test_phase1_live.py) and
the VST3 "MIDI From" route, which is sample-accurate.

    melody-seed   Test smart "Pattern 1" with slot 1 in Melody mode (Straight, 8 notes, seed 7),
                  the Smart zone an octave up: every note of the take is the one the generator
                  gives (a Python port of Source/core/engine/Melody.h below), a second take is
                  identical, and seed 8 plays another melody
    preset        the factory presets are the plug-in's programs, and Live shows JUCE's "Program"
                  parameter (Live lists no VST3 programs under the device's presets): choosing a
                  program loads it, Live sees its parameters change, the routing and the test rig
                  stay as they were, and Init brings back the defaults
    export        a take of the test pattern (Pop, Sevenths, Strum Up 1/64), recorded on the Rec
                  track. While the Test pattern is on, Chordism writes each finished take the way
                  Export drags it (ChordismProcessor::testTakeFile): that file, parsed here, holds
                  the recorded clip note for note; and loaded through Live's browser (Live's own
                  MIDI file import, as for a dropped file) it becomes a 16-beat clip with the same
                  notes. The drag gesture itself is checked by hand (PLAN.md §12).

Every time must match within 0.1 ms (a file tick, 1/960 beat, is 0.52 ms at 120 BPM: file
times are compared within one tick).
"""

import os
import shutil
import struct
import time

import pytest

from test_phase1_live import (  # noqa: F401  (rig and reference are fixtures)
    BARS, PLUGIN, TEMPO, _configure, _record, _save, _set, reference, rig)
from test_phase3_live import DEFAULTS as CHORD_DEFAULTS, STRUM, STRUM_RATES, _apply
from test_phase4_live import SMART_DEFAULTS, TEST_SMART, PATTERN_ARP

pytestmark = pytest.mark.live

MAJOR = [0, 2, 4, 5, 7, 9, 11]
SS_TONIC = 72                     # the Smart zone's tonic (C5) with SS octave +1
TOL_MS = 0.1
RHYTHMS = ["Straight", "Syncopated", "Dotted", "Clave", "Offbeat", "Free"]
TEST_TAKE = os.path.expanduser("~/Music/Chordism/Exports/Test take.mid")
USER_CLIPS = os.path.expanduser("~/Music/Ableton/User Library/Clips/Chordism")

# The plug-in's programs (Source/plugin/FactoryPresets.cpp), Init first.
PROGRAMS = ["Init", "Pop Anthem", "Neo-Soul Keys", "House Stabs", "Lo-Fi Sevenths", "Trap Minor Arps",
            "Reggaeton Dembow", "Synthwave Drive", "Jazz Two-Five", "Gospel Thirteenths", "Bossa Ninths",
            "Cinematic Swells", "Ambient Pads", "Funk Rhythm Guitar", "Classic Rock Power", "Baroque Canon",
            "Dreamy", "Dark", "Uplifting", "Melancholy", "Tense", "Bright Melody"]


# -- Melody.h, ported -----------------------------------------------------------------------

M64 = (1 << 64) - 1
WEIGHTS = {
    "Straight":   [9, 1, 4, 1, 7, 1, 4, 1, 8, 1, 4, 1, 7, 1, 4, 1],
    "Syncopated": [8, 2, 3, 7, 2, 1, 7, 2, 3, 2, 7, 1, 2, 7, 3, 5],
    "Dotted":     [9, 1, 1, 8, 1, 1, 8, 1, 1, 7, 1, 1, 7, 1, 1, 6],
    "Clave":      [9, 1, 2, 8, 1, 2, 8, 1, 2, 2, 8, 1, 8, 1, 2, 1],
    "Offbeat":    [3, 1, 9, 1, 3, 1, 8, 1, 3, 1, 9, 1, 3, 1, 8, 2],
    "Free":       [5] * 16,
}


class _Random:
    def __init__(self, seed, slot, bar):
        self.state = (seed * 0x100000001b3 + (slot + 1) * 0x9e3779b1) & M64
        self.state ^= ((bar & M64) * 0xc2b2ae3d27d4eb4f) & M64
        self.next()

    def next(self):
        self.state = (self.state + 0x9e3779b97f4a7c15) & M64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & M64
        z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & M64
        return ((z ^ (z >> 31)) >> 32) & 0xffffffff

    def below(self, n):
        return self.next() % n if n > 0 else 0


def melody_bar(seed, slot, bar, notes=8, rhythm="Straight", rng_range=1, slots=16):
    """[(sixteenth, scale index from the tonic, strong)] of one bar."""
    rng = _Random(seed, slot, bar)
    wanted = max(1, min(slots, int(notes * slots / 16.0 + 0.5)))
    score = [8 * WEIGHTS[rhythm][j % 16] + rng.below(12) for j in range(slots)]
    chosen = [False] * slots
    for _ in range(wanted):
        best = -1
        for j in range(slots):
            if not chosen[j] and (best < 0 or score[j] > score[best]):
                best = j
        chosen[best] = True
    span = 7 * rng_range
    mid = span // 2
    a = (3 * span) // 8 + rng.below(span // 4 + 1)
    out = []
    for j in range(slots):
        if not chosen[j]:
            continue
        if out:
            r = rng.below(100)
            size = 1 if r < 50 else (2 if r < 75 else (0 if r < 87 else 3 + rng.below(2)))
            if size > 0:
                down = 65 if a > mid else (35 if a < mid else 50)
                a += -size if rng.below(100) < down else size
                if a < 0:
                    a = -a
                if a > span:
                    a = 2 * span - a
                a = max(0, min(span, a))
        out.append((j, a, j % 4 == 0 or not out))
    return out


def _major(i):
    return MAJOR[i % 7] + 12 * (i // 7)


def melody_notes(seed, beats, gate=0.5, count=3):
    """The Test smart Pattern 1 take in C major: (start, pitch, length), the chord of beat b being
    degree b mod 7 (its key pressed just before the melody's note on that beat)."""
    out = []
    for bar in range(int(beats // 4) + 1):
        notes = melody_bar(seed, 0, bar)
        starts = [4 * bar + j / 4.0 for j, _, _ in notes] + [4 * bar + 4.0]
        for i, (j, a, strong) in enumerate(notes):
            start = starts[i]
            d = int(start + 1e-9) % 7
            if strong:
                for delta in (0, -1, 1, -2, 2, -3, 3):
                    if (a + delta - d) % 7 in [(2 * k) % 7 for k in range(count)]:
                        a += delta
                        break
            out.append((start, SS_TONIC + _major(a), gate * (starts[i + 1] - start)))
    return out


# -- a small MIDI file reader ---------------------------------------------------------------

def read_midi(path):
    """(ppq, [(start tick, end tick, pitch, velocity)]) of a type 0/1 file."""
    data = open(path, "rb").read()
    assert data[:4] == b"MThd", "not a MIDI file"
    length, fmt, ntracks, division = struct.unpack(">IHHH", data[4:14])
    pos = 8 + length
    notes, open_notes = [], {}
    for _ in range(ntracks):
        assert data[pos:pos + 4] == b"MTrk"
        (tlen,) = struct.unpack(">I", data[pos + 4:pos + 8])
        p, end, tick, status = pos + 8, pos + 8 + tlen, 0, 0
        while p < end:
            delta = 0
            while True:
                c = data[p]
                p += 1
                delta = (delta << 7) | (c & 0x7F)
                if not c & 0x80:
                    break
            tick += delta
            b = data[p]
            if b == 0xFF:
                ln = data[p + 2]
                p += 3 + ln
                continue
            if b & 0x80:
                status = b
                p += 1
            kind = status & 0xF0
            d1 = data[p]
            d2 = data[p + 1] if kind not in (0xC0, 0xD0) else 0
            p += 1 if kind in (0xC0, 0xD0) else 2
            key = (status & 0x0F, d1)
            if kind == 0x90 and d2 > 0:
                open_notes.setdefault(key, []).append((tick, d2))
            elif kind in (0x80, 0x90) and open_notes.get(key):
                start, vel = open_notes[key].pop(0)
                notes.append((start, tick, d1, vel))
        pos = end
    notes.sort()
    return division, notes


# -- the checks -----------------------------------------------------------------------------

def _melody_settings(seed):
    return {"SS octave": (1, (-3, 3)), "Pattern 1 arp": ("Melody", PATTERN_ARP),
            "Pattern 1 notes": (8, (1, 16)), "Pattern 1 rhythm": ("Straight", RHYTHMS),
            "Melody seed": (seed, (1, 999)), "Melody swing": (0, (0, 100)), "Test smart": ("Pattern 1", TEST_SMART)}


def _melody_take(live, rig, reference, seed):
    _configure(live, rig, reference, 0, "Major", "Triads", compensate=True)
    _set(live, rig, "Progression", 0, [""] * 9)
    _set(live, rig, "Test shifter", 0, [""] * 6)
    _apply(live, rig, CHORD_DEFAULTS)
    _apply(live, rig, SMART_DEFAULTS)
    shown = _apply(live, rig, _melody_settings(seed))
    try:
        routing, audio, clip, notes, beats = _record(live, rig, "vst3")
    finally:
        _apply(live, rig, SMART_DEFAULTS)
        _apply(live, rig, {"Pattern 1 arp": ("Linear Up", PATTERN_ARP), "Melody seed": (1, (1, 999))})
    smart = sorted(((round(n["song_time"], 6), n["pitch"], n["duration"], n["velocity"]) for n in notes if n["pitch"] >= SS_TONIC - 2))
    return shown, beats, smart


def test_melody_same_seed(live, rig, reference, report):
    tempo_ms = 60000.0 / TEMPO
    tol = TOL_MS / tempo_ms
    shown, beats, first = _melody_take(live, rig, reference, 7)
    _, _, second = _melody_take(live, rig, reference, 7)
    _, _, other = _melody_take(live, rig, reference, 8)

    want = [w for w in melody_notes(7, beats) if w[0] < beats - 0.5]
    lines, wrong = [], 0
    for i in range(max(len(want), len(first))):
        w = want[i] if i < len(want) else None
        g = first[i] if i < len(first) else None
        ok = w is not None and g is not None and g[1] == w[1] and abs(g[0] - w[0]) <= tol and abs(g[2] - w[2]) <= tol and g[3] == 100
        wrong += 0 if ok else 1
        lines.append("%-28s %-28s %s" % (w and (round(w[0], 4), w[1], round(w[2], 4)), g and (g[0], g[1], round(g[2], 4)), "ok" if ok else "WRONG"))
    same = len(first) == len(second) and all(
        a[1] == b[1] and abs(a[0] - b[0]) <= tol and abs(a[2] - b[2]) <= tol and a[3] == b[3] for a, b in zip(first, second))
    text = "%s Phase 6 Melody (seed 7, Straight, 8 notes/bar, %d bars): %d notes, want %d; second take %s; seed 8 %s\n" % (
        PLUGIN, BARS, len(first), len(want), "identical" if same else "DIFFERENT",
        "differs" if other != first else "IS THE SAME")
    text += "plug-in shows: %s\n" % ", ".join("%s=%s" % kv for kv in shown.items())
    text += "want (start, pitch, length)  got\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase6-melody", {"shown": shown, "first": first, "second": second, "seed8": other, "want": want})

    assert first, "no melody recorded"
    assert wrong == 0, "%d melody notes differ from the generator" % wrong
    assert same, "a second take with the same seed differs"
    assert other != first, "seed 8 played the same melody as seed 7"


def _program(live, rig, index):
    return live.call("set_parameter", track=rig["src"], device=rig["device"], parameter="Program",
                     value=index / float(len(PROGRAMS) - 1))


def _shown(live, rig, names):
    return {n: live.call("get_parameter", track=rig["src"], device=rig["device"], parameter=n)["display"] for n in names}


def test_preset_program(live, rig, reference, report):
    _configure(live, rig, reference, 0, "Major", "Triads", compensate=True)
    _set(live, rig, "Test pattern", False)
    names = [_program(live, rig, i)["display"] for i in range(len(PROGRAMS))]
    watched = ["Key", "Scale", "Chord Group", "Progression", "Step seq", "Seq pattern", "Velocity", "Strum", "Arp",
               "Pattern 1 arp", "Pattern 1 notes", "Melody seed", "Virtual port", "Test pattern", "Enable"]
    loads = {}
    for name in ("House Stabs", "Jazz Two-Five", "Bright Melody", "Init"):
        _program(live, rig, PROGRAMS.index(name))
        time.sleep(0.3)
        loads[name] = _shown(live, rig, watched)

    text = "%s Phase 6 presets: programs %s\n" % (PLUGIN, names)
    text += "\n".join("%-14s %s" % (k, v) for k, v in loads.items())
    report(text)
    print(text)
    _save("phase6-presets", {"programs": names, "loads": loads})

    assert names == PROGRAMS
    hs = loads["House Stabs"]
    assert (hs["Key"], hs["Scale"], hs["Chord Group"], hs["Step seq"], hs["Seq pattern"], hs["Velocity"]) == \
        ("A", "Natural Minor", "Sevenths", "On", "2", "House"), hs
    jz = loads["Jazz Two-Five"]
    assert (jz["Key"], jz["Chord Group"], jz["Strum"], jz["Step seq"]) == ("Bb", "Ninths", "Down", "Off"), jz
    bm = loads["Bright Melody"]
    assert (bm["Pattern 1 arp"], bm["Pattern 1 notes"], bm["Melody seed"]) == ("Melody", "10", "42"), bm
    init = loads["Init"]
    assert (init["Key"], init["Scale"], init["Chord Group"], init["Pattern 1 arp"], init["Strum"]) == \
        ("C", "Major", "Triads", "Linear Up", "Off"), init
    for v in loads.values():   # a preset leaves the routing and the test rig alone
        assert (v["Virtual port"], v["Test pattern"], v["Enable"]) == ("On", "Off", "On"), v


def _wait_file(path, after, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.exists(path) and os.path.getmtime(path) >= after:
            return True
        time.sleep(0.1)
    return False


def test_export_take(live, rig, reference, report):
    _configure(live, rig, reference, 0, "Major", "Sevenths", compensate=True)
    _set(live, rig, "Progression", 2, [""] * 9)          # Pop
    _set(live, rig, "Test shifter", 0, [""] * 6)
    _apply(live, rig, CHORD_DEFAULTS)
    _apply(live, rig, SMART_DEFAULTS)
    shown = _apply(live, rig, {"Strum": ("Up", STRUM), "Strum rate": ("1/64", STRUM_RATES)})
    if os.path.exists(TEST_TAKE):
        os.remove(TEST_TAKE)
    started = time.time()
    try:
        routing, audio, clip, notes, beats = _record(live, rig, "vst3")
    finally:
        _apply(live, rig, CHORD_DEFAULTS)
        _set(live, rig, "Progression", 0, [""] * 9)
    assert _wait_file(TEST_TAKE, started - 1), "Chordism wrote no take to %s" % TEST_TAKE

    tempo_ms = 60000.0 / TEMPO
    ppq, file_notes = read_midi(TEST_TAKE)
    tick = 1.0 / ppq
    recorded = sorted((n["song_time"], n["pitch"], n["duration"], n["velocity"]) for n in clip["notes"]
                      if -1e-6 <= n["song_time"] < beats - 1e-6)
    exported = [(s / float(ppq), p, (e - s) / float(ppq), v) for s, e, p, v in file_notes]

    def compare(a, b, tol):
        if len(a) != len(b):
            return ["%d notes vs %d" % (len(a), len(b))]
        bad = []
        for x, y in zip(a, b):
            if x[1] != y[1] or abs(x[0] - y[0]) > tol or abs(x[2] - y[2]) > tol or x[3] != y[3]:
                bad.append("%s vs %s" % (x, y))
        return bad

    file_vs_take = compare(exported, recorded, tick + TOL_MS / tempo_ms)

    # Live's own import of the file, through its browser (as for a drop).
    os.makedirs(USER_CLIPS, exist_ok=True)
    name = "Chordism export %d.mid" % int(started)
    target = os.path.join(USER_CLIPS, name)
    shutil.copy(TEST_TAKE, target)
    imported, clip_info = [], {}
    new_track = None
    try:
        live.wait_until(lambda: live.call("browser_search", query=name, category="user_library"), 30.0,
                        interval=0.5, what="Live's browser to list %s" % name)
        res = live.call("load_clip_file", name=name)
        before = res["track_count_before"]
        live.wait_until(lambda: len(live.call("get_state", include_devices=False)["tracks"]) > before, 10.0,
                        what="the imported clip's track")
        new_track = before
        clip_info = live.wait_until(lambda: _session_clip(live, new_track), 10.0, what="the imported clip")
        imported = sorted((n["start_time"], n["pitch"], n["duration"], n["velocity"]) for n in clip_info["notes"])
    finally:
        if new_track is not None:
            live.call("delete_track", track=new_track)
        os.remove(target)
    clip_vs_file = compare(imported, exported, 1e-6)

    text = "%s Phase 6 Export (Pop, Sevenths, Strum Up 1/64, %d bars): file %d notes at %d PPQ, recorded %d, " \
           "Live's import %d notes, clip length %s\n" % (PLUGIN, BARS, len(exported), ppq, len(recorded), len(imported),
                                                       clip_info.get("length"))
    text += "plug-in shows: %s\n" % ", ".join("%s=%s" % kv for kv in shown.items())
    text += "file vs recorded take: %s\n" % ("identical (within a tick)" if not file_vs_take else file_vs_take[:5])
    text += "Live's clip vs file: %s" % ("identical" if not clip_vs_file else clip_vs_file[:5])
    report(text)
    print(text)
    _save("phase6-export", {"shown": shown, "file": exported, "recorded": recorded, "imported": imported,
                            "clip_length": clip_info.get("length")})

    assert exported, "the exported file has no notes"
    assert not file_vs_take, "the file differs from the recorded take: %s" % file_vs_take[:5]
    assert not clip_vs_file, "Live's clip differs from the file: %s" % clip_vs_file[:5]
    assert abs(clip_info["length"] - 4.0 * BARS) < 1e-6, "the clip is %s beats, want %d" % (clip_info["length"], 4 * BARS)


def _session_clip(live, track):
    try:
        return live.call("get_clip_notes", track=track, source="session")
    except Exception:  # noqa: BLE001 (not there yet)
        return None
