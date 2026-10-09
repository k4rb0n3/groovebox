#pragma once

#include "LaneData.h"
#include "Generator.h"
#include "SampleAnalysis.h"

class GrooveboxProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GrooveboxProcessor();
    ~GrooveboxProcessor() override;

    //== AudioProcessor ==========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Groovebox"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== Setups (message thread) =====================================================
    std::unique_ptr<juce::XmlElement> createStateXml();
    void applyStateXml (const juce::XmlElement&);
    static juce::File setupsFolder();
    static juce::File lastSessionFile();
    bool saveSetup (const juce::File&);
    bool loadSetup (const juce::File&);
    void autosave();               // writes the last-session file when something changed
    bool editorEverOpened = false; // only real sessions autosave (not plugin scans)

    //== Background folder scanning ===================================================
    std::atomic<int> ioBusy { 0 };
    bool runSynchronously = false;  // tests: scan folders on the calling thread
    static juce::Array<juce::File> listAudioFiles (const juce::File& folder, bool recursive = true);

    //== Model (message thread writes under `lock`, audio thread reads under `lock`) ==
    juce::AudioProcessorValueTreeState apvts;
    juce::SpinLock lock;
    std::array<cg::Lane, cg::kMaxLanes> lanes;
    std::atomic<int> numLanes { 16 };

    std::array<std::atomic<int>, cg::kMaxLanes>  playStep;
    std::array<std::atomic<bool>, cg::kMaxLanes> auditionFlag;
    std::array<std::atomic<float>, cg::kMaxLanes> laneMeter;
    std::atomic<bool>   previewPlaying { false };
    std::atomic<double> lastBpm { 124.0 };

    cg::GenSettings gen;
    float mutateAmount = 0.3f;
    juce::uint32 seed = 1;
    juce::File libraryRoot;
    int exportBars = 4;
    bool fitFilter = false; // scrolling skips samples that clearly don't fit

    std::atomic<int> modelVersion { 0 };

    //== Song mode: a full-track arrangement over the lanes ===========================
    struct Section { juce::String name; int startBar = 0, bars = 0; int type = 0; };
    std::atomic<bool> songMode { false };
    int songBars = 64;
    std::vector<Section> songSections;
    std::array<juce::uint32, 512> songMask {}; // bit per lane: does the lane play in this bar
    std::atomic<int> currentBar { -1 };
    void makeSong (int bars);
    bool laneActiveAtBar (int lane, int bar) const
    {
        return bar >= 0 && bar < juce::jmin (songBars, (int) songMask.size()) && ((songMask[(size_t) bar] >> lane) & 1u) != 0;
    }

    std::atomic<double> previewBpm { 124.0 }; // tempo for the Play button when FL isn't playing

    // Piano roll mode: the internal sequencer stops and Groovebox plays the notes FL's piano roll sends
    // (C3 = lane 1 ...). Each note picks up the step settings (pitch, pan, cutoff, start) of the step it sits on.
    std::atomic<bool> pianoRollMode { false };

    // Automatable per-lane parameters.
    static juce::String paramId (int lane, const char* what) { return "l" + juce::String (lane + 1) + "_" + what; }
    static float cutoffHz (float norm) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, norm)); }

    //== Sample fit ================================================================
    cg::SampleAnalyzer analyzer;
    cg::Fit fileFit (const juce::File&, cg::Role role) const;
    cg::Fit laneFit (int lane) const;
    bool fileFeatures (const juce::File& f, cg::Features& out) const { return analyzer.get (f, out); }

    //== Actions (message thread) ==================================================
    void setLaneFolder (int lane, const juce::File& folder, bool guessRoleFromName = true,
                        const juce::File& preferredFile = {}); // lists the folder in the background
    void stepSample (int lane, int delta);
    void randomSample (int lane);      // favours good fits; respects the sample lock
    void shuffleAllSamples();          // favours good fits; skips locked lanes
    void loadSampleIndex (int lane, int index);
    void scanLibrary (const juce::File& root);

    void generateAll();
    void generateLane (int lane);
    void mutateAll();
    void mutateOneLane (int lane);
    void newSeed();

    void addLane();
    void removeLane (int lane);
    void audition (int lane) { if (lane >= 0 && lane < cg::kMaxLanes) auditionFlag[(size_t) lane] = true; }

    juce::File exportMidi();
    static juce::File exportFolder();

    juce::AudioFormatManager formats;

    //== Undo / redo (message thread) ==================================================
    // checkpoint() is called regularly by the editor: when anything changed since the last
    // checkpoint (and no mouse button is held), the previous state goes on the undo stack.
    void checkpoint();
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

