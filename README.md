# Groovebox by PJH

A drum groove instrument for FL Studio built around your own sample folders. Point each lane at a folder, scroll through samples while the groove plays, let Groovebox tell you which samples fit, and let the generator write grooves per genre, irregular polymeters, or completely rule-free patterns. Every lane can have its own mixer track, or play one of your own FL channels.

## Install / update

1. Open the latest successful run under the **Actions** tab, download **Groovebox-Windows** from **Artifacts**, unzip it.
2. Close FL Studio, then copy `Groovebox.vst3` into `C:\Program Files\Common Files\VST3\` (replace the old one).
3. Open FL. Updates normally need no rescan. When an update changes outputs or parameters (like version 0.2), rescan once: **Options > Manage plugins**, untick **Verify plugins**, click **Find installed plugins**.

## Quick start

1. Add Groovebox to the Channel Rack.
2. **Library...** maps your drum library: subfolders are matched to lanes by name (kick, snare, clap, hat, open hat, ride, shaker, perc, rim, tom, click, texture, fx, bass, 808...). Or drop any folder (or one sample) on a lane. Scans run in the background, so even a huge library won't freeze FL.
3. Pick a genre and click **Generate**. Press Play in FL, or **Play** in Groovebox.
4. Hover a sample name and **scroll** to dig. Click the name to audition.
5. Click any lane to see its settings in the panel at the bottom.

## Sample fit

Groovebox listens to every sample in your folders (tail length, attack, brightness, low end, top end, pitch) and rates how well it fits the lane's role in the current genre:

- **Green** dot: good fit, **yellow**: usable, **red**: poor fit, **hollow**: still listening.
- Hover the sample name for the reason ("too long (820 ms) for Techno Closed Hat") and the detected note for pitched sounds (shown in FL's octave naming, middle C = C5).
- **?** and **Shuffle samples** favour good fits. **Fit filter** makes scrolling skip poor fits.
- Changing the genre re-rates everything. Results are cached in `Documents\Groovebox`, so a library is only analyzed once.

## Locks

Each lane has two padlocks:

- Before the sample name: **sample lock**. Shuffle samples, **?** and Library scans leave that sample alone.
- Before Gen: **pattern lock**. Generate, Mutate and the lane's Gen/Mut leave that pattern alone.

Manual edits (scrolling, drawing steps) still work on locked lanes.

## Setups

- **Setups > Save setup as...** saves everything (folders, samples, patterns, knobs) to `Documents\Groovebox\Setups`.
- Saved setups are listed in the Setups menu; click one to load it.
- **A new Groovebox starts from your last session**, and the last session autosaves every few seconds, so a crash never loses your folders and grooves. **Setups > Load last session** brings it back any time.
- FL projects still restore their own Groovebox state as usual.

## Each lane on its own mixer track

1. Open Groovebox's plugin wrapper, go to the **Processing** tab, click **Auto map outputs**.
2. Every lane plays on its own output (Lane 1 also feeds Main), so each lands on its own mixer track.

Change a lane's routing in the lane panel (**Output**: own output, Main, or any Out 2 to 32). Without auto-mapped outputs everything plays through Main.

## Lanes that play your FL channels

Set a lane to **Sends notes to an FL channel** in the lane panel. It keeps its groove, but instead of playing a sample it sends MIDI notes, so the sound comes from any FL channel (Sampler with FL's envelopes and filters, Slicex, a synth...).

1. In Groovebox's wrapper settings, set the **MIDI output port** to a free number (for example 10).
2. In the target channel's settings, set its **MIDI input port** to the same number.
3. Pick the lane's **MIDI** channel and **Note** in the lane panel (C5 is FL's default root note).

If several target channels share the port and all play together, give each lane a different MIDI channel and set each target to listen to only its channel, or use separate ports with one Groovebox lane each.

## Controls

**Top bar**

| Control | What it does |
|---|---|
| Setups | Save, load, last session |
| Library... / Shuffle samples / Fit filter | Map a library, re-dig every unlocked lane, skip poor fits while scrolling |
| Genre | House, Deep House, Tech House, Minimal, Techno, Dub Techno, Breaks, UK Garage, IDM, Ambient, **Hybrid** (every lane borrows from a random genre), **Free** (no genre rules) |
| Generate / Mutate + amount | New groove, or evolve the current one (pattern-locked lanes are kept) |
| Density / Variation / Irregular | How busy; ghosts, velocity spread, probability and micro timing; odd lengths, odd rates and Euclidean placement |
| Swing x / Humanize x | Scale every lane's swing and humanize (automatable) |
| Draw | What dragging on the step grids edits: Steps, Velocity, Probability, Nudge, Ratchet, Pitch, Pan, Cutoff, Sample start |
| Bars / Drag MIDI / Save MIDI | Export the groove as MIDI (saved to Documents\Groovebox\MIDI) |
| Play / Sequencer | Preview without FL's transport / turn the sequencer off to play Groovebox from MIDI only (C3 = lane 1, C#3 = lane 2...) |

**Each lane**

| Control | What it does |
|---|---|
| M / S | Mute / solo |
| Role | What the lane is: drives the generator and the fit rating |
| Locks | Sample lock, pattern lock |
| < name > ? ... | Previous / next sample (or scroll), random good fit, choose folder |
| Length / Rate | 1 to 64 steps; 1/4 down to 1/32 with triplets and dotted values |
| Lvl, Pan, Pitch, Cut | Level, pan, pitch and filter cutoff. **Automatable in FL** ("Lane 3 Cutoff" etc.) |
| Decay, Swing, Hum | Sample length, lane swing, humanize |
| Step grid | Click to toggle, drag to paint, right-click for clear, shift, reverse, randomize velocity/pitch/pan, copy/paste |

**Lane panel (bottom)**: play mode (sample or FL channel), MIDI channel and note, filter type (low/high/band-pass) and resonance, attack, reverse, output, choke group.

## Building yourself (optional)

Requires CMake 3.22+ and Visual Studio 2022 or newer (JUCE 8 is downloaded automatically):

```
cmake -B build -A x64
cmake --build build --config Release --target Groovebox_VST3
```

`-DCG_BUILD_TESTS=ON` adds a headless test harness (`Tests/TestMain.cpp`).
