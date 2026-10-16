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

Turn on **Song**, pick a length (32 to 128 bars) and press **Make song**. Groovebox arranges intro, build, main, break and outro and decides which lanes play in each section (the kick drops out for the last bar of a build). It follows FL's song position and lanes sitting out the current section are dimmed. **Click or drag on the song strip** to scrub: the marker shows where Groovebox's own Play starts and which lanes play there. Notes sent to FL cover the whole song.

## Groove from a reference

Drop a drum loop, or a whole track you love, on the **Reference groove** box (top right), or click it. Groovebox finds its tempo and bar and measures, for every 16th, how early or late and how loud its lows (kick, bass), mids (snare, clap, perc) and highs (hats, shakers, ride) play.

Clicking the box opens the **Groove panel** at the bottom: every 16th of the reference, with how loud (bar height), how often (%) and how early or late (ms). There you can:

- Set **Groove amount**: how far your lanes lean into that feel while they play (0 = off).
- **Copy groove to lanes**: write the reference's hits, accents and timing into your lanes (kick from the lows, snare and clap from the loud mids, hats from the highs). Pattern-locked lanes are kept.
- **Copy timing only**: keep your patterns but give them the reference's push and drag (Nudge and Velocity rows).
- Fix the **Tempo** if it picked half or double (it listens again).

Ctrl+Z undoes a copy. Genre **From reference groove** + **Generate** also writes patterns from where the reference hits. The feel goes along when you send lanes to FL, and it's saved with setups and projects.

## Foliage: foley, followers and beds

- **Followers**: in the lane editor set **Plays** to "Right after lane N". The lane answers that lane's hits a few ms later, off the grid (a rustle completing the snare, a tick trailing the kick). Set how often, how many ms after, the drift (timing, sample start, pitch) and the level compared to the hit.
- **Foley** lanes: sparse, quiet hits on 1/32 steps nudged off the grid, a different bit of the sample each time, different every bar.
- **Beds**: the sample loops underneath everything while the music runs (crackle, rain, room tone, hiss) and fades with song sections.
- **Starter kit** (top bar): the lanes a genre needs, with followers and beds. Keeps samples you already picked for those instruments and fills the rest from your library (folders named foley, vinyl, crackle, rain, vocal... are recognised).
- **Randomize all**: new instruments from your library, new samples and new patterns in one go. Locked lanes stay.

## Genres and tempo

House, Deep House, Tech House, Minimal, Techno, Dub Techno, Breaks, UK Garage, IDM, Ambient, Drum & Bass, Jungle, Dubstep / Halftime, Trap (with hat rolls), Hip-hop / Boom bap, Downtempo, **Microhouse**, **Deep & dusty**, **2-step / future garage** (the last three add more quiet foliage), plus **Hybrid**, **Free** and **From reference groove**. Picking a genre sets the **BPM** box to its typical tempo; that tempo drives Groovebox's own **Play** button. When FL plays, FL's tempo is used, so set FL to match.

## Send lanes to FL as their own channels

Click a lane; the lane editor at the bottom has dashed drag buttons:

1. Drag **Sample > Channel Rack** onto FL's Channel Rack. FL makes a new channel with the lane's sample, with its pitch, filter, decay, attack, reverse, level and pan baked in.
2. Open that channel's piano roll and drag **Notes > Piano roll** into it. The pattern lands on C5 (the sample's own pitch) with velocity, step pitch, ratchets, swing and the reference groove. Length = your loop, or the whole song in Song mode.

**All lanes > Channel Rack** makes every channel at once; then drag each lane's notes into its channel. Per-step pan, cutoff and sample start stay in Groovebox (FL channels can't take them per note). Files are kept in `Documents\Groovebox\To FL`. **Setups > Save all lanes as one MIDI file** writes every lane into one .mid (C3 = lane 1...).

## Shortcuts

Click anywhere in Groovebox first. **Space stays FL's play/stop.**

| Key | Does |
|---|---|
| Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) | Undo / redo (steps, generate, mutate, samples, lane settings, lanes, song) |
| Ctrl+S | Save setup |
| Ctrl+D | Duplicate the selected lane (also the + on each lane) |
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
- Drum and foley lanes go for **one-shots**: loops ("loop", a tempo like 124 or "bpm" in the name, a Loops folder, or far too long for the instrument) are rated poor and never picked by Shuffle, ?, Randomize all or a new folder. Beds and vocals may be loops.
- Changing the genre re-rates everything. Results are cached in `Documents\Groovebox`, so a library is only analyzed once.

## Locks

Click a lane's padlock (either one) to **lock the lane**: Shuffle samples, **?**, library scans, Generate and Mutate leave its sample and pattern alone. Right-click a padlock to lock only the sample or only the pattern. Manual edits (scrolling, drawing steps) still work on locked lanes.

Right-click a sample name to **open it in Explorer** or copy its path.

## Setups

- **Setups > Save setup as...** saves everything (folders, samples, patterns, knobs) to `Documents\Groovebox\Setups`.
- Saved setups are listed in the Setups menu; click one to load it.
- **A new Groovebox starts from your last session**, and the last session autosaves every few seconds, so a crash never loses your folders and grooves. **Setups > Load last session** brings it back any time.
- FL projects still restore their own Groovebox state as usual.

## Mixer tracks

**Lanes playing inside Groovebox:** turn on **Separate mixer tracks** in the lane editor (it's off by default, so adding Groovebox to an existing project never touches your mixer: everything plays on Groovebox's one track). Then open Groovebox's plugin wrapper (icon at the top left of its window), **Processing** tab, **Auto map outputs**. Lane 1 then lands on the first new mixer track, lane 2 on the next, and so on. In the lane editor, **Mixer track** picks Own, Main, or "Same as lane N" to share a track (the **How?** button repeats these steps).

**Lanes sent to FL as channels:** set the mixer track on the FL channel itself (the number box on the channel in the Channel Rack).

## Controls

**Top bar**

| Control | What it does |
|---|---|
| Help | Plain-language guide to every control |
| Setups | Save, load, last session |
| Library... / Shuffle samples / Randomize all | Map a library; re-dig every unlocked lane; new instruments, samples and patterns at once |
| Settings | Density, Variation, Irregular, Swing x, Humanize x, Mutate amount, Fit filter |
| Starter kit | Set up the lanes the genre needs (core drums, foley followers, beds) |
| Genre | House, Deep House, Tech House, Minimal, Techno, Dub Techno, Breaks, UK Garage, IDM, Ambient, **Hybrid** (every lane borrows from a random genre), **Free** (no genre rules) |
| Generate / Mutate | New groove, or evolve the current one (pattern-locked lanes are kept) |
| Density / Variation / Irregular | How busy; ghosts, velocity spread, probability and micro timing; odd lengths, odd rates and Euclidean placement |
| Swing x / Humanize x | Scale every lane's swing and humanize (automatable) |
| Reference groove | Loop or track to take the feel from; click it for the Groove panel |
| Loop / Song / Make song | Bars Generate writes; song mode on/off; arrange a full track |
| BPM | Tempo for Groovebox's own Play button (follows the genre) |
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
