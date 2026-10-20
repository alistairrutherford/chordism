"""Phase 8 checks in Live (PLAN.md §7 row 8, §8): CPU with 8 instances, and notes held when a
Chordism device is switched off or deleted.

    cpu           eight tracks "Chordism P8 1" … "Chordism P8 8" (made on first use, deleted at the
                  end), each with Chordism. Live's CPU meter (Application.average_process_usage, in
                  percent, through the bridge's get_cpu) and Live's process CPU time (ps) are sampled
                  for SECONDS with the transport playing, in three states: every Chordism switched off
                  (Device On), on and idle, and on with everything at once (Thirteenths, Stack
                  +1&2, Arp Boomerang Up 1/32T over 3 octaves, Repeat, Step seq, Velocity, Hold
                  Bar, Voice leading, Note repeat, SS stack, SS step seq, SS hold, Melody, the test
                  pattern on both zones, the eight in Sync groups A-D). Each instance's share is
                  (state − off) / 8, and must be under 1% on Live's meter (the process time,
                  which includes the plug-in windows Live opens, is reported).
    held-*        the Phase 1 rig (Chordism P1 Src → Chordism P1 Rec, Drift) records the Test
                  pattern at 60 BPM with Hold Latch, so each chord sounds until the next beat. At beat 6.25
                  Chordism is switched off: by its own Enable (enable), by Live's Device On
                  (device-off), or by deleting the device (delete; loaded again after). The chord
                  of beat 6 must end within REACT beats of the switch, not ring to the end of the
                  take, and nothing may start after it. Live stops calling a device that is off,
                  so nothing can end its notes then: device-off stops the test pattern, turns the
                  device on again at beat 8.6, and the chord must end within REACT of that.
"""

import os
import subprocess
import time

import pytest

from test_phase1_live import (  # noqa: F401  (rig and reference are fixtures)
    FORMAT, PLUGIN, TEMPO, _configure, _is_chordism, _route, _save, _set, reference, rig)
from test_phase3_live import ARP, DEFAULTS as CHORD_DEFAULTS, HOLD, RATES, STACK, VELOCITY, _apply
from test_phase4_live import PATTERN_ARP, SMART_DEFAULTS, TEST_SMART

from livebridge import livelog

INSTANCES = 8
TRACK_PREFIX = "Chordism P8 "
SECONDS = float(os.environ.get("CHORDISM_CPU_SECONDS", "15"))
SETTLE = 3.0
SYNC = ["Off", "A", "B", "C", "D"]
LOOK_AHEAD = ["Off", "1/128", "1/64", "1/32", "1/16", "1/8"]
VOICE_LEADING = ["Off", "Smooth"]
GROUPS = None                       # from the reference fixture

HELD_TEMPO = 60.0                   # so the bridge's ~0.35 s to act is about a third of a beat
SWITCH_AT = 6.25                    # beats into the take
BACK_ON_AT = 8.6                    # device-off: the device on again
TAKE = 12.0
REACT = 0.25                        # beats: 250 ms at HELD_TEMPO (ten blocks of 1024 samples)

pytestmark = pytest.mark.live


# -- CPU ------------------------------------------------------------------------------------

def _live_pid():
    out = subprocess.run(["pgrep", "-x", "Live"], capture_output=True, text=True).stdout.split()
    return int(out[0]) if out else None


