// Headless checks for Groovebox: library scan, generation, rendering, MIDI export, state, UI snapshot.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/GrooveAnalysis.h"

struct FakeHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 124.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        return p;
    }
};

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "FAIL: " << msg << "\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static juce::String laneString (const cg::Lane& l)
{
    juce::String s;
    for (int i = 0; i < l.length; ++i)
    {
        const auto& st = l.steps[(size_t) i];
        s << (st.on ? (st.ratchet > 1 ? "R" : (st.prob < 0.99f ? "?" : (st.vel > 0.7f ? "X" : "x"))) : ".");
    }
    return s;
}

struct MidiCount { int on = 0, off = 0; };

static float render (GrooveboxProcessor& p, FakeHead& head, double seconds, juce::AudioBuffer<float>* capture,
                     MidiCount* midiCount = nullptr, std::vector<float>* busPeaks = nullptr)
{
    const double sr = 48000.0;
    const int bs = 512;
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), bs);
    juce::MidiBuffer midi;
    const int blocks = (int) (seconds * sr / bs);
    float peak = 0.0f;
    if (capture) capture->setSize (2, blocks * bs);
    if (busPeaks) busPeaks->assign ((size_t) (buf.getNumChannels() / 2), 0.0f);
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        p.processBlock (buf, midi);
        peak = juce::jmax (peak, buf.getMagnitude (0, bs));
        if (busPeaks)
            for (int pr = 0; pr < buf.getNumChannels() / 2; ++pr)
                (*busPeaks)[(size_t) pr] = juce::jmax ((*busPeaks)[(size_t) pr], buf.getMagnitude (pr * 2, 0, bs));
        if (midiCount)
            for (const auto m : midi)
            {
                if (m.getMessage().isNoteOn()) ++midiCount->on;
                if (m.getMessage().isNoteOff()) ++midiCount->off;
            }
        if (capture)
            for (int ch = 0; ch < 2; ++ch)
                capture->copyFrom (ch, b * bs, buf, ch, 0, bs);
        head.ppq += bs * head.bpm / 60.0 / sr;
    }
    return peak;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File root (argc > 1 ? argv[1] : "lib");
    const juce::File outDir (argc > 2 ? argv[2] : ".");
    GrooveboxProcessor::lastSessionFile().deleteFile(); // start clean

    GrooveboxProcessor p;
    p.runSynchronously = true;
    FakeHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);

    // 1. Library scan
    p.scanLibrary (root);
    int withSample = 0;
    for (int i = 0; i < p.numLanes; ++i)
    {
        const auto& l = p.lanes[(size_t) i];
        std::cout << "  lane " << i + 1 << " " << cg::roleName (l.role) << " <- " << l.name
                  << " (" << l.files.size() << " files) " << (l.sample ? l.sample->file.getFileName() : juce::String ("-")) << "\n";
        if (l.sample) ++withSample;
    }
    CHECK (withSample >= 6, "library scan mapped folders to lanes (" + juce::String (withSample) + ")");
    CHECK (p.lanes[0].sample != nullptr && p.lanes[0].name == "Kicks", "kick lane got the Kicks folder");

    // 2. Sample scrolling wraps around
    const int before = p.lanes[0].fileIndex;
    p.stepSample (0, -1);
    CHECK (p.lanes[0].fileIndex == (before - 1 + p.lanes[0].files.size()) % p.lanes[0].files.size(), "scrolling back wraps to last sample");
    p.stepSample (0, 1);

    // 3. Generation per genre
    juce::Random r (42);
    for (int g = 0; g < cg::NumGenres; ++g)
    {
        p.gen.genre = g;
        p.gen.variation = 0.4f;
        p.gen.irregular = 0.0f;
        p.generateAll();
        std::cout << "  " << juce::String (cg::genreName (g)).paddedRight (' ', 20) << " kick " << laneString (p.lanes[0]).paddedRight (' ', 20)
                  << " hats " << laneString (p.lanes[3]) << " (len " << p.lanes[3].length << ", " << cg::rates()[(size_t) p.lanes[3].rate].name << ")\n";
    }

    p.gen.genre = cg::House;
    p.gen.irregular = 1.0f;
    p.generateAll();
    int oddLens = 0;
    for (int i = 0; i < p.numLanes; ++i)
        if (p.lanes[(size_t) i].length != 16 && p.lanes[(size_t) i].length != 32 && p.lanes[(size_t) i].length != 64) ++oddLens;
    CHECK (oddLens >= 3, "irregular 100% gives odd lane lengths (" + juce::String (oddLens) + " lanes)");

    p.gen.irregular = 0.0f;
    p.gen.genre = cg::Techno;
    p.generateAll();
    CHECK (laneString (p.lanes[0]) == "X...X...X...X...", "techno kick stays four-on-the-floor at 0% irregular: " + laneString (p.lanes[0]));

    const auto beforeMut = laneString (p.lanes[3]);
    p.mutateAmount = 0.8f;
    p.mutateAll();
    CHECK (laneString (p.lanes[3]) != beforeMut || p.lanes[3].length != 16, "mutate changes the hats");

    // 4. Render audio from host transport
    juce::AudioBuffer<float> audio;
    float peak = render (p, head, 8.0, &audio);
    CHECK (peak > 0.05f, "renders audio while host plays (peak " + juce::String (peak, 3) + ")");
    {
        juce::WavAudioFormat wav;
        auto f = outDir.getChildFile ("render.wav");
        f.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 48000.0, 2, 24, {}, 0));
        if (w) w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    head.playing = false;
    render (p, head, 2.0, nullptr); // let tails ring out
    float silent = render (p, head, 1.0, nullptr);
    CHECK (silent < 0.01f, "silent when stopped and preview off");
    p.previewPlaying = true;
    CHECK (render (p, head, 2.0, nullptr) > 0.05f, "preview button plays without transport");
    p.previewPlaying = false;
    head.playing = true;

    // 5. MIDI export
    p.exportBars = 4;
    auto mid = p.exportMidi();
    juce::MidiFile mf;
    juce::FileInputStream in (mid);
    CHECK (in.openedOk() && mf.readFrom (in), "exported MIDI file reads back: " + mid.getFileName());
    int notes = 0;
    double lastT = 0;
    if (mf.getNumTracks() > 0)
        for (auto* e : *mf.getTrack (0))
            if (e->message.isNoteOn()) { ++notes; lastT = juce::jmax (lastT, e->message.getTimeStamp()); }
    CHECK (notes > 40 && lastT < 4 * 4 * 960 - 0.5, "MIDI has " + juce::String (notes) + " notes within 4 bars");

    // 6. State round trip
    juce::MemoryBlock state;
    p.getStateInformation (state);
    GrooveboxProcessor q;
    q.runSynchronously = true;
    q.setStateInformation (state.getData(), (int) state.getSize());
    bool same = q.numLanes.load() == p.numLanes.load();
    for (int i = 0; i < p.numLanes && same; ++i)
        same = laneString (p.lanes[(size_t) i]) == laneString (q.lanes[(size_t) i])
            && p.lanes[(size_t) i].rate == q.lanes[(size_t) i].rate
            && (p.lanes[(size_t) i].sample == nullptr) == (q.lanes[(size_t) i].sample == nullptr)
            && (p.lanes[(size_t) i].sample == nullptr || p.lanes[(size_t) i].sample->file == q.lanes[(size_t) i].sample->file);
    CHECK (same, "state save/load restores patterns, rates and the selected samples");

    // 7. Lanes add/remove
    const int nl = p.numLanes;
    p.addLane();
    CHECK (p.numLanes == nl + 1, "add lane");
    p.removeLane (0);
    CHECK (p.numLanes == nl && p.lanes[0].name != "Kicks", "remove lane shifts lanes up");

    // 9. Sample fit analysis
    for (int i = 0; i < 300 && p.analyzer.pending() > 0; ++i) juce::Thread::sleep (50);
    juce::Thread::sleep (300);
    {
        const auto kick = root.getChildFile ("Kicks/kick_01.wav");
        const auto hat  = root.getChildFile ("Hats/Closed Hats/hat 1.wav");
        const auto open = root.getChildFile ("Hats/Open Hats/open 1.wav");
        const auto kAsKick = p.fileFit (kick, cg::Role::Kick), hAsKick = p.fileFit (hat, cg::Role::Kick);
        const auto hAsHat = p.fileFit (hat, cg::Role::ClosedHat), kAsHat = p.fileFit (kick, cg::Role::ClosedHat);
        const auto oAsOpen = p.fileFit (open, cg::Role::OpenHat), hAsOpen = p.fileFit (hat, cg::Role::OpenHat);
        std::cout << "  kick as Kick " << kAsKick.score << " (" << kAsKick.reason << ")\n"
                  << "  hat as Kick " << hAsKick.score << " (" << hAsKick.reason << ")\n"
                  << "  hat as Closed Hat " << hAsHat.score << " (" << hAsHat.reason << ")\n"
                  << "  open as Open Hat " << oAsOpen.score << ", closed as Open Hat " << hAsOpen.score << "\n";
        CHECK (kAsKick.score >= 70 && hAsKick.score < 50, "kicks rate well as kicks, hats rate poorly as kicks");
        CHECK (hAsHat.score >= 70 && kAsHat.score < 50, "hats rate well as hats, kicks rate poorly as hats");
        CHECK (oAsOpen.score > hAsOpen.score, "long hats rate better as open hats than short ones");
        cg::Features kf;
        CHECK (p.fileFeatures (kick, kf) && kf.pitchHz > 35.0f && kf.pitchHz < 80.0f,
               "kick pitch detected (" + juce::String (kf.pitchHz, 1) + " Hz, " + cg::noteName (kf.pitchHz) + ")");
    }

    // 10. Locks
    {
        int sl = 0; // a lane whose folder has several samples
        for (int i = 0; i < p.numLanes; ++i) if (p.lanes[(size_t) i].files.size() > 2) { sl = i; break; }
        p.lanes[0].lockPattern = true;
        p.lanes[(size_t) sl].lockSample = true;
        const auto pat = laneString (p.lanes[0]);
        const auto smp = p.lanes[(size_t) sl].sample ? p.lanes[(size_t) sl].sample->file : juce::File();
        bool patternKept = true, sampleKept = true;
        for (int i = 0; i < 10; ++i)
        {
            p.generateAll(); p.mutateAll(); p.generateLane (0); p.shuffleAllSamples(); p.randomSample (sl);
            patternKept = patternKept && laneString (p.lanes[0]) == pat;
            sampleKept = sampleKept && p.lanes[(size_t) sl].sample && p.lanes[(size_t) sl].sample->file == smp;
        }
        CHECK (patternKept, "pattern lock survives Generate, Mutate and lane Gen");
        CHECK (sampleKept, "sample lock survives Shuffle and ?");
        p.lanes[0].lockPattern = false;
        p.lanes[(size_t) sl].lockSample = false;
    }

    // 11. FL channel mode sends MIDI notes
    {
        for (int i = 0; i < p.numLanes; ++i) p.lanes[(size_t) i].mute = (i != 0);
        p.lanes[0].mode = cg::LaneMode::FLChannel;
        p.lanes[0].midiChannel = 3;
        p.lanes[0].length = 16; p.lanes[0].rate = cg::kRate16;
        for (int i = 0; i < 16; ++i) p.lanes[0].steps[(size_t) i] = cg::Step { i % 4 == 0 };
        MidiCount mc;
        head.playing = true;
        render (p, head, 4.0, nullptr, &mc);
        head.playing = false;
        render (p, head, 0.2, nullptr, &mc);
        CHECK (mc.on >= 6 && mc.on == mc.off, "FL channel lane sends balanced MIDI notes (" + juce::String (mc.on) + " on, " + juce::String (mc.off) + " off)");
        p.lanes[0].mode = cg::LaneMode::Sample;
        for (int i = 0; i < p.numLanes; ++i) p.lanes[(size_t) i].mute = false;
        head.playing = true;
    }

    // 12. One output per lane
    {
        p.enableAllBuses();
        p.prepareToPlay (48000.0, 512);
        CHECK (p.getTotalNumOutputChannels() == 64, "32 stereo outputs available (" + juce::String (p.getTotalNumOutputChannels()) + " channels)");
        for (int i = 0; i < p.numLanes; ++i) p.lanes[(size_t) i].mute = (i != 1);
        p.lanes[1].length = 16; p.lanes[1].rate = cg::kRate16;
        for (int s2 = 0; s2 < 16; ++s2) p.lanes[1].steps[(size_t) s2] = cg::Step { s2 % 4 == 0 };
        std::vector<float> peaks;
        render (p, head, 4.0, nullptr, nullptr, &peaks);
        CHECK (peaks.size() >= 2 && peaks[1] > 0.01f && peaks[0] < 0.001f, "lane 2 plays on its own output, not Main");
        for (int i = 0; i < p.numLanes; ++i) p.lanes[(size_t) i].mute = false;
    }

    // 13. Step parameters and lane filter/reverse/attack render without trouble
    {
        for (int i = 0; i < p.numLanes; ++i)
        {
            auto& l = p.lanes[(size_t) i];
            l.filter = (cg::FilterType) (i % 4);
            l.reso = 0.9f;
            l.reverse = (i % 3) == 0;
            l.attack = 0.5f;
            for (auto& st : l.steps) { st.pitch = (float) ((i % 5) - 2); st.pan = 0.5f; st.cutoff = 0.4f; st.start = 0.3f; }
        }
        juce::AudioBuffer<float> a;
        const float pk = render (p, head, 3.0, &a);
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                finite = finite && std::isfinite (a.getSample (ch, i));
        CHECK (pk > 0.0f && finite, "filters, reverse, attack and per-step pitch/pan/cutoff/start render cleanly");
    }

    // 14. Setups: save, load, autosave of the last session
    {
        auto setup = GrooveboxProcessor::setupsFolder().getChildFile ("test setup.gbx");
        CHECK (p.saveSetup (setup), "setup saved");
        GrooveboxProcessor r;
        r.runSynchronously = true;
        CHECK (r.loadSetup (setup) && r.numLanes.load() == p.numLanes.load() && laneString (r.lanes[3]) == laneString (p.lanes[3])
               && r.lanes[0].folder == p.lanes[0].folder && r.lanes[0].files.size() == p.lanes[0].files.size(),
               "setup loads back lanes, patterns and folders");
        setup.deleteFile();

        p.editorEverOpened = true;
        p.autosave();
        CHECK (GrooveboxProcessor::lastSessionFile().existsAsFile(), "last session autosaved");
        GrooveboxProcessor fresh; // a new instance starts from the last session
        for (int i = 0; i < 150 && fresh.lanes[0].files.size() != p.lanes[0].files.size(); ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
        CHECK (fresh.lanes[0].folder == p.lanes[0].folder && fresh.lanes[0].files.size() == p.lanes[0].files.size(),
               "a new Groovebox remembers the last folders (background scan finished)");
        GrooveboxProcessor::lastSessionFile().deleteFile();
    }

    // 15. Long loops evolve bar by bar
    {
        for (int i = 0; i < p.numLanes; ++i) { p.lanes[(size_t) i].lockPattern = false; p.lanes[(size_t) i].mute = false; }
        p.gen.genre = cg::House; p.gen.irregular = 0.0f; p.gen.variation = 0.5f; p.gen.bars = 16;
        p.generateAll();
        int hatLane = -1;
        for (int i = 0; i < p.numLanes; ++i) if (p.lanes[(size_t) i].role == cg::Role::ClosedHat) { hatLane = i; break; }
        const auto& hl = p.lanes[(size_t) juce::jmax (0, hatLane)];
        int differentBars = 0;
        for (int b = 1; b < 16; ++b)
        {
            bool same = true;
            for (int s2 = 0; s2 < 16; ++s2)
                same = same && hl.steps[(size_t) (b * 16 + s2)].on == hl.steps[(size_t) s2].on
                            && std::abs (hl.steps[(size_t) (b * 16 + s2)].vel - hl.steps[(size_t) s2].vel) < 0.01f;
            if (! same) ++differentBars;
        }
        CHECK (hl.length == 256, "16-bar loop gives a 256-step hat lane (" + juce::String (hl.length) + ")");
        CHECK (differentBars >= 2, "hat bars evolve across the loop (" + juce::String (differentBars) + " of 15 bars differ from bar 1)");
        juce::AudioBuffer<float> a;
        CHECK (render (p, head, 2.0, &a) > 0.01f, "long lanes render");
        p.gen.bars = 1;
    }

    // 16. New genres
    {
        p.lanes[0].role = cg::Role::Kick; // lane 1 was removed earlier; make sure there is a kick lane
        p.gen.variation = 0.0f;
        p.gen.genre = cg::DrumAndBass;
        p.generateAll();
        int kick = -1;
        for (int i = 0; i < p.numLanes; ++i) if (p.lanes[(size_t) i].role == cg::Role::Kick) { kick = i; break; }
        CHECK (kick >= 0 && laneString (p.lanes[(size_t) kick]) == "X.........X.....", "drum & bass two-step kick: "
               + (kick >= 0 ? laneString (p.lanes[(size_t) kick]) : juce::String ("no kick lane")));
        CHECK (cg::genreBpm (cg::DrumAndBass) > 160.0 && cg::genreBpm (cg::HipHop) < 100.0, "genres carry their tempo (DnB 174, hip-hop 90)");
        p.gen.genre = cg::Trap; p.gen.variation = 0.4f;
        p.generateAll();
        int rolls = 0;
        for (int i = 0; i < p.numLanes; ++i)
            if (p.lanes[(size_t) i].role == cg::Role::ClosedHat)
                for (int s2 = 0; s2 < p.lanes[(size_t) i].length; ++s2) if (p.lanes[(size_t) i].steps[(size_t) s2].ratchet > 1) ++rolls;
        CHECK (rolls >= 1, "trap hats get rolls (" + juce::String (rolls) + " ratcheted steps)");
    }

    // 17. Song mode
    {
        p.gen.genre = cg::Techno; p.gen.variation = 0.3f;
        p.generateAll();
        p.makeSong (64);
        int kick = -1;
        for (int i = 0; i < p.numLanes; ++i) if (p.lanes[(size_t) i].role == cg::Role::Kick) { kick = i; break; }
        int breakStart = -1, buildEnd = -1, mainStart = -1;
        for (const auto& sc : p.songSections)
        {
            if (sc.name == "Break" && breakStart < 0) breakStart = sc.startBar;
            if (sc.name == "Build" && buildEnd < 0) buildEnd = sc.startBar + sc.bars - 1;
            if (sc.name == "Main" && mainStart < 0) mainStart = sc.startBar;
        }
        juce::String plan;
        for (const auto& sc : p.songSections) plan << sc.name << " " << sc.bars << ", ";
        std::cout << "  song plan: " << plan << "\n";
        CHECK (p.songBars == 64 && p.songSections.size() == 6, "64-bar song with 6 sections");
        CHECK (kick >= 0 && ! p.laneActiveAtBar (kick, breakStart) && ! p.laneActiveAtBar (kick, buildEnd) && p.laneActiveAtBar (kick, mainStart),
               "kick rests in the break and the last build bar, plays in the main section");

        p.songMode = true;
        for (int i = 0; i < p.numLanes; ++i) p.lanes[(size_t) i].mute = (i != kick);
        head.playing = true;
        head.ppq = breakStart * 4.0 + 0.5;
        render (p, head, 0.2, nullptr); // let earlier tails decay
        render (p, head, 1.5, nullptr);
        const float breakPeak = render (p, head, 1.0, nullptr);
        head.ppq = mainStart * 4.0;
        const float mainPeak = render (p, head, 2.0, nullptr);
        CHECK (breakPeak < 0.01f && mainPeak > 0.05f, "song playback follows the arrangement (break " + juce::String (breakPeak, 3)
               + ", main " + juce::String (mainPeak, 3) + ")");
        for (int i = 0; i < p.numLanes; ++i) p.lanes[(size_t) i].mute = false;

        auto mid = p.exportMidi();
        juce::MidiFile mf;
        juce::FileInputStream in (mid);
        mf.readFrom (in);
        int kickInBreak = 0, notes = 0;
        double last = 0;
        if (mf.getNumTracks() > 0)
            for (auto* e : *mf.getTrack (0))
                if (e->message.isNoteOn())
                {
                    ++notes;
                    last = juce::jmax (last, e->message.getTimeStamp());
                    // A hit from the bar before may be humanized a hair late into the break: allow 1/16 note.
                    const double beat = e->message.getTimeStamp() / 960.0;
                    // Hits from the neighbouring sections may be humanized a hair into the break: ignore 1/16 at each edge.
                    if (e->message.getNoteNumber() == cg::kBaseNote + kick && beat >= breakStart * 4.0 + 0.25
                        && beat < (breakStart + 8) * 4.0 - 0.25)
                        ++kickInBreak;
                }
        CHECK (notes > 200 && kickInBreak == 0 && last > (64 - 8) * 4 * 960 && last < 64 * 4 * 960,
               "song exports as one long MIDI (" + juce::String (notes) + " notes, last bar " + juce::String ((int) (last / 3840)) + ")");

        juce::MemoryBlock st;
        p.getStateInformation (st);
        GrooveboxProcessor s2;
        s2.runSynchronously = true;
        s2.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (s2.songMode.load() && s2.songBars == 64 && s2.songSections.size() == 6 && ! s2.laneActiveAtBar (kick, breakStart),
               "song arrangement survives save/load");
        p.songMode = false;
    }

    // 18. Piano roll mode: internal sequencer stops, notes from FL play the lanes with their step settings
    {
        p.songMode = false;
        p.pianoRollMode = true;
        head.playing = true; head.ppq = 0.0;
        render (p, head, 1.5, nullptr); // tails decay
        const float silent = render (p, head, 1.0, nullptr);

        // Lane 1 (index 0): make step 0 pitched up an octave; note at step 0 should pick that up.
        p.lanes[0].mute = false;
        p.lanes[0].reverse = false; p.lanes[0].attack = 0.0f; p.lanes[0].filter = cg::FilterType::Off; // undo test 13
        p.setLaneFolder (0, root.getChildFile ("Kicks"), false); // a clean tone, so pitch is measurable (synchronous in tests)
        for (auto& st : p.lanes[0].steps) st = cg::Step {};
        p.lanes[0].length = 16; p.lanes[0].rate = cg::kRate16;
        p.lanes[0].steps[0].on = true; p.lanes[0].steps[0].pitch = 12.0f;

        auto playNoteAt = [&] (double atPpq) -> float
        {
            head.ppq = atPpq;
            juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, cg::kBaseNote, (juce::uint8) 110), 0);
            p.processBlock (buf, midi);
            float pk = buf.getMagnitude (0, 512);
            juce::MidiBuffer none;
            for (int b = 0; b < 20; ++b) { p.processBlock (buf, none); pk = juce::jmax (pk, buf.getMagnitude (0, 512)); }
            return pk;
        };
        auto crossingsAt = [&] (double atPpq) -> int
        {
            head.ppq = atPpq;
            juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, cg::kBaseNote, (juce::uint8) 110), 0);
            int zc = 0; float prev = 0.0f;
            for (int b = 0; b < 8; ++b)
            {
                p.processBlock (buf, midi);
                midi.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const float x = buf.getSample (0, i);
                    if ((x >= 0.0f) != (prev >= 0.0f)) ++zc;
                    prev = x;
                }
            }
            juce::MidiBuffer none;
            for (int b = 0; b < 200; ++b) p.processBlock (buf, none); // let it ring out
            return zc;
        };
        const int zcPitched = crossingsAt (16.0);        // step 0: +12 semitones
        const int zcPlain   = crossingsAt (16.0 + 1.0);  // step 4: empty step, lane's normal sound
        CHECK (zcPitched > zcPlain * 1.6, "a piano roll note picks up its step's pitch (+12 st: " + juce::String (zcPitched)
               + " vs " + juce::String (zcPlain) + " zero crossings)");
        const float played = playNoteAt (16.0); // bar 5 beat 1 = step 0 of the 16-step lane
        CHECK (silent < 0.001f && played > 0.01f, "piano roll mode: own sequencer silent (" + juce::String (silent, 4)
               + "), notes from the piano roll play (" + juce::String (played, 3) + ")");
        p.pianoRollMode = false;
    }

    // 19. Undo / redo
    {
        p.checkpoint();
        const auto before = laneString (p.lanes[2]);
        const int lanesBefore = p.numLanes.load();
        const auto folderBefore = p.lanes[1].folder;
        p.generateAll();
        p.checkpoint();
        const auto generated = laneString (p.lanes[2]);
        CHECK (p.undo() && laneString (p.lanes[2]) == before, "Ctrl+Z undoes Generate");
        CHECK (p.redo() && laneString (p.lanes[2]) == generated, "Ctrl+Y redoes it");

        p.removeLane (1);
        p.checkpoint();
        CHECK (p.numLanes.load() == lanesBefore - 1, "lane removed");
        CHECK (p.undo() && p.numLanes.load() == lanesBefore && p.lanes[1].folder == folderBefore
               && laneString (p.lanes[2]) == generated, "undo brings the removed lane back with its folder and pattern");

        p.lanes[3].steps[0].on = ! p.lanes[3].steps[0].on; // a manual step edit
        p.checkpoint();
        const bool edited = p.lanes[3].steps[0].on;
        CHECK (p.undo() && p.lanes[3].steps[0].on != edited, "undo reverts a single step edit");
    }

    // 20. Groove from a reference
    {
        // Synthesize 8 bars at 124 BPM: kicks on the beats, snares on 2 and 4, hats on every 16th with the
        // odd 16ths 30% of a step late (a swung feel) and the offbeat 8ths accented.
        const double rate = 44100.0, bpmRef = 124.0, stepSec = 60.0 / bpmRef / 4.0;
        const int total = (int) (rate * stepSec * 16 * 8);
        juce::AudioBuffer<float> loop (1, total);
        loop.clear();
        juce::Random nr (7);
        auto* d = loop.getWritePointer (0);
        auto addHit = [&] (double tSec, int kind, float gain)
        {
            const int s0 = (int) (tSec * rate);
            const int len = (int) (rate * (kind == 0 ? 0.25 : kind == 1 ? 0.15 : 0.04));
            float prev = 0.0f;
            for (int i = 0; i < len && s0 + i < total; ++i)
            {
                const float t = (float) i / (float) rate;
                float v;
                if (kind == 0) v = std::sin (juce::MathConstants<float>::twoPi * (50.0f + 120.0f * std::exp (-t * 40.0f)) * t) * std::exp (-t * 14.0f);
                else if (kind == 1) v = (nr.nextFloat() * 2.0f - 1.0f) * std::exp (-t * 30.0f) * 0.6f
                                        + std::sin (juce::MathConstants<float>::twoPi * 220.0f * t) * std::exp (-t * 25.0f) * 0.4f;
                else { const float w = nr.nextFloat() * 2.0f - 1.0f; v = (w - prev) * 0.5f * std::exp (-t * 120.0f); prev = w; }
                d[s0 + i] += v * gain;
            }
        };
        for (int bar = 0; bar < 8; ++bar)
            for (int s = 0; s < 16; ++s)
            {
                const double t0 = (bar * 16 + s) * stepSec;
                if (s % 4 == 0) addHit (t0, 0, 0.9f);
                if (s == 4 || s == 12) addHit (t0, 1, 0.7f);
                const double late = (s % 2 == 1) ? 0.3 * stepSec : 0.0;
                addHit (t0 + late, 2, s % 4 == 2 ? 0.8f : 0.35f);
            }

        const auto g = cg::analyzeGroove (loop.getReadPointer (0), total, rate, 0.0, 120.0, "test");
        std::cout << "  " << cg::describeGroove (g) << "\n";
        for (int b = 0; b < 3; ++b)
        {
            juce::String line = "  band " + juce::String (b) + ":";
            for (int s = 0; s < 16; ++s)
                line << " " << (g.has[(size_t) b][(size_t) s] ? juce::String (g.offset[(size_t) b][(size_t) s], 2) : juce::String ("  . "));
            std::cout << line << "\n";
        }
        CHECK (g.valid && std::abs (g.bpm - bpmRef) < 1.0, "reference tempo detected (" + juce::String (g.bpm, 2) + " BPM)");
        CHECK (g.prob[0][0] > 0.8f && g.prob[0][4] > 0.8f && g.prob[0][8] > 0.8f && g.prob[0][12] > 0.8f
               && g.prob[0][2] < 0.3f && g.prob[0][6] < 0.3f, "kicks found on the beats, bar start found");
        float odd = 0.0f, even = 0.0f;
        for (int s = 0; s < 16; ++s) (s % 2 ? odd : even) += g.offset[2][(size_t) s] / 8.0f;
        CHECK (odd > 0.2f && odd < 0.4f && std::abs (even) < 0.08f,
               "hat swing measured (odd 16ths +" + juce::String (odd, 2) + " steps, even " + juce::String (even, 2) + ")");
        CHECK (g.accent[2][2] > g.accent[2][1] + 0.2f, "offbeat hat accents measured");

        // A reference that starts mid-bar (one beat plus a bit in) still finds bar 1 from the backbeat.
        const int skip = (int) (rate * (stepSec * 4 + 0.03));
        const auto gm = cg::analyzeGroove (loop.getReadPointer (0) + skip, total - skip, rate, 0.0, 120.0, "test");
        CHECK (gm.accent[1][4] > gm.accent[1][0] + 0.25f && gm.accent[1][12] > gm.accent[1][8] + 0.25f && std::abs (gm.offset[2][1] - g.offset[2][1]) < 0.05f,
               "reference starting mid-bar: loudest mids still on 2 and 4, same swing");

        // Half-tempo hint picks the half-tempo reading.
        const auto gh = cg::analyzeGroove (loop.getReadPointer (0), total, rate, 0.0, 64.0, "test");
        CHECK (std::abs (gh.bpm - bpmRef / 2.0) < 1.0, "half/double tempo follows the host tempo hint (" + juce::String (gh.bpm, 1) + ")");

        const auto gn = cg::analyzeGroove (loop.getReadPointer (0), total, rate, 0.0, 174.0, "test");
        CHECK (std::abs (gn.bpm - bpmRef) < 1.0, "a host tempo far from the reference doesn't fool detection (" + juce::String (gn.bpm, 1) + ")");

        // Load from a file through the processor.
        auto wav = outDir.getChildFile ("ref-loop.wav");
        wav.deleteFile();
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), rate, 1, 16, {}, 0));
            w->writeFromAudioSampleBuffer (loop, 0, total);
        }
        p.lastBpm = 124.0;
        p.loadGroove (wav, 0.0);
        CHECK (p.groove.valid && std::abs (p.groove.bpm - bpmRef) < 1.0 && p.grooveFile == wav, "groove loaded from a file");

        // Applying the groove moves a hat on an odd 16th late; kicks on the beat stay put.
        cg::Lane hat;
        hat.role = cg::Role::ClosedHat;
        hat.length = 16;
        hat.humanize = 0.0f;
        hat.swing = 0.0f;
        hat.steps[1].on = true;
        hat.steps[4].on = true;
        std::vector<double> plain, grooved;
        cg::collectEvents (hat, 0, 1, 0.0, 4.0, 124.0, 1.0f, 0.0f, [&] (double t, float, const cg::Step&, double) { plain.push_back (t); });
        cg::collectEvents (hat, 0, 1, 0.0, 4.0, 124.0, 1.0f, 0.0f, [&] (double t, float, const cg::Step&, double) { grooved.push_back (t); },
                           -1, &p.groove, 1.0f);
        CHECK (plain.size() == 2 && grooved.size() == 2 && grooved[0] - plain[0] > 0.05 * 0.25 * 4 && std::abs (grooved[1] - plain[1]) < 0.02,
               "groove shifts the odd hat late (" + juce::String ((grooved[0] - plain[0]) / 0.25, 2) + " steps) and leaves the beat");

        // Generating from the reference.
        p.gen.genre = cg::Reference;
        p.gen.irregular = 0.0f;
        p.gen.bars = 1;
        cg::Lane kick;
        kick.role = cg::Role::Kick;
        juce::Random gr (3);
        cg::generateLane (kick, p.gen, gr);
        CHECK (kick.steps[0].on && kick.steps[4].on && kick.steps[8].on && kick.steps[12].on && ! kick.steps[2].on,
               "From reference groove: kick pattern follows the reference (" + laneString (kick) + ")");

        // Saved with the state.
        auto xml = p.createStateXml();
        GrooveboxProcessor s3;
        s3.runSynchronously = true;
        s3.applyStateXml (*xml);
        CHECK (s3.groove.valid && std::abs (s3.groove.offset[2][1] - p.groove.offset[2][1]) < 0.001f
               && std::abs (s3.grooveAmount - p.grooveAmount) < 0.001f, "groove restored from saved state");
        p.gen.genre = cg::House;
    }

    // 8. UI snapshot
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
    {
        p.removeLane (0);
        q.gen.bars = 8;
        q.generateAll();
        q.makeSong (64);
        q.songMode = true;
        q.previewPlaying = true;
        q.loadGroove (outDir.getChildFile ("ref-loop.wav"), 0.0);
        FakeHead h2;
        h2.playing = false;
        q.setPlayHead (&h2);
        q.prepareToPlay (48000.0, 512);
        render (q, h2, 0.7, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        ed->setSize (1400, 800);
        for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto pngFile = outDir.getChildFile ("ui.png");
        pngFile.deleteFile();
        juce::FileOutputStream os (pngFile);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << "ok:   UI snapshot written\n";
        {
            cg::HelpOverlay helpView;
            helpView.setSize (1400, 800);
            helpView.setVisible (true);
            auto himg = helpView.createComponentSnapshot (helpView.getLocalBounds(), true, 1.0f);
            auto hf = outDir.getChildFile ("help.png");
            hf.deleteFile();
            juce::FileOutputStream hs (hf);
            juce::PNGImageFormat().writeImageToStream (himg, hs);
        }
        ed.reset();
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : juce::String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}
