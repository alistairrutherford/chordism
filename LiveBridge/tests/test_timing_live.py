"""Phase 1 timing check (PLAN.md §3), run against a live Live 12.

Track "Chordism Src": the Chordism VST3. Track "Chordism Rec": an instrument, MIDI From either
the Src track / Chordism (route "vst3") or the CoreMIDI virtual port (route "port"). The Test
pattern plays a chord on every beat; 4 bars are recorded into the arrangement on Rec and each
note's offset from the beat is reported in beats and ms.

Settings (environment variables):
    CHORDISM_PLUGIN        plug-in name in Live's browser     (default "Chordism Spike")
    CHORDISM_FORMAT        vst3 | au | vst                    (default vst3)
    CHORDISM_TEST_PARAM    name of the Test pattern parameter (default "Test pattern")
    CHORDISM_PORT          virtual port name prefix           (default "<plugin> Out")
    CHORDISM_INSTRUMENT    Live instrument for the Rec track  (default "Drift")
    CHORDISM_TEMPOS        comma-separated BPM                (default "120")
    CHORDISM_BARS          bars to record                     (default 4)
    CHORDISM_MAX_OFFSET_MS fail if any |offset| exceeds this  (default: report only)

Tracks are reused when they already exist, so a set where the plug-in's Test pattern was switched
on by hand (the Phase 0 spike has no host parameter for it) works too.
"""

import datetime
import json
import os
import time

import pytest

from livebridge.client import BridgeError
from livebridge import livelog, timing

PLUGIN = os.environ.get("CHORDISM_PLUGIN", "Chordism Spike")
FORMAT = os.environ.get("CHORDISM_FORMAT", "vst3")
TEST_PARAM = os.environ.get("CHORDISM_TEST_PARAM", "Test pattern")
PORT = os.environ.get("CHORDISM_PORT", PLUGIN + " Out")
INSTRUMENT = os.environ.get("CHORDISM_INSTRUMENT", "Drift")
TEMPOS = [float(t) for t in os.environ.get("CHORDISM_TEMPOS", "120").split(",") if t.strip()]
BARS = int(os.environ.get("CHORDISM_BARS", "4"))
MAX_OFFSET_MS = os.environ.get("CHORDISM_MAX_OFFSET_MS")

SRC_NAME = "Chordism Src"
REC_NAME = "Chordism Rec"
REPORT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "reports")

pytestmark = pytest.mark.live


def _find_track(state, pred):
    for t in state["tracks"]:
        if pred(t):
            return t
    return None


def _has_plugin(t):
    return any(PLUGIN.lower() in d["name"].lower() for d in t.get("devices", []))


@pytest.fixture(scope="module")
def rig(live):
    """Find or build the two tracks. Returns a dict with track indices and how the pattern is on."""
    live.call("stop_transport")
    state = live.call("get_state")

    src = _find_track(state, lambda t: t["name"] == SRC_NAME and _has_plugin(t)) \
        or _find_track(state, lambda t: t["is_midi"] and _has_plugin(t))
    if src is None:
        src = live.call("create_midi_track", name=SRC_NAME)
        live.load_device_and_wait(PLUGIN, src["index"], "plugins", FORMAT)
    src_i = src["index"]

    state = live.call("get_state")
    rec = _find_track(state, lambda t: t["name"] == REC_NAME and t["is_midi"])
    if rec is None:
        rec = live.call("create_midi_track", name=REC_NAME)
        live.load_device_and_wait(INSTRUMENT, rec["index"], "instruments")
    rec_i = rec["index"]

    devices = live.call("list_devices", track=src_i)
    plugin_dev = next(d for d in devices if PLUGIN.lower() in d["name"].lower())

    try:
        live.call("set_parameter", track=src_i, device=plugin_dev["index"], parameter=TEST_PARAM, value=True)
        pattern = "parameter %r on" % TEST_PARAM
    except BridgeError as e:
        pattern = None
        param_error = str(e)

    live.call("set_track", track=src_i, arm=False)
    live.call("set_song_properties", loop=False, arrangement_overdub=False,
              midi_recording_quantization="none")

    rig = {"src": src_i, "rec": rec_i, "device": plugin_dev, "pattern": pattern}
    if pattern is None:
        rig["pattern_note"] = (
            "%s has no host parameter %r (%s). Using the plug-in's own setting: open %s on track %d "
            "and switch Test pattern on if no notes are recorded."
            % (plugin_dev["name"], TEST_PARAM, param_error, plugin_dev["name"], src_i + 1))
    yield rig
    live.call("stop_transport")
    live.call("set_track", track=rec_i, arm=False)


