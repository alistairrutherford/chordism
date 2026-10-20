"""Phase 7 checks in Live (PLAN.md §7, row 7): Instance Sync, look-ahead and voice leading.

Uses the Phase 1 rig (tracks "Chordism P1 Src" / "Chordism P1 Rec", test_phase1_live.py) and a
second Chordism track, "Chordism P7 Follow" (made on first use). The Rec track records one
Chordism track through VST3 "MIDI From", which is sample-accurate. Everything is set through host
parameters, as LiveBridge requires.

    sync-*        two Chordism tracks in Sync group A. The leader plays the Test pattern (chord slot
                  b on beat b, an eighth long) in D Dorian; the follower's chord zone is muted and
                  its Test smart "Tones" presses Smart Scale key 3b mod 7 on each beat, with Note
                  repeat at 1/32 (gate 50%), SS octave +1. Its Key and Scale parameters must become
                  the leader's (D Dorian) once both are in the group, and every strike must be the
                  leader's chord's tone (Chord Tones): from the second strike on (1/32 note = 62.5
                  ms after the change, more than a block) always; the strike on the beat itself if
                  the leader's block ran first, else one block late (the chord before). Both ways
                  round (follow-p7: P7 follows P1 Src; follow-p1: the other way).
    sync-off      the control: the follower in no group, playing its own chords (C major) with
                  the chord zone on: its Smart Scale follows only those.
    sync-loop     follow-p7 on one bar looped (as look-ahead below) with Look-ahead 1/32 on the
                  follower: in pass 0 the strike on the beat may be a block late; from pass 1 on
                  look-ahead gives it the leader's chord on time (the learned map holds the
                  leader's stamped chords).
    look-ahead    one bar looped (arrangement loop 0-4) and recorded for four passes into a session
                  clip: chord slot b on beat b; Test smart "Tones early" presses the Smart key of
                  beat b a 1/64 note before the beat, held to b + 1/2. Pass 0 has nothing learned,
                  so each early key has the chord before; from pass 1 on, look-ahead (1/32) gives it
                  the upcoming chord.
    look-ahead-off  the same with Look-ahead Off: every early key has the chord before.
    voice-leading C major Triads with Voice leading Smooth: each beat's chord is the inversion
                  nearest the one before (a Python port of VoiceLeading.h), and differs from the
                  chord as written on some beats.

Every note must start and end within 0.1 ms of its time.
"""

import math
import time

import pytest

from test_phase1_live import (  # noqa: F401  (rig and reference are fixtures)
    BARS, FORMAT, KEY_NAMES, PLUGIN, TEMPO, _configure, _is_chordism, _record, _save, _set, reference, rig)
from test_phase3_live import DEFAULTS as CHORD_DEFAULTS, RATES, _apply
from test_phase4_live import SMART_DEFAULTS, TEST_SMART

from livebridge import livelog, timing

FOLLOW_NAME = "Chordism P7 Follow"
SYNC = ["Off", "A", "B", "C", "D"]
LOOK_AHEAD = ["Off", "1/128", "1/64", "1/32", "1/16", "1/8"]
VOICE_LEADING = ["Off", "Smooth"]
SCALE_NAMES = None                  # from the reference fixture

MAJOR = [0, 2, 4, 5, 7, 9, 11]
DORIAN = [0, 2, 3, 5, 7, 9, 10]
SS_C = 72                           # the Smart zone's C with SS octave +1
TOL_MS = 0.1
EARLY = 1.0 / 16.0                  # Tones early: a 1/64 note before the beat
LOOP_BEATS = 4
PASSES = 4

pytestmark = pytest.mark.live


