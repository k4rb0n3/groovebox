#pragma once

#include "LaneData.h"
#include "Generator.h"

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
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

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

    // Bumped whenever lanes change from outside the editor (generate, state load, library scan).
    std::atomic<int> modelVersion { 0 };

    //== Actions (message thread) ==================================================
    void setLaneFolder (int lane, const juce::File& folder, bool guessRoleFromName = true);
    void stepSample (int lane, int delta);
    void randomSample (int lane);
    void shuffleAllSamples();
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

private:
    struct Voice
    {
        bool active = false;
        cg::SamplePtr smp;
        double pos = 0.0, inc = 1.0;
        float gainL = 1.0f, gainR = 1.0f;
        int lane = 0, chokeGroup = 0, out = 0;
        int startOffset = 0;
        int chokeAt = -1;
        int age = 0, decaySamples = 0;
        bool fading = false;
        int fadeLen = 1, fadeLeft = 0;
    };

    std::array<Voice, 128> voices;
    double sr = 44100.0;
    double internalPpq = 0.0;
    double lastPpqEnd = 0.0;
    juce::uint32 passCounter = 0;
    juce::Random rng;

    std::vector<cg::SamplePtr> retired; // keeps old samples alive so the audio thread never frees memory

    void triggerVoice (int lane, float vel, int offset); // audio thread, under lock
    cg::SamplePtr loadSample (const juce::File&);
    void swapSample (int lane, cg::SamplePtr);
    void timerCallback() override;
    void initDefaultLanes();
    static BusesProperties makeBuses();

    static juce::String stepsToString (const cg::Lane&);
    static void stepsFromString (cg::Lane&, const juce::String&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveboxProcessor)
};
