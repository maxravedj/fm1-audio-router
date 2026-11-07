# Debugging with fm1_remote.py

The client drives Audio Router through USB-MIDI SysEx. It uses the existing
MIDI interface, not a serial console. Run commands from the project root:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-debug.txt
python src/tools/fm1_remote.py log
```

On Arch, install `alsa-lib` for the MIDI backend; on Linux the ALSA sequencer
must be available (`/dev/snd/seq`). If several devices/ports are present, put
`--port 'part of MIDI port name'` before the command. List port names with:

```sh
python -c 'import mido; print(*mido.get_input_names(), sep="\n")'
python src/tools/fm1_remote.py --port 'FM1 Audio' log
```

## Diagnostics

| Command | Purpose |
| --- | --- |
| `log` | Current/retained crash, reset/clock/rail data and Bluetooth session/profile/codec diagnostics. |
| `crashes` | Saved crash history, oldest first (up to eight), including build IDs and PCs. |
| `crashes --elf build/gateway/fm1audio.elf` | Resolve crash PCs against the **matching** build's symbols. |
| `stats` | Retained platform workload/stack snapshots, when collected by that firmware. |
| `screen` | Describe the current screen through the screen-reader channel. |
| `listen` | Print screen-reader events while subsequent commands run. |
| `screenshot` | Download a frozen 240×240 PNG to the current working directory. |

A different build's ELF can produce misleading symbol names. Release assets
include `audio-router-debug.tar.xz` with ELF, disassembly and build metadata.
A stats/history request can legitimately return no records. Collect logs
before clearing them: `crashes --clear` and `stats --clear` erase their retained
flash logs; they are not needed for ordinary diagnosis.

Capture Bluetooth failures immediately, with the unwanted competing source
disconnected:

```sh
python src/tools/fm1_remote.py log > bluetooth-log.txt
python src/tools/fm1_remote.py crashes --elf build/gateway/fm1audio.elf
```

Report source OS, SBC/AAC selection, whether FM-1 or the source initiated the
connection, manual disconnect versus range loss, and whether power cycling
changes it. Authentication/encryption, profile/AVDTP/config, codec/audio/queue
rows distinguish a paired link from an actual audio stream. A radio skipped
after a crash is intentional protection; collect logs, then Settings SAVE can
explicitly retry Bluetooth RECEIVER.

## Screenshots

```sh
python src/tools/fm1_remote.py screenshot
```

Output: `fm1-screenshot-YYYY-MM-DD_HH-MM-SS_microseconds.png`. The client needs
neither Pillow nor an image converter for this command. Display updates freeze
through capture/download, while audio and the physical MASTER continue. Panel
input cancels capture and applies the control. Completion, lost session,
five-second idle timeout or 30-second hard timeout resumes drawing. Failed or
interrupted downloads do not leave a partial PNG. Screenshot capture writes
no device flash.

## Workload investigation

Run on a HOME screen, keeping source, codec and waveform window identical
between comparisons:

```sh
python src/tools/fm1_remote.py profile 10
python src/tools/fm1_remote.py profile 10 no-lcd
python src/tools/fm1_remote.py profile 10 capture-only
python src/tools/fm1_remote.py profile 10 capture-off
python src/tools/fm1_remote.py profile 10 idle-slow
```

Duration is 1–30 seconds. `no-lcd` suppresses hardware LCD transfers;
`capture-only` skips waveform foreground extraction/drawing;
`capture-off` additionally skips waveform history capture; `idle-slow` lengthens
CPU1's idle mailbox backoff without stopping that core. Flags can be combined.
They are temporary, never persisted, and restore when the session ends/expires
or the view/updater changes. The screen may appear frozen while work is
suppressed; live meters/audio are separate from graph rendering.

Compare WORK (the nesting-aware measured busy interval), wave_read/wave_draw,
lcd_wait/window/byte counts, audio/USB/BT stages, CPU2 time and xruns. Stage
wall times include preemption and overlap: **do not sum them as CPU load**.
Profiling adds overhead; it is diagnostic evidence, not a replacement for
listening or hardware timing checks.

## Remote controls and reproducible cases

Several commands can be chained in order:

```sh
python src/tools/fm1_remote.py tap HOME wait 100 screenshot
python src/tools/fm1_remote.py tap EDIT turn SELECT 1 turn ALGO 1 screen
python src/tools/fm1_remote.py press OCT+ wait 1100 release OCT+ wait 200 log
python src/tools/fm1_remote.py tap FX turn SELECT 1 turn K1 4 screen
python src/tools/fm1_remote.py tap PLAY wait 200 log
```

| Command | Meaning |
| --- | --- |
| `tap BTN`, `press BTN`, `release BTN` | Tap or hold/release a panel button. |
| `turn ENC N` | Turn an encoder by signed detents. |
| `key K down`, `key K up`, `tapkey K` | Keyboard key 0..26 (0 = F3). |
| `master N` | Override MASTER position, 0..4096; follows the firmware's taper. |
| `master pot` | Release the override and follow the physical potentiometer. |
| `wait MS` | Allow time for fades, long presses, redraws or debounce. |
| `release-all` | Release remote buttons/keys and return MASTER to its pot. |

Buttons: `OCT- OCT+ FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC`.
Encoders: `SELECT ALGO PRESET K1 K2 K3 K4`.

Remote controls have the same side effects as panel controls, including
settings/preset saves, mute and pairing. The EDIT example changes a draft;
SAVE is required to persist/apply it. Finish held-control experiments with
`release-all`. Wi-Fi, Bluetooth-transmitter (`bta`) and BLE-central (`dev`)
commands also exist in the shared client but are not Audio Router receiver
controls; configure/pair this receiver through Settings and OCT+.