def _scale_pitch(scale, i):
    return scale[i % 7] + 12 * (i // 7)


def _tone(key, scale, degree, letter):
    """Chord Tones: white-key letter j plays scale index d + 2j above the tonic, from SS_C."""
    return SS_C + key + _scale_pitch(scale, degree + 2 * letter)


def _letter(b):
    return (3 * b) % 7


@pytest.fixture(scope="module")
def follow(live, rig):
    """The second Chordism track, as a rig-like dict for _set (its "src" is the track)."""
    state = live.call("get_state")
    track = next((t for t in state["tracks"] if t["name"] == FOLLOW_NAME and any(_is_chordism(d) for d in t["devices"])), None)
    if track is None:
        track = live.call("create_midi_track", name=FOLLOW_NAME)
        live.load_device_and_wait(PLUGIN, track["index"], "plugins", FORMAT)
    index = track["index"]
    live.call("set_track", track=index, arm=False)
    device = next(d for d in live.call("list_devices", track=index) if _is_chordism(d))
    f = {"src": index, "rec": rig["rec"], "device": device["index"], "device_name": device["name"]}
    yield f
    for r in (f, rig):
        _set(live, r, "Sync group", 0, SYNC)
    _set(live, f, "Test pattern", False)


def _display(live, r, name):
    return live.call("get_parameter", track=r["src"], device=r["device"], parameter=name)["display"]


def _base(live, r, ref, key, scale, group="Triads"):
    """A Chordism track in a known state: _configure plus the Phase 3/4 defaults and Phase 7 off."""
    _configure(live, r, ref, key, scale, group)
    _set(live, r, "Progression", 0, [""] * 9)   # Diatonic
    _set(live, r, "Test shifter", 0, [""] * 6)  # Off
    _apply(live, r, CHORD_DEFAULTS)
    _apply(live, r, SMART_DEFAULTS)
    _set(live, r, "Look-ahead", LOOK_AHEAD.index("1/32"), LOOK_AHEAD)
    _set(live, r, "Voice leading", 0, VOICE_LEADING)


def _record_from(live, rig, source):
    """_record, with the Rec track's MIDI From set to `source` (a rig-like dict)."""
    return _record(live, dict(rig, src=source["src"], device=source["device"], device_name=source["device_name"]))


def _check_notes(notes, want, beats, tol_beats):
    """Pairs each beat's recorded notes with the expected (start, pitch, length) ones."""
    lines, wrong = [], []
    for b in range(int(beats)):
        w = sorted((x for x in want if b - 0.1 <= x[0] < b + 0.9), key=lambda x: (x[0], x[1]))
        g = sorted((n for n in notes if b - 0.1 <= n["song_time"] < b + 0.9), key=lambda n: (n["song_time"], n["pitch"]))
        bad = len(g) != len(w)
        worst = 0.0
        for n, (start, pitch, length) in zip(g, w):
            d_on = abs(n["song_time"] - start)
            d_off = abs(n["song_time"] + n["duration"] - (start + length))
            worst = max(worst, d_on, d_off)
            if n["pitch"] != pitch or d_on > tol_beats or d_off > tol_beats:
                bad = True
        if bad:
            wrong.append(b)
        lines.append("beat %2d  want %s\n         got  %s  (worst %.3f ms) %s" % (
            b, [(round(x[0] - b, 4), x[1]) for x in w], [(round(n["song_time"] - b, 4), n["pitch"]) for n in g],
            worst * 60000.0 / TEMPO, "WRONG" if bad else "ok"))
    return lines, wrong


# -- Instance Sync -------------------------------------------------------------------------

SYNC_CASES = [("follow-p7", "rig", "follow"), ("follow-p1", "follow", "rig"), ("sync-off", None, "follow")]


@pytest.mark.parametrize("case", SYNC_CASES, ids=lambda c: c[0])
def test_sync(live, rig, follow, reference, report, case):
    cid, leader_name, follower_name = case
    tracks = {"rig": rig, "follow": follow}
    follower = tracks[follower_name]
    leader = tracks[leader_name] if leader_name else tracks["rig" if follower_name == "follow" else "follow"]
    scales = reference["scales"]
    tempo_ms = 60000.0 / TEMPO
    tol_beats = TOL_MS / tempo_ms

    # Both out of any group first, so neither takes an old group's Key and Scale.
    for r in (leader, follower):
        _set(live, r, "Sync group", 0, SYNC)
    _base(live, leader, reference, 2, "Dorian")
    _base(live, follower, reference, 0, "Major")
    smart = {"Test smart": ("Tones", TEST_SMART), "Note repeat": (True, None), "Pattern slot": ("1", ["1", "2", "3", "4", "5"]),
             "Pattern 1 rate": ("1/32", RATES), "Pattern 1 gate": (50, (0, 100)), "SS octave": (1, (-3, 3)),
             "Look-ahead": ("Off", LOOK_AHEAD)}
    _apply(live, follower, smart)
    synced = cid != "sync-off"
    _set(live, follower, "Mute CG", synced)
    if synced:
        _set(live, leader, "Sync group", 1, SYNC)
        time.sleep(0.3)
        _set(live, follower, "Sync group", 1, SYNC)
        time.sleep(0.3)
        # A change after both joined is what the group shares (whatever an earlier run left in A).
        _set(live, leader, "Key", 2, KEY_NAMES)
        _set(live, leader, "Scale", scales.index("Dorian"), scales)
        time.sleep(0.5)
    shown = {"leader Key": _display(live, leader, "Key"), "leader Scale": _display(live, leader, "Scale"),
             "follower Key": _display(live, follower, "Key"), "follower Scale": _display(live, follower, "Scale"),
             "follower Sync group": _display(live, follower, "Sync group")}
    try:
        routing, audio, clip, notes, beats = _record_from(live, rig, follower)
    finally:
        for r in (leader, follower):
            _set(live, r, "Sync group", 0, SYNC)
            _set(live, r, "Mute CG", False)
        _apply(live, follower, {"Test smart": ("Off", TEST_SMART), "Note repeat": (False, None)})

    smart_notes = [n for n in notes if n["pitch"] >= SS_C]
    want, late_ok = [], {}
    for b in range(int(beats)):
        j = _letter(b)
        if synced:
            pitch, before = _tone(2, DORIAN, b % 7, j), _tone(2, DORIAN, (b - 1) % 7, j)
        else:
            pitch = before = _tone(0, MAJOR, b % 7, j)   # its own chords, C major
        for s in range(4):
            start = b + s / 8.0
            if start < beats - 0.5:
                want.append((start, pitch, 1 / 16.0))
        late_ok[b] = before

    # The strike on the beat may be one block late: it then has the chord before (any chord on beat 0).
    on_time, late, lines, wrong = 0, 0, [], []
    for b in range(int(beats)):
        w = sorted((x for x in want if b <= x[0] < b + 0.9), key=lambda x: x[0])
        g = sorted((n for n in smart_notes if b - 0.1 <= n["song_time"] < b + 0.9), key=lambda n: n["song_time"])
        bad = len(g) != len(w)
        worst = 0.0
        for k, (n, (start, pitch, length)) in enumerate(zip(g, w)):
            d_on = abs(n["song_time"] - start)
            d_off = abs(n["song_time"] + n["duration"] - (start + length))
            worst = max(worst, d_on, d_off)
            if d_on > tol_beats or d_off > tol_beats:
                bad = True
            if k == 0:
                if n["pitch"] == pitch:
                    on_time += 1
                elif synced and (n["pitch"] == late_ok[b] or b == 0):
                    late += 1
                else:
                    bad = True
            elif n["pitch"] != pitch:
                bad = True
        if bad:
            wrong.append(b)
        lines.append("beat %2d  want %s\n         got  %s  (worst %.3f ms) %s" % (
            b, [(round(x[0] - b, 4), x[1]) for x in w], [(round(n["song_time"] - b, 4), n["pitch"]) for n in g],
            worst * tempo_ms, "WRONG" if bad else "ok"))

    rows = timing.note_offsets(smart_notes, TEMPO, grid=1 / 32.0, sample_rate=audio.get("sample_rate"))
    summary = timing.summarize(rows, audio.get("output_buffer"))
    text = timing.format_table("%s Phase 7 %s (%d bars, grid 1/32)" % (PLUGIN, cid, BARS), rows, summary, TEMPO, audio)
    text += "\nplug-ins show: %s" % ", ".join("%s=%s" % kv for kv in shown.items())
    text += "\nstrikes on the beat: %d on time, %d one block late" % (on_time, late)
    text += "\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase7-%s" % cid, {"case": cid, "shown": shown, "audio": audio, "clip": clip, "rows": rows,
                              "wrong_beats": wrong, "on_time": on_time, "late": late})

    assert smart_notes, "nothing recorded from the follower's Smart Scale"
    if synced:
        assert shown["follower Key"] == KEY_NAMES[2] and shown["follower Scale"] == "Dorian", \
            "the follower's Key and Scale didn't become the leader's: %s" % shown
    assert not wrong, "beats with wrong notes or times: %s" % wrong


