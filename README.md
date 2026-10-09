# Groovebox

A drum groove instrument for FL Studio built around your own sample folders. Point each lane at a folder, scroll through samples while the groove plays, and let the generator write grooves per genre, irregular polymeters, or completely rule-free patterns. Then drag the result out as MIDI.

## Get the plugin (no coding needed)

GitHub compiles the Windows plugin for you, for free.

1. Make a free account at github.com if you don't have one.
2. Click **+** (top right) > **New repository**. Name it `groovebox`, leave everything else as is, click **Create repository**.
3. On the new empty repository page, click **uploading an existing file**.
4. Unzip `Groovebox-source.zip` on your computer. Open the `Groovebox` folder, select **everything inside it** (including the `.github` folder) and drag it onto the GitHub upload page. Click **Commit changes**.
5. Open the **Actions** tab. A build called "Build Groovebox" starts by itself and takes about 10 to 15 minutes. Wait for the green tick.
6. Click the finished run, scroll down to **Artifacts**, and download **Groovebox-Windows**.
7. Unzip it and follow `INSTALL.txt` (copy `Groovebox.vst3` to `C:\Program Files\Common Files\VST3\`, then rescan plugins in FL).

If the Actions tab shows no build: the `.github` folder didn't upload. Click **Actions** > **set up a workflow yourself**, paste the contents of `.github/workflows/build.yml`, and commit.

Any time you (or Claude) change the code and upload it again, GitHub builds a fresh version automatically.

## Quick start in FL Studio

1. Add Groovebox to the Channel Rack.
2. Click **Library...** and choose the root of your drum samples. Subfolders are matched to lanes by name (kick, snare, clap, hat, open hat, ride, shaker, perc, rim, tom, click, texture, fx, bass, 808...). Unmatched folders get their own lanes, up to 32.
   Or drop any folder (or a single sample) straight onto a lane.
3. Pick a genre and click **Generate**. Press Play in FL, or **Play** in the plugin.
4. Hover over a lane's sample name and **scroll the mouse wheel** to dig through that folder while the groove plays. Click the name to audition. **?** picks a random sample, **Shuffle samples** re-digs every lane at once.
5. Happy with it? Drag the **Drag MIDI** button onto FL's playlist or a pattern.

## Controls

**Top bar**

| Control | What it does |
|---|---|
| Genre | House, Deep House, Tech House, Minimal, Techno, Dub Techno, Breaks, UK Garage, IDM, Ambient, plus **Hybrid** (every lane borrows from a random genre) and **Free** (no genre rules at all) |
| Generate | New groove for all lanes |
| Mutate + amount | Evolves the current groove instead of replacing it: drops or adds ghost notes, shifts velocities, probability and timing. Small amounts are subtle; high amounts can change lane lengths |
| Density | How busy the generator writes |
| Variation | Ghost notes, velocity spread, steps that only play some of the time, micro timing, 2-bar hat phrases |
| Irregular | Chance that a lane gets an odd length (5, 7, 11, 13...), an odd rate (triplets, dotted, 1/32) and Euclidean placement. Kicks stay straight unless you push it above about 60% |
| Swing x / Humanize x | Scale every lane's swing and humanize at once (automatable in FL) |
| Draw | What dragging on the step grids edits: Steps, Velocity, Probability, Nudge (micro timing), Ratchet |
| Bars / Drag MIDI / Save MIDI | Export the groove as MIDI. Odd lane lengths make every bar of the clip different. Saved files go to Documents\Groovebox\MIDI |
| Play | Preview without FL's transport |
| Sequencer | Turn off to use Groovebox only as a sampler played from MIDI (C1 = lane 1, C#1 = lane 2...) |

**Each lane**

| Control | What it does |
|---|---|
| M / S | Mute / solo |
| Role | Tells the generator how the lane behaves (kick, hat, texture...) |
| < name > ? ... | Previous / next sample (or scroll), random sample, choose folder |
| Length box | 1 to 64 steps. Drag up/down or scroll. Lanes with different lengths drift against each other |
| Rate | 1/4 down to 1/32, including triplets and dotted values |
| Gen / Mut | Generate or mutate this lane only |
| Lvl, Pan, Pitch, Decay, Swing, Hum | Per-lane sound and feel. Double-click a knob to reset. Pitch is fine-tunable to 0.01 semitone |
| Step grid | Click to toggle, drag to paint. Brightness shows probability, bar height shows velocity, dots show ratchets, a tick shows nudge |

**Right-click a step grid** for: clear, fill, shift left/right, reverse, randomize velocities, reset feel, double length, copy/paste a lane, choke group (open and closed hats share group 1 by default) and output routing.

## Multiple outputs

Each lane can go to Main or Out 2 to Out 8 (right-click the grid > Output). Enable the extra outputs in FL's plugin wrapper settings and route them to mixer tracks to process kick, hats and percussion separately. Lanes sent to an output FL hasn't enabled play through Main.

## Tips for subtle, irregular grooves

- Set Variation to 40 to 60% and Irregular to 10 to 30% with a straight genre, so the core stays danceable and the top end breathes.
- Duplicate a hat lane, set it to 1/16T or length 15, drop its level and lower step probability to around 50%. It never repeats the same way twice.
- Draw: Nudge lets you push single ghost notes a few milliseconds late for a lazier pocket.
- Use Mutate at 10 to 20% every few minutes while jamming, and save MIDI of the versions you like.
- Every time FL loops, steps with less than 100% probability re-roll, so patterns keep evolving.

## Building yourself (optional)

Requires CMake 3.22+ and Visual Studio 2022 or newer (JUCE 8 is downloaded automatically):

```
cmake -B build -A x64
cmake --build build --config Release --target Groovebox_VST3
```

`-DCG_BUILD_TESTS=ON` adds a headless test harness (`Tests/TestMain.cpp`).