def _cpu_seconds(pid):
    """User + system CPU time of a process, from ps ([[dd-]hh:]mm:ss.ss)."""
    text = subprocess.run(["ps", "-o", "time=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    days = 0
    if "-" in text:
        d, text = text.split("-", 1)
        days = int(d)
    parts = [float(x) for x in text.split(":")]
    seconds = 0.0
    for p in parts:
        seconds = seconds * 60.0 + p
    return days * 86400.0 + seconds


def _measure(live, pid, seconds, profile=None):
    """Mean of Live's CPU meter (percent: Live 12.4.6 reports about 0.6 for its empty default
    set) and Live's process CPU (cores) over `seconds`."""
    time.sleep(SETTLE)
    meter, peaks = [], []
    # CHORDISM_SAMPLE=<file>: a call-tree profile of Live (macOS `sample`) during the measurement.
    sampler = subprocess.Popen(["sample", str(pid), str(int(seconds) - 2), "-file", profile],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) if profile else None
    c0, t0 = _cpu_seconds(pid), time.monotonic()
    while time.monotonic() - t0 < seconds:
        r = live.call("get_cpu")
        if r["average"] is not None:
            meter.append(r["average"])
            peaks.append(r["peak"])
        time.sleep(0.25)
    process = (_cpu_seconds(pid) - c0) / (time.monotonic() - t0)
    if sampler:
        sampler.wait()
    return {"meter": sum(meter) / len(meter) if meter else None, "meter_peak": max(peaks) if peaks else None,
            "process": process, "samples": len(meter)}


@pytest.fixture(scope="module")
def eight(live, reference):
    """Eight Chordism tracks (rig-like dicts), made if missing; deleted afterwards."""
    global GROUPS
    GROUPS = reference["groups"]
    live.call("stop_transport")
    made = []
    for n in range(1, INSTANCES + 1):
        name = TRACK_PREFIX + str(n)
        state = live.call("get_state")
        track = next((t for t in state["tracks"] if t["name"] == name and any(_is_chordism(d) for d in t["devices"])), None)
        if track is None:
            track = live.call("create_midi_track", name=name)
            live.load_device_and_wait(PLUGIN, track["index"], "plugins", FORMAT)
        made.append(name)
    rigs = []
    for name in made:
        index = next(t["index"] for t in live.call("get_state")["tracks"] if t["name"] == name)
        live.call("set_track", track=index, arm=False)
        device = next(d for d in live.call("list_devices", track=index) if _is_chordism(d))
        rigs.append({"src": index, "device": device["index"], "device_name": device["name"], "name": name})
    yield rigs
    live.call("stop_transport")
    for name in reversed(made):
        live.call("delete_track", track=name)


HEAVY = {"Stack": ("+1&2", STACK), "Arp": ("Boomerang Up", ARP), "Arp rate": ("1/32T", RATES), "Arp range": (3, (1, 3)),
         "Repeat": (True, None), "Step seq": (True, None), "Seq sustain": (True, None), "Velocity": ("House", VELOCITY),
         "Hold": ("Bar", HOLD), "Note repeat": (True, None), "SS stack": ("+1&2", STACK), "SS step seq": (True, None),
         "SS hold": ("Latch", HOLD), "Pattern 1 arp": ("Melody", PATTERN_ARP), "Test smart": ("Pattern 3", TEST_SMART)}


def _heavy(live, r, i):
    _set(live, r, "Chord Group", GROUPS.index("Thirteenths"), GROUPS)
    _apply(live, r, HEAVY)
    _set(live, r, "Voice leading", 1, VOICE_LEADING)
    _set(live, r, "Look-ahead", LOOK_AHEAD.index("1/8"), LOOK_AHEAD)
    _set(live, r, "Sync group", 1 + i % 4, SYNC)
    _set(live, r, "Test pattern", True)


def _idle(live, r):
    _apply(live, r, CHORD_DEFAULTS)
    _apply(live, r, SMART_DEFAULTS)
    _set(live, r, "Sync group", 0, SYNC)
    _set(live, r, "Test pattern", False)
    _set(live, r, "Enable", True)


def test_cpu(live, eight, report):
    pid = _live_pid()
    assert pid is not None, "no Live process"
    audio = livelog.audio_settings(live.call("ping")["live_version"])
    live.call("set_song_properties", loop=True, loop_start=0.0, loop_length=8.0)
    live.call("set_tempo", bpm=TEMPO)
    results = {}
    try:
        live.call("start_transport", from_beat=0.0)
        for r in eight:
            _set(live, r, "Device On", False)
        results["off"] = _measure(live, pid, SECONDS)
        for r in eight:
            _idle(live, r)
            _set(live, r, "Device On", True)
        results["idle"] = _measure(live, pid, SECONDS)
        for i, r in enumerate(eight):
            _heavy(live, r, i)
        results["heavy"] = _measure(live, pid, SECONDS, os.environ.get("CHORDISM_SAMPLE"))
    finally:
        for r in eight:
            _idle(live, r)
        live.call("stop_transport")
        live.call("set_song_properties", loop=False)

    lines = ["Phase 8 CPU: %d Chordism instances, %s, %.0f s per state" % (INSTANCES, audio, SECONDS),
             "  state   Live meter (avg / peak)   Live process (cores)   per instance: meter / process"]
    per = {}
    for state in ("off", "idle", "heavy"):
        m = results[state]
        meter = m["meter"]
        dm = None if meter is None or results["off"]["meter"] is None else (meter - results["off"]["meter"]) / INSTANCES
        dp = (m["process"] - results["off"]["process"]) / INSTANCES
        per[state] = (dm, dp)
        lines.append("  %-6s  %s   %6.3f              %s / %+.4f%%" % (
            state, "n/a" if meter is None else "%6.3f%% / %6.3f%%" % (meter, m["meter_peak"]),
            m["process"], "n/a" if dm is None else "%+.4f%%" % dm, 100 * dp))
    report("\n".join(lines))
    _save("phase8-cpu", {"audio": str(audio), "results": results, "per_instance": per})
    # The exit criterion is Live's own CPU meter: the audio processing, which is what a set
    # pays per instance. Live's process time also counts the plug-in windows Live opens when it
    # loads a device ("Auto-Open Plug-In Windows"): a `sample` profile (CHORDISM_SAMPLE) shows
    # Chordism's editor painting, not processBlock, behind most of it (PLAN.md §12, Phase 8), so
    # it is reported, not asserted.
    assert results["off"]["meter"] is not None, "this Live has no average_process_usage"
    for state in ("idle", "heavy"):
        dm, dp = per[state]
        assert dm < 1.0, "%s: %.3f%% of Live's meter per instance" % (state, dm)


# -- Notes held when Chordism goes away ------------------------------------------------------

@pytest.mark.parametrize("how", ["enable", "device-off", "delete"])
def test_held_notes_end(live, rig, reference, report, how):
    _configure(live, rig, reference, 0, "Major", "Triads")
    _set(live, rig, "Progression", 0, [""] * 9)
    _set(live, rig, "Test shifter", 0, [""] * 6)
    _apply(live, rig, CHORD_DEFAULTS)
    _apply(live, rig, SMART_DEFAULTS)
    _set(live, rig, "Hold", HOLD.index("Latch"), HOLD)
    rec = rig["rec"]
    _route(live, rig)
    live.call("set_tempo", bpm=HELD_TEMPO)
    live.call("set_track", track=rec, arm=True, monitoring="auto")
    live.call("clear_arrangement_clips", track=rec)
    time.sleep(0.3)
    switched = back = None
    try:
        live.call("start_recording", mode="arrangement", from_beat=0.0)
        live.wait_song_time(SWITCH_AT, 30.0)
        if how == "enable":
            _set(live, rig, "Enable", False)
        elif how == "device-off":
            _set(live, rig, "Device On", False)
        else:
            live.call("delete_device", track=rig["src"], device=rig["device"])
        switched = live.call("get_state", include_devices=False)["current_song_time"]
        if how == "device-off":
            # Live stops calling a device that is off, so Chordism can't end its notes then; it
            # ends them on the first block after the device is on again (reset()). The test
            # pattern goes off first, so only the latched chord could still sound.
            _set(live, rig, "Test pattern", False)
            live.wait_song_time(BACK_ON_AT, 30.0)
            _set(live, rig, "Device On", True)
            back = live.call("get_state", include_devices=False)["current_song_time"]
        live.wait_song_time(TAKE, 30.0)
    finally:
        live.call("stop_recording", mode="arrangement", stop_transport=True)
        try:
            live.call("set_track", track=rec, arm=False)
        except Exception:
            pass                                    # no input left to arm (the device was deleted)
        if how == "delete":
            live.load_device_and_wait(PLUGIN, rig["src"], "plugins", FORMAT)
            device = next(d for d in live.call("list_devices", track=rig["src"]) if _is_chordism(d))
            rig["device"] = device["index"]
            _route(live, rig)
        _set(live, rig, "Device On", True)
        _set(live, rig, "Enable", True)
        _set(live, rig, "Hold", 0, HOLD)
        _set(live, rig, "Test pattern", False)
        live.call("set_tempo", bpm=TEMPO)

    clip = live.call("get_clip_notes", track=rec, source="arrangement")
    notes = clip["notes"]
    _save("phase8-held-" + how, {"switched_at": switched, "back_on_at": back, "clip": clip})
    last = [n for n in notes if 5.5 <= n["song_time"] < 6.2]
    later = [n for n in notes if n["song_time"] >= SWITCH_AT + REACT]
    ends = [n["song_time"] + n["duration"] for n in last]
    report("Phase 8 held notes (%s): switched at beat %.3f%s; the chord of beat 6 (%s) ends at %s; %d notes start after"
           % (how, switched or -1, "" if back is None else ", on again at %.3f" % back, [n["pitch"] for n in last],
              ["%.3f" % e for e in ends], len(later)))
    assert last, "no chord on beat 6"
    assert not later, "notes start after the switch: %s" % [(n["pitch"], n["song_time"]) for n in later]
    limit = (back if back is not None else switched) + REACT
    assert all(e <= limit for e in ends), "the chord of beat 6 rings on (ends %s, limit %.3f)" % (ends, limit)