# -- Look-ahead on a loop -------------------------------------------------------------------

def _record_loop(live, rig, beats):
    """Records `beats` beats of the looped transport into a session clip on the Rec track
    (session clips run on in real time while the arrangement loops). Returns the notes, with
    `song_time` = beats since the recording started."""
    rec = rig["rec"]
    routing = live.call("set_input_routing", track=rec, source_track=rig["src"])
    names = [c["display_name"] for c in routing["available_channels"]]
    chan = next(n for n in names if n.lower() == rig["device_name"].lower())
    live.call("set_input_routing", track=rec, channel=chan)
    live.call("set_track", track=rec, arm=True, monitoring="auto")
    live.call("clear_session_clips", track=rec)
    live.call("stop_transport")
    live.call("set_song_properties", current_song_time=0.0)
    time.sleep(0.3)
    seconds = beats * 60.0 / TEMPO
    live.call("start_recording", mode="session", track=rec, scene=0, length_beats=beats)
    try:
        live.wait_until(lambda: live.call("get_state", include_devices=False)["is_playing"], 10.0, what="playback")
        time.sleep(seconds + 1.0)
        live.wait_until(lambda: not live.call("get_clip_notes", track=rec, source="session", scene=0).get("is_recording"),
                        10.0, what="the session clip to finish recording")
    finally:
        live.call("stop_recording", mode="session", stop_transport=True)
        live.call("set_track", track=rec, arm=False)
    clip = live.call("get_clip_notes", track=rec, source="session", scene=0)
    notes = [dict(n, song_time=n["start_time"]) for n in clip["notes"]]
    audio = livelog.audio_settings(live.call("ping")["live_version"])
    return audio, clip, notes


