# LiveBridge: Live-in-the-loop tests

Drives Ableton Live 12 from tests and from Claude. Covers only behaviour that needs the
real host. Offline checks stay in `Tests/run_tests.sh`, `auval` and `pluginval`.

```
pytest / MCP server / CLI  ──TCP 127.0.0.1:9871──▶  ChordismBridge Remote Script (inside Live)
   (livebridge/)               JSON lines            polled from Live's update tick → LOM
```

| Path | What it is |
|---|---|
| `RemoteScript/ChordismBridge/` | The Live control surface. `server.py` is the socket (no Live imports), `commands.py` the LOM commands. |
| `install_remote_script.sh` | Symlinks it into `~/Music/Ableton/User Library/Remote Scripts/`. |
| `livebridge/client.py` | Python client, plus helpers (load and wait, record N beats, sample meters). |
| `livebridge/timing.py` | Offset of each note from the beat. |
| `livebridge/livelog.py` | Sample rate and buffer size, read from Live's `Log.txt`. |
| `livebridge/mcp_server.py` | MCP server (stdio) with the same commands as tools. Registered in the repo's `.mcp.json` as `live-bridge`. |
| `tests/test_offline.py` | Protocol and commands against a fake LOM, and the timing math. No Live needed. |
| `tests/test_timing_live.py` | The Phase 1 timing check (PLAN.md §3), against a running Live. |
| `scripts/restart_live.sh` | Quits Live and reopens it with a set, then waits for the bridge. |

## Setup

