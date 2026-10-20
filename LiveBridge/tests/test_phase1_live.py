"""Phase 1 exit check (PLAN.md §7, row 1), run against a running Live 12.

Track "Chordism P1 Src" holds the Chordism plug-in with its Test pattern on: it presses one
chord-zone white key per beat (C, D, E, F, G, A, B, C…, so beat b plays chord slot b mod 7),
an eighth note long. Track "Chordism P1 Rec" (an instrument) takes Chordism's output through
the chosen route and records it into the arrangement. The recorded chords are compared, beat
by beat, with the music21 reference (Tests/fixtures/theory_reference.json), and their timing
is reported in beats and ms.

Route: the Rec track's MIDI From is the Src track / Chordism (the VST3), which Live records
sample-accurately. (The AU build and its CoreMIDI virtual-port route were removed on 2026-10-20.)

Settings (environment variables):
    CHORDISM_PLUGIN          plug-in name in Live's browser       (default "Chordism")
    CHORDISM_INSTRUMENT      Live instrument for the Rec track    (default "Drift")
    CHORDISM_BARS            bars per recording                   (default 4)
    CHORDISM_TEMPO           BPM                                  (default 120)
"""

import datetime
import json
import os
import time

import pytest

from livebridge import livelog, timing

PLUGIN = os.environ.get("CHORDISM_PLUGIN", "Chordism")
FORMAT = "vst3"
INSTRUMENT = os.environ.get("CHORDISM_INSTRUMENT", "Drift")
BARS = int(os.environ.get("CHORDISM_BARS", "4"))
TEMPO = float(os.environ.get("CHORDISM_TEMPO", "120"))

SRC_NAME = "Chordism P1 Src"
REC_NAME = "Chordism P1 Rec"
HERE = os.path.dirname(os.path.abspath(__file__))
REPORT_DIR = os.path.join(os.path.dirname(HERE), "reports")
FIXTURE = os.path.join(os.path.dirname(os.path.dirname(HERE)), "Tests", "fixtures", "theory_reference.json")

# The plug-in's choice lists (Source/core/theory): Key menu names, by index.
KEY_NAMES = ["C", "C#/Db", "D", "Eb", "E", "F", "F#/Gb", "G", "Ab", "A", "Bb", "B"]

# (key index, scale, chord group). The first is the Phase 1 exit criterion: C major chords.
CASES = [(0, "Major", "Triads"), (3, "Dorian", "Sevenths")]

pytestmark = pytest.mark.live


@pytest.fixture(scope="module")
def reference():
    with open(FIXTURE) as f:
        doc = json.load(f)
    table = {(c["key"], c["scale"], c["group"]): c["chords"] for c in doc["chords"]}
    return {"table": table, "scales": [s["name"] for s in doc["scales"]], "groups": doc["groups"]}


def _is_chordism(device):
    return device["name"].lower() == PLUGIN.lower()


@pytest.fixture(scope="module")
def rig(live):
    live.call("stop_transport")
    state = live.call("get_state")

    src = next((t for t in state["tracks"] if t["name"] == SRC_NAME and any(_is_chordism(d) for d in t["devices"])), None)
    if src is None:
        src = live.call("create_midi_track", name=SRC_NAME)
        live.load_device_and_wait(PLUGIN, src["index"], "plugins", FORMAT)
    src_i = src["index"]

    state = live.call("get_state")
    rec = next((t for t in state["tracks"] if t["name"] == REC_NAME and t["is_midi"]), None)
    if rec is None:
        rec = live.call("create_midi_track", name=REC_NAME)
        live.load_device_and_wait(INSTRUMENT, rec["index"], "instruments")
    rec_i = rec["index"]

    device = next(d for d in live.call("list_devices", track=src_i) if _is_chordism(d))

    live.call("set_track", track=src_i, arm=False)
    live.call("set_song_properties", loop=False, arrangement_overdub=False, midi_recording_quantization="none",
              signature_numerator=4, signature_denominator=4)
    live.call("set_tempo", bpm=TEMPO)

    r = {"src": src_i, "rec": rec_i, "device": device["index"], "device_name": device["name"]}
    params = live.call("list_parameters", track=src_i, device=device["index"])["parameters"]
    r["params"] = {p["name"]: p for p in params}
    yield r
    _set(live, r, "Test pattern", False)
    live.call("stop_transport")
    live.call("set_track", track=rec_i, arm=False)


def _set(live, rig, name, value, choices=None):
    """Set a Chordism parameter by name: a bool, a choice index (with `choices`, the list the
    plug-in offers), or an int in the parameter's own range (`choices` = (lo, hi))."""
    p = live.call("get_parameter", track=rig["src"], device=rig["device"], parameter=name)
    if isinstance(value, bool):
        v = value
    elif isinstance(choices, list):
        items = p.get("value_items") or []
        if len(items) == len(choices):
            v = items[value]
        else:
            v = p["min"] + (p["max"] - p["min"]) * value / (len(choices) - 1)
    else:
        lo, hi = choices
        v = p["min"] + (p["max"] - p["min"]) * (value - lo) / (hi - lo)
    return live.call("set_parameter", track=rig["src"], device=rig["device"], parameter=name, value=v)