@pytest.mark.parametrize("window", ["1/32", "Off"], ids=["look-ahead", "look-ahead-off"])
def test_look_ahead(live, rig, reference, report, window):
    tempo_ms = 60000.0 / TEMPO
    tol_beats = TOL_MS / tempo_ms
    beats = LOOP_BEATS * PASSES
    _set(live, rig, "Sync group", 0, SYNC)
    _base(live, rig, reference, 0, "Major")
    shown = _apply(live, rig, {"Test smart": ("Tones early", TEST_SMART), "SS octave": (1, (-3, 3)),
                               "Look-ahead": (window, LOOK_AHEAD)})
    live.call("set_song_properties", loop=True, loop_start=0.0, loop_length=float(LOOP_BEATS))
    try:
        audio, clip, notes = _record_loop(live, rig, beats)
    finally:
        live.call("set_song_properties", loop=False)
        live.call("clear_session_clips", track=rig["rec"])
        live.call("set_song_properties", back_to_arranger=False)
        _apply(live, rig, {"Test smart": ("Off", TEST_SMART)})

    chords = reference["table"][(0, "Major", "Triads")]
    want = []
    for t in range(beats):
        b = t % LOOP_BEATS
        want += [(float(t), p, 0.5) for p in chords[b]]
        if t > 0:
            ahead = window != "Off" and t > LOOP_BEATS   # from pass 1 on (beat 4's key is pressed in pass 0)
            d = b if ahead else (b - 1) % LOOP_BEATS
            want.append((t - EARLY, _tone(0, MAJOR, d, _letter(b)), 0.5 + EARLY))
    # The last early key (for the pass after) is cut short when the recording ends.
    want = [w for w in want if w[0] < beats - 0.5]
    notes = [n for n in notes if n["song_time"] < beats - 0.5]
    lines, wrong = _check_notes(notes, want, beats, tol_beats)

    rows = timing.note_offsets(notes, TEMPO, grid=1 / 64.0, sample_rate=audio.get("sample_rate"))
    summary = timing.summarize(rows, audio.get("output_buffer"))
    text = timing.format_table("%s Phase 7 look-ahead %s (1-bar loop x %d, grid 1/64)" % (PLUGIN, window, PASSES),
                               rows, summary, TEMPO, audio)
    text += "\nplug-in shows: %s" % ", ".join("%s=%s" % kv for kv in shown.items())
    text += "\nclip: length %s, %d notes" % (clip.get("length"), len(clip["notes"]))
    text += "\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase7-look-ahead-%s" % window.replace("/", "-"), {"window": window, "shown": shown, "audio": audio, "clip": clip,
                                                               "rows": rows, "wrong_beats": wrong})

    assert notes, "nothing recorded"
    assert not wrong, "beats with wrong notes or times: %s" % wrong