Needs [uv](https://docs.astral.sh/uv/). Live 12 runs Remote Scripts on its own Python 3.11.

```sh
cd LiveBridge
uv sync                      # .venv with mcp and pytest
./install_remote_script.sh   # symlink (use --copy if Live ignores the link)
```

Then restart Live and, in **Settings → Link, Tempo & MIDI → Control Surface**, pick
**ChordismBridge** in an empty row. Leave its Input and Output on *None*. Live shows
*"ChordismBridge listening on port 9871"* in the status bar, and the script logs to
`~/Library/Preferences/Ableton/Live 12.x.x/Log.txt` (search for `ChordismBridge:`).

Check it:

```sh
uv run python -m livebridge ping
uv run python -m livebridge get_state
uv run python -m livebridge set_tempo '{"bpm": 97}'
```

## Running the tests

```sh
uv run pytest tests/test_offline.py        # no Live needed
uv run pytest tests/test_timing_live.py    # Live running with the control surface selected
```

The live tests skip when the bridge doesn't answer. The timing report is printed at the end
of the run (with or without `-s`). Each run is also saved as JSON in `reports/` (gitignored),
with the whole clip.

### The timing check (`test_timing_live.py`)

1. Finds or creates track **Chordism Src** with the Chordism VST3, and **Chordism Rec** with
   an instrument (Drift). Existing tracks are reused, so the same set can be run again.
2. Switches on the **Test pattern** parameter (I IV V vi, a chord on every beat). Turns off
   arrangement loop, overdub and record quantization, and disarms Src.
3. Route `vst3`: Rec's MIDI From = Src track / the plug-in's own channel. Route `port`: Rec's
   MIDI From = the CoreMIDI virtual port (`<plugin> Out n`).
4. Arms Rec. Plays 1.5 s first and samples Rec's output meter (a routing sanity check, in the report).
5. Records 4 bars from bar 1 into the arrangement, then reads the clip with `get_notes_extended`.
6. Prints each note's beat, time, and offset (positive = late) in beats, ms and samples, with
   the mean, median, min, max, spread and missing beats. It also shows the mean offset as whole
   buffers plus a remainder (`mean = 1.53 x buffer (128) = 1 buffer(s) + 68.0 samples`), so a
   delay that scales with the buffer size is easy to spot.

The sample rate and buffer size come from Live's `Log.txt`
(`~/Library/Preferences/Ableton/Live <version>/Log.txt`). Live writes an `Audio In Out:` block
there at startup and whenever Settings → Audio changes, and the last block is the current setting.
They're in the report header and in the JSON (`audio`). If the log has no such block, the report
says so and leaves out the samples.

The test fails if nothing was recorded, or a note is more than 1/16 from its beat. It only
measures; set `CHORDISM_MAX_OFFSET_MS` to make it also fail above a limit.

| Variable | Default | |
|---|---|---|
| `CHORDISM_PLUGIN` | `Chordism Spike` | Name in Live's browser. Use `Chordism` once Phase 1 has built it. |
| `CHORDISM_FORMAT` | `vst3` | `vst3`, `au` or `vst` |
| `CHORDISM_TEST_PARAM` | `Test pattern` | Host parameter that switches the pattern on |
| `CHORDISM_PORT` | `<plugin> Out` | Virtual port name prefix |
| `CHORDISM_INSTRUMENT` | `Drift` | Live instrument for the Rec track |
| `CHORDISM_TEMPOS` | `120` | e.g. `120,97,174` |
| `CHORDISM_BARS` | `4` | |
| `CHORDISM_MAX_OFFSET_MS` | none | |

**The Phase 0 spike has no host parameters**: Test pattern is a button in its window, so the bridge
can't switch it on. With the spike, run the test once to create the tracks, open Chordism Spike on
track *Chordism Src*, click **Test pattern** on, and run again. Save the set so it stays on.
The Phase 1 plug-in should expose Test pattern as a parameter.

**The port route** needs the port's **Track** switch on in Settings → Link, Tempo & MIDI → MIDI
Ports (Input: `Chordism Spike Out 1`). Without it the port test is skipped, and the message
lists the inputs Live offers.

## Restarting Live (new plug-in builds)

Live keeps a plug-in binary loaded until it quits, and loads Remote Scripts only at startup. After
rebuilding the plug-in or editing the Remote Script:

```sh
LiveBridge/scripts/restart_live.sh "path/to/test set.als"
```

which does:

```sh
osascript -e 'quit app "Live"'                 # Live asks to save if the set changed; answer it
open -a "Ableton Live 12 Standard" "path/to/test set.als"
uv run python -m livebridge wait --timeout 120 # until the bridge answers
```

## Manual steps (not in the Live Object Model)

- **Saving and reopening a set.** Use File → Save, then the restart script, or File → Open.
- **Audio buffer size** (Settings → Audio → Buffer Size). For the Phase 0 checklist item "repeat
  at 128 and 1024", change it by hand between runs. The report reads the new value from Live's log.
- **Sample rate** (also read from the log), **MIDI port switches** (Track / Sync / Remote), **plug-in rescans**, and the
  **count-in** preference. Count-in doesn't change the results: the recorder waits for song time.
- **Plug-in windows.** The LOM can't open a plug-in editor or click in it.

## Socket protocol

TCP on `127.0.0.1:9871`, localhost only. One JSON object per line, UTF-8, `\n` terminated, in
each direction. Several clients may connect. Requests on one connection are answered in order.

```json
{"id": 7, "cmd": "set_tempo", "args": {"bpm": 120}}
{"id": 7, "ok": true, "result": {"tempo": 120.0}}
{"id": 8, "ok": false, "error": "CommandError: no track named 'Foo'; have ['1-MIDI', '2-MIDI']", "traceback": "..."}
```

`id` is any JSON value and is echoed back. `args` is optional. Errors never close the
connection; a line that isn't JSON gets an error reply with `id: null`.

**Threading.** The listener and client sockets are non-blocking and are serviced only in
`update_display`, Live's main-thread tick (about every 100 ms). Each tick accepts, reads, runs
up to 64 commands and writes. So every LOM call runs on the main thread, and one call takes up
to ~100 ms. Nothing blocks in Live. Commands that Live finishes later (loading a device) return
straight away, and the client polls (`load_device_and_wait`).

**Addressing.** `track`: 0-based index, a name, or `"master"`. `device` and `parameter`: 0-based
index or a name. Names match exactly (ignoring case) first, then by a unique substring.

### Commands

| Command | Args | Result |
|---|---|---|
| `ping` | | `{pong, live_version}` |
| `list_commands` | | command names |
| `get_state` | `include_devices=true` | tempo, signature, is_playing, current_song_time, record_mode, session_record, is_counting_in, arrangement_overdub, loop, metronome, midi_recording_quantization, `tracks[]` (index, name, is_midi, arm, monitoring, mute, solo, input/output routing, devices), master meter, selected_track |
| `set_tempo` | `bpm` | `{tempo}` |
| `start_transport` | `from_beat?`, `resume=false` | `{is_playing, current_song_time}` |
| `stop_transport` | | same |
| `set_song_properties` | any of `current_song_time, loop, metronome, arrangement_overdub, record_mode, session_record, signature_numerator, signature_denominator, back_to_arranger, midi_recording_quantization` (`"none"` or a `RecordingQuantization` name) | the new values |
| `create_midi_track` | `index=-1`, `name?` | the track |
| `delete_track` | `track` | `{deleted, track_count}` |
| `get_track` | `track` | the track |
| `set_track` | `track`, `name? arm? monitoring? ("in"/"auto"/"off") mute? solo?` | the track |
| `arm_track` | `track`, `arm=true` | `{track, arm}` |
| `browser_search` | `query`, `category="plugins"`, `format?`, `limit=50` | `[{name, path, uri, format, exact, is_device}]` |
| `load_device` | `name`, `track`, `category="plugins"`, `format?` | `{track, device_count_before, loaded, other_matches}`. The device appears a moment later. |
| `load_plugin` | `name`, `track`, `format?` | same as `load_device` with category `plugins` |
| `list_devices` | `track` | `[{index, name, class_name, class_display_name, type, is_active, parameter_count}]` |
| `list_parameters` | `track`, `device` | `{device, parameters: [{index, name, value, min, max, is_quantized, is_enabled, display, value_items?}]}` |
| `get_parameter` | `track`, `device`, `parameter` | the parameter |
| `set_parameter` | `track`, `device`, `parameter`, `value` (number, `true`/`false` → max/min, or a value item) | the parameter |
| `get_routing` | `track`, `direction="input"` | `{current: {type, channel}, available_types: [{display_name, category, track_index}], available_channels: [{display_name}]}` |
| `set_routing` | `track`, `direction`, `type?` or `source_track?`, `channel?` | as `get_routing` |
| `get_input_routing` / `set_input_routing` | as above, input only | |
| `start_recording` | `mode="arrangement"`, `from_beat?`; or `mode="session"` with `track?`, `scene?`, `length_beats?` | the record state |
| `stop_recording` | `mode`, `stop_transport=true` | the record and transport state |
| `clear_arrangement_clips` | `track` | `{deleted}` |
| `get_clip_notes` | `track`, `source="auto"` (`arrangement`/`session`), `clip_index?`, `scene?` | clip info (start_time, markers, loop, length) and `notes[]` (pitch, start_time, duration, velocity, mute, probability, and `song_time` for arrangement clips) |
| `get_meters` | `tracks?` (default all) | `[{track, name, left, right, level}]`, master last |

**Plug-in lookup.** `load_device` walks `application().browser.<category>` (for plugins, the
Plug-Ins tree). It loads onto the track by selecting it and calling `browser.load_item`.
An exact name wins over a substring match. If a plug-in is listed in more than one format, the
order is VST3, then AU, then VST, unless `format` says otherwise. The format comes from the
browser folder above the item ("VST3", "Audio Units", "VST"), or else from its URI.

**MIDI From another track's plug-in.** Set the routing type with `source_track` (matched by the
routing type's `attached_object`, so track names don't matter). Then set `channel` to the plug-in's
name, which Live lists next to "Pre FX" and "Post FX".

**"Most recent clip".** In the arrangement, the MIDI clip that starts latest. In Session View,
the slot last recorded through this bridge, or else the recording or playing slot, or else the
last filled one. Note times are in clip beats. `song_time = clip.start_time + start_time - start_marker`.

## MCP server

`.mcp.json` in the repo root registers it:

```json
{"mcpServers": {"live-bridge": {"command": "uv",
  "args": ["run", "--directory", "LiveBridge", "python", "-m", "livebridge.mcp_server"]}}}
```

Its tools are the commands above, plus `load_device` (which waits for the device),
`measure_note_timing` (offsets of the latest clip on a track, in beats, ms and samples),
`get_audio_settings` (sample rate, buffer sizes and latencies from Live's log), and
`bridge_command` (any command by name). It works with the `mcp` SDK 1.x (`FastMCP`) and 2.x (`MCPServer`). Claude Code asks to
approve the server the first time it starts in this folder.
