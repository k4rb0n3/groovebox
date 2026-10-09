# Groovebox by PJH

A drum groove instrument for FL Studio built around your own sample folders. Point each lane at a folder, scroll through samples while the groove plays, let Groovebox tell you which samples fit, and let the generator write grooves per genre, long evolving loops, irregular polymeters, or a whole arranged track. Every lane can have its own mixer track. Click **Help** inside the plugin for a plain-language guide.

## Install / update

1. Open the latest successful run under the **Actions** tab, download **Groovebox-<version>-Windows** (e.g. Groovebox-0.5.0-Windows) from **Artifacts**, unzip it.
2. Close FL Studio, then copy `Groovebox.vst3` into `C:\Program Files\Common Files\VST3\` (replace the old one).
3. Open FL. Updates normally need no rescan (the version number shows next to the title). When an update changes outputs or parameters (like version 0.2), rescan once: **Options > Manage plugins**, untick **Verify plugins**, click **Find installed plugins**.

## Quick start

1. Add Groovebox to the Channel Rack.
2. **Library...** maps your drum library: subfolders are matched to lanes by name (kick, snare, clap, hat, open hat, ride, shaker, perc, rim, tom, click, texture, fx, bass, 808...). Or drop any folder (or one sample) on a lane. Scans run in the background, so even a huge library won't freeze FL.
3. Pick a genre and click **Generate**. Press Play in FL, or **Play** in Groovebox.
4. Hover a sample name and **scroll** to dig. Click the name to audition.
5. Click any lane to see its settings in the panel at the bottom.

## Lane editor

Click a lane and the bottom panel shows its sequence large, with a row for each step setting: **Velocity, Probability, Pitch, Pan, Cutoff, Sample start, Nudge, Ratchet**. Drag up and down to draw values (values are printed in the cells when there's room), double-click to reset a step, right-click a row to reset, randomize or ramp it. The panel also holds the lane's filter, resonance, attack, reverse, output and choke group. **Lane editor** (bottom right) hides it.

## Long loops

- **Loop** (1 to 64 bars) sets how long Generate writes. Long loops repeat the phrase but evolve bar by bar, with small fills every 4 and 8 bars.
- A lane's **Length** goes up to 64 bars: up to 64 steps it moves by 1, beyond that by whole bars.
- Long lanes are shown 4 bars at a time. Use **< >** next to the bar numbers, or **Follow** to track the playhead.

## Song mode

Turn on **Song**, pick a length (32 to 128 bars) and press **Make song**. Groovebox arranges intro, build, main, break and outro and decides which lanes play in each section (the kick drops out for the last bar of a build). It follows FL's song position, lanes sitting out the current section are dimmed, and **To piano roll / Save MIDI** export the whole arrangement.

## Groove from a reference

Drop a drum loop, or a whole track you love, on the **Groove** box (above the lanes, or click it to pick a file). Groovebox finds its tempo and bar and measures, for every 16th, how early or late and how loud its lows (kick, bass), mids (snare, clap, perc) and highs (hats, shakers, ride) play. The mini grid shows the result (red lows, orange mids, blue highs; off-centre means early or late).

- Every lane moves towards that feel, following its own band. **Groove** sets how far (0 = off).
- Global swing goes to 0 when a groove loads, since the reference brings its own.
- Genre **From reference groove** + **Generate** writes patterns from where the reference hits.
- Tempo is detected; if it picks half or double, drag the **Ref** box to the real tempo and it listens again.
- Loops that start on the downbeat give the cleanest result. Right-click the Groove box to analyze again or clear.
- The feel carries into **To piano roll** and **Save MIDI**, and is saved with setups and FL projects.

## Genres and tempo

House, Deep House, Tech House, Minimal, Techno, Dub Techno, Breaks, UK Garage, IDM, Ambient, Drum & Bass, Jungle, Dubstep / Halftime, Trap (with hat rolls), Hip-hop / Boom bap, Downtempo, plus **Hybrid**, **Free** and **From reference groove**. Picking a genre sets the **BPM** box to its typical tempo; that tempo drives Groovebox's own **Play** button. When FL plays, FL's tempo is used, so set FL to match.

## Into FL's piano roll

1. Open Groovebox's piano roll: right-click Groovebox in the Channel Rack > **Piano roll**.
2. Set how many bars to export (box next to **To piano roll**; Song mode exports the whole song).
3. Drag **To piano roll** into that piano roll window.

Groovebox switches to **Piano roll mode**: its own sequencer stops and it plays the notes in the piano roll (C3 = lane 1, C#3 = lane 2..., shown on each lane). Samples, filters, outputs and automation stay; each note picks up the pitch, pan, cutoff and sample start of the step it sits on; velocity is in the notes and ratchets become real notes. Notes you add or move to an empty step play the lane's normal sound. Probability is decided at export. Click **Piano roll mode** to switch back to Groovebox's own sequencer.

## Shortcuts

Click anywhere in Groovebox first. **Space stays FL's play/stop.**

| Key | Does |
|---|---|
| Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) | Undo / redo (steps, generate, mutate, samples, lane settings, lanes, song) |
| Ctrl+S | Save setup |
| G / Shift+G | Generate all / selected lane |
| M / Shift+M | Mutate all / selected lane |
| Up / Down | Select lane |
| Left / Right | Previous / next sample in the selected lane |
| A | Audition the selected lane |
| R / Shift+R | Random good-fit sample in the selected lane / shuffle all |
| P | Groovebox's own Play |
| H or F1 / Esc | Help / close help |

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

Change a lane's routing in the lane editor (**Output**: own output, main output, or any output 2 to 32). Without auto-mapped outputs everything plays through the main output.

## Controls

**Top bar**

| Control | What it does |
|---|---|
| Help | Plain-language guide to every control |
| Setups | Save, load, last session |
| Library... / Shuffle samples / Fit filter | Map a library, re-dig every unlocked lane, skip poor fits while scrolling |
| Genre | House, Deep House, Tech House, Minimal, Techno, Dub Techno, Breaks, UK Garage, IDM, Ambient, **Hybrid** (every lane borrows from a random genre), **Free** (no genre rules) |
| Generate / Mutate + amount | New groove, or evolve the current one (pattern-locked lanes are kept) |
| Density / Variation / Irregular | How busy; ghosts, velocity spread, probability and micro timing; odd lengths, odd rates and Euclidean placement |
| Swing x / Humanize x | Scale every lane's swing and humanize (automatable) |
| Groove / amount / Ref | Reference loop or track to take the feel from; how much; its tempo (auto) |
| Loop / Song / Make song | Bars Generate writes; song mode on/off; arrange a full track |
| BPM | Tempo for Groovebox's own Play button (follows the genre) |
| Bars / To piano roll / Piano roll mode / Save MIDI | Drag the groove (or song) into Groovebox's piano roll; switch between piano roll and Groovebox's own sequencer; save a .mid file |
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

## Building yourself (optional)

Requires CMake 3.22+ and Visual Studio 2022 or newer (JUCE 8 is downloaded automatically):

```
cmake -B build -A x64
cmake --build build --config Release --target Groovebox_VST3
```

`-DCG_BUILD_TESTS=ON` adds a headless test harness (`Tests/TestMain.cpp`).