def test_sync_loop(live, rig, follow, reference, report):
    leader, follower = rig, follow
    scales = reference["scales"]
    tol_beats = TOL_MS / (60000.0 / TEMPO)
    beats = LOOP_BEATS * PASSES
    for r in (leader, follower):
        _set(live, r, "Sync group", 0, SYNC)
    _base(live, leader, reference, 2, "Dorian")
    _base(live, follower, reference, 0, "Major")
    _apply(live, follower, {"Test smart": ("Tones", TEST_SMART), "Note repeat": (True, None),
                            "Pattern slot": ("1", ["1", "2", "3", "4", "5"]), "Pattern 1 rate": ("1/32", RATES),
                            "Pattern 1 gate": (50, (0, 100)), "SS octave": (1, (-3, 3)), "Look-ahead": ("1/32", LOOK_AHEAD)})
    _set(live, follower, "Mute CG", True)
    _set(live, leader, "Sync group", 1, SYNC)
    time.sleep(0.3)
    _set(live, follower, "Sync group", 1, SYNC)
    time.sleep(0.3)
    _set(live, leader, "Key", 2, KEY_NAMES)
    _set(live, leader, "Scale", scales.index("Dorian"), scales)
    time.sleep(0.5)
    live.call("set_song_properties", loop=True, loop_start=0.0, loop_length=float(LOOP_BEATS))
    try:
        audio, clip, notes = _record_loop(live, dict(rig, src=follower["src"], device=follower["device"],
                                                     device_name=follower["device_name"]), beats)
    finally:
        live.call("set_song_properties", loop=False)
        live.call("clear_session_clips", track=rig["rec"])
        live.call("set_song_properties", back_to_arranger=False)
        for r in (leader, follower):
            _set(live, r, "Sync group", 0, SYNC)
            _set(live, r, "Mute CG", False)
        _apply(live, follower, {"Test smart": ("Off", TEST_SMART), "Note repeat": (False, None)})

    notes = [n for n in notes if n["pitch"] >= SS_C and n["song_time"] < beats - 0.5]
    lines, wrong, late = [], [], []
    for t in range(beats):
        b = t % LOOP_BEATS
        j = _letter(b)
        pitch, before = _tone(2, DORIAN, b, j), _tone(2, DORIAN, (b - 1) % LOOP_BEATS, j)
        w = [(t + s / 8.0, pitch, 1 / 16.0) for s in range(4) if t + s / 8.0 < beats - 0.5]
        g = sorted((n for n in notes if t - 0.1 <= n["song_time"] < t + 0.9), key=lambda n: n["song_time"])
        bad = len(g) != len(w)
        for k, (n, (start, p, length)) in enumerate(zip(g, w)):
            if abs(n["song_time"] - start) > tol_beats or abs(n["song_time"] + n["duration"] - start - length) > tol_beats:
                bad = True
            if n["pitch"] != p:
                if k == 0 and t < LOOP_BEATS and (n["pitch"] == before or t == 0):
                    late.append(t)        # pass 0: nothing learned yet, a block late is allowed
                else:
                    bad = True
        if bad:
            wrong.append(t)
        lines.append("beat %2d (pass %d)  want %s\n                   got  %s %s" % (
            t, t // LOOP_BEATS, [(round(x[0] - t, 4), x[1]) for x in w], [(round(n["song_time"] - t, 4), n["pitch"]) for n in g],
            "WRONG" if bad else ("late (pass 0)" if t in late else "ok")))

    text = "%s Phase 7 sync-loop: follower first strikes late in pass 0 on beats %s; from pass 1 on every strike on time" % (
        PLUGIN, late)
    text += "\nclip: length %s, %d notes; audio %s" % (clip.get("length"), len(clip["notes"]), audio)
    text += "\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase7-sync-loop", {"audio": audio, "clip": clip, "wrong_beats": wrong, "late": late})

    assert notes, "nothing recorded from the follower's Smart Scale"
    assert not wrong, "beats with wrong notes or times: %s" % wrong