private:
    struct LaneSnap
    {
        cg::Role role = cg::Role::Other;
        juce::String name;
        int length = 16, rate = cg::kRate16, choke = 0, out = -1;
        std::vector<cg::Step> steps;
        float decay = 1, attack = 0, reso = 0.2f, swing = 0, humanize = 0.15f;
        float level = -6, pan = 0, pitch = 0, cutoff = 1;
        cg::FilterType filter = cg::FilterType::Off;
        bool reverse = false, mute = false, solo = false, lockSample = false, lockPattern = false;
        juce::File folder, file;
        bool operator== (const LaneSnap&) const;
    };
    struct Snapshot
    {
        int numLanes = 0;
        std::vector<LaneSnap> lanes;
        bool songMode = false;
        int songBars = 64;
        std::vector<Section> sections;
        std::array<juce::uint32, 512> mask {};
        bool operator== (const Snapshot&) const;
    };
    Snapshot takeSnapshot() const;
    void restoreSnapshot (const Snapshot&);
    std::vector<Snapshot> undoStack, redoStack;
    Snapshot current;
    bool hasCurrent = false;

    struct Voice
    {
        bool active = false;
        cg::SamplePtr smp;
        double pos = 0.0, inc = 1.0;
        float gainL = 1.0f, gainR = 1.0f;
        int lane = 0, chokeGroup = 0, out = 0;
        int startOffset = 0;
        int chokeAt = -1;
        int age = 0, decaySamples = 0, attackSamples = 0;
        bool fading = false;
        int fadeLen = 1, fadeLeft = 0;

        // State-variable filter (per voice, set at trigger time)
        cg::FilterType ftype = cg::FilterType::Off;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
        float ic1[2] {}, ic2[2] {};
    };

    struct PendingOff { int samplesLeft = 0, channel = 1, note = 60; };

    std::array<Voice, 128> voices;
    std::array<PendingOff, 512> pendingOffs;
    int numPendingOffs = 0;
    juce::MidiBuffer midiOut;

    double sr = 44100.0;
    double internalPpq = 0.0;
    double lastPpqEnd = 0.0;
    juce::uint32 passCounter = 0;
    juce::Random rng;

    std::array<std::atomic<float>*, cg::kMaxLanes> pLevel {}, pPan {}, pPitch {}, pCutoff {};

    std::vector<cg::SamplePtr> retired; // keeps old samples alive so the audio thread never frees memory

    juce::ThreadPool io { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::array<int, cg::kMaxLanes> laneToken {};
    int stateGeneration = 0, libraryToken = 0;
    juce::String lastAutosaved;

    void applyLaneFolder (int lane, const juce::File& folder, const juce::Array<juce::File>& files,
                          bool guessRoleFromName, const juce::File& preferredFile);
    void applyLibrary (const juce::File& root, const std::vector<std::pair<juce::File, juce::Array<juce::File>>>& dirs);
    static std::vector<std::pair<juce::File, juce::Array<juce::File>>> findLibraryFolders (const juce::File& root);

    void triggerVoice (int lane, float vel, int offset, const cg::Step& st);        // audio thread, under lock
    void sendLaneMidi (int lane, float vel, int offset, const cg::Step& st, int lengthSamples); // audio thread
    cg::SamplePtr loadSample (const juce::File&);
    void swapSample (int lane, cg::SamplePtr);
    void timerCallback() override;
    void initDefaultLanes();
    int  pickGoodSample (int lane);
    void copyLaneParams (int from, int to);
    void resetLaneParams (int lane);
    static BusesProperties makeBuses();

    static juce::String stepsToString (const cg::Lane&);
    static void stepsFromString (cg::Lane&, const juce::String&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveboxProcessor)
};