def _configure(live, rig, ref, key, scale, group):
    shown = {}
    shown["Enable"] = _set(live, rig, "Enable", True)["display"]
    shown["Key"] = _set(live, rig, "Key", key, KEY_NAMES)["display"]
    shown["Scale"] = _set(live, rig, "Scale", ref["scales"].index(scale), ref["scales"])["display"]
    shown["Chord Group"] = _set(live, rig, "Chord Group", ref["groups"].index(group), ref["groups"])["display"]
    shown["Split"] = _set(live, rig, "Split", 60, (0, 127))["display"]
    shown["Chord Octave"] = _set(live, rig, "Chord Octave", 0, (-3, 3))["display"]
    _set(live, rig, "Mute CG", False)
    _set(live, rig, "Mute SS", False)
    _set(live, rig, "Test pattern", True)
    return shown


def _route(live, rig):
    """The Rec track's MIDI From: the Src track / Chordism."""
    rec, src = rig["rec"], rig["src"]
    r = live.call("set_input_routing", track=rec, source_track=src)
    names = [c["display_name"] for c in r["available_channels"]]
    chan = next((n for n in names if n.lower() == rig["device_name"].lower()), None)
    if chan is None:
        pytest.fail("Live lists no MIDI From channel %r on track %d; channels: %s" % (rig["device_name"], src + 1, names))
    return live.call("set_input_routing", track=rec, channel=chan)


def _record(live, rig):
    rec = rig["rec"]
    routing = _route(live, rig)
    live.call("set_track", track=rec, arm=True, monitoring="auto")
    live.call("clear_arrangement_clips", track=rec)
    time.sleep(0.3)
    beats = BARS * 4.0
    clip = live.record_arrangement(rec, beats, from_beat=0.0)
    live.call("set_track", track=rec, arm=False)
    audio = livelog.audio_settings(live.call("ping")["live_version"])
    notes = [n for n in clip["notes"] if -0.5 <= n["song_time"] < beats - 0.5]
    return routing, audio, clip, notes, beats


def _chord_check(notes, beats, chords):
    """Pitches per beat vs the reference; returns (lines, wrong beats, bad durations)."""
    by_beat = {}
    for n in notes:
        by_beat.setdefault(int(round(n["song_time"])), []).append(n)
    lines, wrong, bad_len = [], [], []
    for b in range(int(beats)):
        got = sorted(n["pitch"] for n in by_beat.get(b, []))
        want = chords[b % 7]
        ok = got == want
        if not ok:
            wrong.append(b)
        lens = [n["duration"] for n in by_beat.get(b, [])]
        if any(abs(d - 0.5) > 0.05 for d in lens):
            bad_len.append((b, lens))
        lines.append("beat %2d  slot %d  want %-22s got %-22s %s" % (b, b % 7 + 1, want, got, "ok" if ok else "WRONG"))
    return lines, wrong, bad_len


def _save(name, payload):
    os.makedirs(REPORT_DIR, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    with open(os.path.join(REPORT_DIR, "%s-%s.json" % (name, stamp)), "w") as f:
        json.dump(payload, f, indent=2)


@pytest.mark.parametrize("case", CASES, ids=lambda c: "%s-%s-%s" % (KEY_NAMES[c[0]].split("/")[0], c[1], c[2]))
def test_chords_recorded(live, rig, reference, report, case):
    key, scale, group = case
    shown = _configure(live, rig, reference, key, scale, group)
    routing, audio, clip, notes, beats = _record(live, rig)

    chords = reference["table"][(key, scale, group)]
    lines, wrong, bad_len = _chord_check(notes, beats, chords)
    rows = timing.note_offsets(notes, TEMPO, grid=1.0, sample_rate=audio.get("sample_rate"))
    summary = timing.summarize(rows, audio.get("output_buffer"))

    title = "%s via MIDI From track: %s %s, %s" % (PLUGIN, KEY_NAMES[key], scale, group)
    text = timing.format_table(title, rows, summary, TEMPO, audio)
    text += "\nplug-in shows: %s" % ", ".join("%s=%s" % kv for kv in shown.items())
    text += "\ninput: %s / %s" % (routing["current"]["type"], routing["current"]["channel"])
    text += "\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase1-vst3-%d-%s-%s" % (key, scale.replace(" ", ""), group.replace(" ", "")),
          {"case": case, "shown": shown, "routing": routing["current"],
           "audio": audio, "clip": clip, "rows": rows, "summary": summary, "wrong_beats": wrong})

    assert notes, "nothing recorded on track %d" % (rig["rec"] + 1)
    assert not wrong, "beats with the wrong chord: %s" % wrong
    assert not bad_len, "notes not an eighth long: %s" % bad_len
    assert max(abs(r["offset_ms"]) for r in rows) < 0.1, "VST3 route not sample-accurate"