# -- Voice leading ---------------------------------------------------------------------------

def _distance(a, b):
    return sum(min(abs(x - y) for y in b) for x in a) + sum(min(abs(x - y) for x in a) for y in b)


def lead_voicing(chord, previous, key, octave=0):
    """VoiceLeading.h in Python: the inversion (bass in [A - 6, A + 6), A = 48 + key + 12 octave)
    nearest `previous` (or the chord as written); ties: nearest as written, then lower bass."""
    w = sorted(chord)
    target = previous or w
    lo = 48 + key + 12 * octave - 6
    best = None
    for i in range(len(w)):
        notes = w[i:]
        used = set(notes)
        for k in range(i):
            p = w[k] + 12
            while p <= w[i] or p in used:
                p += 12
            used.add(p)
            notes.append(p)
        shift = 12 * math.ceil((lo - w[i]) / 12.0)
        c = sorted(n + shift for n in notes)
        if c[0] < 0 or c[-1] > 127:
            continue
        score = (_distance(c, target), _distance(c, w), c[0])
        if best is None or score < best[0]:
            best = (score, c)
    return best[1] if best else w


def test_voice_leading(live, rig, reference, report):
    _set(live, rig, "Sync group", 0, SYNC)
    _base(live, rig, reference, 0, "Major")
    shown = _apply(live, rig, {"Voice leading": ("Smooth", VOICE_LEADING)})
    try:
        routing, audio, clip, notes, beats = _record(live, rig)
    finally:
        _apply(live, rig, {"Voice leading": ("Off", VOICE_LEADING)})

    chords = reference["table"][(0, "Major", "Triads")]
    tol_beats = TOL_MS / (60000.0 / TEMPO)
    want, prev, moved = [], None, 0
    for b in range(int(beats)):
        led = lead_voicing(chords[b % 7], prev, 0)
        prev = led
        moved += 1 if led != sorted(chords[b % 7]) else 0
        want += [(float(b), p, 0.5) for p in led]
    lines, wrong = _check_notes(notes, want, beats, tol_beats)
    rows = timing.note_offsets(notes, TEMPO, grid=1.0, sample_rate=audio.get("sample_rate"))
    summary = timing.summarize(rows, audio.get("output_buffer"))
    text = timing.format_table("%s Phase 7 voice leading Smooth, C major Triads (%d bars)" % (PLUGIN, BARS), rows, summary, TEMPO, audio)
    text += "\nplug-in shows: %s; %d of %d beats voiced differently from the chord as written" % (
        ", ".join("%s=%s" % kv for kv in shown.items()), moved, int(beats))
    text += "\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase7-voice-leading", {"shown": shown, "audio": audio, "clip": clip, "rows": rows, "wrong_beats": wrong, "moved": moved})

    assert notes, "nothing recorded"
    assert moved > 0, "voice leading changed nothing"
    assert not wrong, "beats with wrong voicings or times: %s" % wrong