def _route(live, rig, route):
    rec, src = rig["rec"], rig["src"]
    if route == "vst3":
        r = live.call("set_input_routing", track=rec, source_track=src)
        names = [c["display_name"] for c in r["available_channels"]]
        # The plug-in's own output is listed by device name, next to "Pre FX" / "Post FX".
        chan = next((n for n in names if n.lower() == rig["device"]["name"].lower()), None) \
            or next((n for n in names if PLUGIN.lower() in n.lower()), None)
        if chan is None:
            pytest.fail("Live lists no MIDI From channel for %s on track %d; channels: %s"
                        % (PLUGIN, src + 1, names))
        return live.call("set_input_routing", track=rec, channel=chan)
    r = live.call("get_input_routing", track=rec)
    types = [t["display_name"] for t in r["available_types"]]
    port = next((t for t in types if t.lower().startswith(PORT.lower())), None)
    if port is None:
        pytest.skip("no MIDI input %r* in Live. Turn on its Track switch in Settings > "
                    "Link, Tempo & MIDI. Inputs: %s" % (PORT, types))
    r = live.call("set_input_routing", track=rec, type=port)
    chans = [c["display_name"] for c in r["available_channels"]]
    if "All Channels" in chans:
        r = live.call("set_input_routing", track=rec, channel="All Channels")
    return r


@pytest.mark.parametrize("tempo", TEMPOS, ids=lambda t: "%gbpm" % t)
@pytest.mark.parametrize("route", ["vst3", "port"])
def test_recorded_offset_from_beat(live, rig, report, route, tempo):
    rec = rig["rec"]
    routing = _route(live, rig, route)
    live.call("set_tempo", bpm=tempo)
    live.call("set_track", track=rec, arm=True, monitoring="auto")
    try:
        live.call("clear_arrangement_clips", track=rec)
    except BridgeError:
        pass  # older Live without Track.delete_clip: the new take replaces the old one anyway

    # Preflight: does anything reach the Rec track's instrument while the transport runs?
    live.call("start_transport", from_beat=0)
    peak = live.peak_meter(rec, 1.5)
    live.call("stop_transport")
    time.sleep(0.3)

    state = live.call("get_state", include_devices=False)
    beats = BARS * state["signature"][0] * 4.0 / state["signature"][1]
    clip = live.record_arrangement(rec, beats, from_beat=0.0)
    live.call("set_track", track=rec, arm=False)

    # Live logs its audio settings on every change; the last block is what this take used.
    audio = livelog.audio_settings(live.call("ping")["live_version"])
    notes = [n for n in clip["notes"] if -0.5 <= n["song_time"] < beats - 0.5]
    rows = timing.note_offsets(notes, tempo, grid=1.0, sample_rate=audio.get("sample_rate"))
    summary = timing.summarize(rows, audio.get("output_buffer"))
    missing = timing.missing_beats(rows, 0.0, beats - 1.0)

    title = "%s via %s, %d bars" % (PLUGIN, "MIDI From track" if route == "vst3" else "virtual port", BARS)
    text = timing.format_table(title, rows, summary, tempo, audio)
    text += "\ninput: %s / %s; preflight meter peak %.3f; pattern: %s" % (
        routing["current"]["type"], routing["current"]["channel"], peak,
        rig["pattern"] or "set by hand in the plug-in")
    if missing:
        text += "\nbeats with no note: %s" % missing
    report(text)
    print(text)

    os.makedirs(REPORT_DIR, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    with open(os.path.join(REPORT_DIR, "timing-%s-%g-%s.json" % (route, tempo, stamp)), "w") as f:
        json.dump({"route": route, "tempo": tempo, "bars": BARS, "plugin": PLUGIN,
                   "routing": routing["current"], "audio": audio, "preflight_peak": peak, "clip": clip,
                   "rows": rows, "summary": summary, "missing_beats": missing}, f, indent=2)

    if not rows:
        pytest.fail("no notes recorded on track %d. %s" % (
            rec + 1, rig.get("pattern_note", "Is the Test pattern on?")))
    worst = max(abs(r["offset_beats"]) for r in rows)
    assert worst < 0.25, "a note is more than 1/16 from its beat; the grid match is unreliable"
    if MAX_OFFSET_MS:
        assert max(abs(r["offset_ms"]) for r in rows) <= float(MAX_OFFSET_MS)
