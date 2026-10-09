#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace cg;

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout makeParams()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master",
                                                  NormalisableRange<float> (-36.0f, 6.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing amount",
                                                  NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, "Humanize amount",
                                                  NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "seq", 1 }, "Sequencer on", true));

    for (int i = 0; i < kMaxLanes; ++i)
    {
        const String n = "Lane " + String (i + 1) + " ";
        l.add (std::make_unique<AudioParameterFloat> (
            ParameterID { GrooveboxProcessor::paramId (i, "level"), 1 }, n + "Level",
            NormalisableRange<float> (-48.0f, 6.0f, 0.01f), -6.0f,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
        l.add (std::make_unique<AudioParameterFloat> (
            ParameterID { GrooveboxProcessor::paramId (i, "pan"), 1 }, n + "Pan",
            NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const int p = roundToInt (v * 100.0f);
                return p == 0 ? String ("C") : (p < 0 ? "L" + String (-p) : "R" + String (p));
            })));
        l.add (std::make_unique<AudioParameterFloat> (
            ParameterID { GrooveboxProcessor::paramId (i, "pitch"), 1 }, n + "Pitch",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("st")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 2) + " st"; })));
        l.add (std::make_unique<AudioParameterFloat> (
            ParameterID { GrooveboxProcessor::paramId (i, "cutoff"), 1 }, n + "Cutoff",
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const float hz = GrooveboxProcessor::cutoffHz (v);
                return hz >= 1000.0f ? String (hz / 1000.0f, 1) + " kHz" : String (roundToInt (hz)) + " Hz";
            })));
    }
    return l;
}

const char* const kAudioPattern = "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

struct NaturalFileOrder
{
    static int compareElements (const juce::File& a, const juce::File& b)
    {
        return a.getFullPathName().compareNatural (b.getFullPathName());
    }
};
} // namespace

juce::AudioProcessor::BusesProperties GrooveboxProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Lane 1 / Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 2; i <= kNumOuts; ++i)
        b = b.withOutput ("Lane " + juce::String (i), juce::AudioChannelSet::stereo(), false);
    return b;
}

GrooveboxProcessor::GrooveboxProcessor()
    : juce::AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "Params", makeParams())
{
    formats.registerBasicFormats();

    for (int i = 0; i < kMaxLanes; ++i)
    {
        playStep[(size_t) i] = -1;
        auditionFlag[(size_t) i] = false;
        laneMeter[(size_t) i] = 0.0f;
        pLevel[(size_t) i]  = apvts.getRawParameterValue (paramId (i, "level"));
        pPan[(size_t) i]    = apvts.getRawParameterValue (paramId (i, "pan"));
        pPitch[(size_t) i]  = apvts.getRawParameterValue (paramId (i, "pitch"));
        pCutoff[(size_t) i] = apvts.getRawParameterValue (paramId (i, "cutoff"));
    }

    midiOut.ensureSize (8192);
    seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    initDefaultLanes();
    generateAll();

    // A fresh Groovebox picks up where you left off: last folders, samples and patterns.
    // (When FL loads a project it restores that project's state right after this.)
    if (lastSessionFile().existsAsFile())
        loadSetup (lastSessionFile());

    startTimer (1000);
}

GrooveboxProcessor::~GrooveboxProcessor()
{
    alive->store (false);
    stopTimer();
    io.removeAllJobs (true, 4000);
    if (editorEverOpened)
        autosave();
}

void GrooveboxProcessor::initDefaultLanes()
{
    const Role defaults[] = { Role::Kick, Role::Snare, Role::Clap, Role::ClosedHat, Role::ClosedHat, Role::OpenHat,
                              Role::Ride, Role::Shaker, Role::Perc, Role::Perc, Role::Rim, Role::Tom,
                              Role::Click, Role::Cymbal, Role::Texture, Role::FX };

    const juce::SpinLock::ScopedLockType sl (lock);
    for (int i = 0; i < kMaxLanes; ++i)
        lanes[(size_t) i] = Lane {};

    for (int i = 0; i < 16; ++i)
    {
        lanes[(size_t) i].role = defaults[i];
        lanes[(size_t) i].name = roleName (defaults[i]);
    }
    numLanes = 16;
}

//==============================================================================
void GrooveboxProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& v : voices)
    {
        v.active = false;
        v.smp.reset();
    }
    numPendingOffs = 0;
}

bool GrooveboxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return layouts.inputBuses.isEmpty() || layouts.getMainInputChannelSet().isDisabled();
}

void GrooveboxProcessor::triggerVoice (int li, float vel, int offset, const Step& st)
{
    const auto& lane = lanes[(size_t) li];
    if (lane.sample == nullptr || lane.sample->buffer.getNumSamples() < 4)
        return;

    if (lane.choke > 0)
        for (auto& v : voices)
            if (v.active && v.chokeGroup == lane.choke && ! v.fading)
                v.chokeAt = juce::jmax (offset, v.startOffset);

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr) // steal the oldest
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.age > target->age) target = &v;
    }

    auto& v = *target;
    const int len = lane.sample->buffer.getNumSamples();
    const double ratio = std::pow (2.0, (double) (pPitch[(size_t) li]->load() + st.pitch) / 12.0) * lane.sample->sampleRate / sr;
    const double startSamples = (double) juce::jlimit (0.0f, 1.0f, st.start) * 0.5 * (double) len;

    v.active = true;
    v.smp = lane.sample;
    if (lane.reverse) { v.pos = (double) (len - 2) - startSamples; v.inc = -ratio; }
    else              { v.pos = startSamples;                      v.inc = ratio; }

    const float velGain = juce::Decibels::decibelsToGain (-30.0f * (1.0f - juce::jlimit (0.0f, 1.0f, vel)));
    const float g = juce::Decibels::decibelsToGain (pLevel[(size_t) li]->load(), -60.0f) * velGain;
    const float pan = juce::jlimit (-1.0f, 1.0f, pPan[(size_t) li]->load() + st.pan);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    v.gainL = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    v.gainR = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;

    // Filter: the lane filter, or an automatic low-pass when only per-step cutoff is used.
    v.ftype = lane.filter;
    float norm = pCutoff[(size_t) li]->load();
    if (v.ftype == FilterType::Off && st.cutoff < 0.999f) { v.ftype = FilterType::LowPass; norm = 1.0f; }
    if (v.ftype != FilterType::Off)
    {
        const float fc = juce::jlimit (20.0f, (float) (sr * 0.45),
                                       cutoffHz (norm) * std::pow (2.0f, -(1.0f - juce::jlimit (0.0f, 1.0f, st.cutoff)) * 6.0f));
        const float gg = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
        v.k  = 2.0f - 1.9f * juce::jlimit (0.0f, 1.0f, lane.reso);
        v.a1 = 1.0f / (1.0f + gg * (gg + v.k));
        v.a2 = gg * v.a1;
        v.a3 = gg * v.a2;
        v.ic1[0] = v.ic1[1] = v.ic2[0] = v.ic2[1] = 0.0f;
    }

    v.lane = li;
    v.chokeGroup = lane.choke;
    const int outIdx = lane.out < 0 ? li : lane.out;
    v.out = juce::jlimit (0, kNumOuts - 1, outIdx);
    v.startOffset = offset;
    v.chokeAt = -1;
    v.age = 0;
    v.attackSamples = (int) (sr * 0.2 * (double) (lane.attack * lane.attack));
    v.decaySamples = lane.decay >= 0.999f ? std::numeric_limits<int>::max()
                                          : (int) (sr * 0.005 * std::pow (400.0, (double) lane.decay));
    v.fading = false;
    v.fadeLen = juce::jmax (1, (int) (sr * 0.012));
    v.fadeLeft = v.fadeLen;

    laneMeter[(size_t) li] = juce::jmax (laneMeter[(size_t) li].load(), vel);
}

void GrooveboxProcessor::sendLaneMidi (int li, float vel, int offset, const Step& st, int lengthSamples)
{
    const auto& lane = lanes[(size_t) li];
    const int ch = juce::jlimit (1, 16, lane.midiChannel);
    const int note = juce::jlimit (0, 127, lane.midiNote + juce::roundToInt (pPitch[(size_t) li]->load() + st.pitch));

    // If this note is still held, release it first so it can retrigger.
    for (int i = 0; i < numPendingOffs; ++i)
        if (pendingOffs[(size_t) i].channel == ch && pendingOffs[(size_t) i].note == note)
        {
            midiOut.addEvent (juce::MidiMessage::noteOff (ch, note), offset);
            pendingOffs[(size_t) i] = pendingOffs[(size_t) --numPendingOffs];
            break;
        }

    midiOut.addEvent (juce::MidiMessage::noteOn (ch, note, juce::jlimit (0.01f, 1.0f, vel)), offset);

    if (numPendingOffs < (int) pendingOffs.size())
        pendingOffs[(size_t) numPendingOffs++] = { offset + juce::jmax (1, lengthSamples), ch, note };

    laneMeter[(size_t) li] = juce::jmax (laneMeter[(size_t) li].load(), vel);
}

void GrooveboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
    midiOut.clear();

    // Output pointers per stereo pair (pairs the host hasn't enabled fall back to Main).
    std::array<float*, kNumOuts> outL {}, outR {};
    const int mainIdx = getChannelIndexInProcessBlockBuffer (false, 0, 0);
    for (int i = 0; i < kNumOuts; ++i)
    {
        auto* bus = getBus (false, i);
        const bool on = bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2;
        const int idx = on ? getChannelIndexInProcessBlockBuffer (false, i, 0) : mainIdx;
        outL[(size_t) i] = buffer.getWritePointer (idx);
        outR[(size_t) i] = buffer.getWritePointer (idx + 1);
    }

    double bpm = 124.0, ppq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
        }
    bpm = juce::jlimit (20.0, 999.0, bpm);
    lastBpm = bpm;

    const bool preview = previewPlaying.load();
    const bool seqOn = apvts.getRawParameterValue ("seq")->load() > 0.5f;
    const bool running = seqOn && (hostPlaying || preview);

    if (! hostPlaying)
    {
        if (preview) ppq = internalPpq;
        else         internalPpq = 0.0;
    }

    const double beatsPerSample = bpm / 60.0 / sr;
    const double ppqEnd = ppq + (double) n * beatsPerSample;

    // A jump backwards means the host looped: change the random salt so probabilities re-roll each pass.
    if (ppq < lastPpqEnd - 1.0e-6)
        ++passCounter;
    lastPpqEnd = ppqEnd;
    const juce::uint32 passSeed = seed ^ ((juce::uint32) passCounter * 2654435761u);

    const float swingScale = apvts.getRawParameterValue ("swing")->load();
    const float humScale   = apvts.getRawParameterValue ("humanize")->load();
    const Step defaultStep {};

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int nl = numLanes.load();

        auto fire = [&] (int li, float vel, int off, const Step& st, double stepBeats)
        {
            if (lanes[(size_t) li].mode == LaneMode::FLChannel)
            {
                const int lenSamples = (int) juce::jmin (sr * 0.25, stepBeats / beatsPerSample * 0.9);
                sendLaneMidi (li, vel, off, st, lenSamples);
            }
            else
            {
                triggerVoice (li, vel, off, st);
            }
        };

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                const int li = m.getNoteNumber() - kBaseNote;
                if (li >= 0 && li < nl)
                    fire (li, m.getFloatVelocity(), juce::jlimit (0, n - 1, meta.samplePosition), defaultStep,
                          lanes[(size_t) li].stepBeats());
            }
        }

        for (int li = 0; li < nl; ++li)
            if (auditionFlag[(size_t) li].exchange (false))
                fire (li, 0.9f, 0, defaultStep, 0.5);

        bool anySolo = false;
        for (int li = 0; li < nl; ++li)
            anySolo = anySolo || lanes[(size_t) li].solo;

        for (int li = 0; li < nl; ++li)
        {
            const auto& lane = lanes[(size_t) li];
            if (! running)
            {
                playStep[(size_t) li] = -1;
                continue;
            }

            const double sb = lane.stepBeats();
            const int len = juce::jlimit (1, kMaxSteps, lane.length);
            playStep[(size_t) li] = (int) (((juce::int64) std::floor (juce::jmax (0.0, ppq) / sb)) % len);

            if (lane.mute || (anySolo && ! lane.solo))
                continue;
            if (lane.mode == LaneMode::Sample && lane.sample == nullptr)
                continue;

            collectEvents (lane, li, passSeed, ppq, ppqEnd, bpm, swingScale, humScale,
                           [&] (double t, float v, const Step& st)
                           {
                               const int off = juce::jlimit (0, n - 1, (int) ((t - ppq) / beatsPerSample));
                               fire (li, v, off, st, sb);
                           });
        }
    }

    // Release MIDI notes whose time has come (all of them when the sequencer stops).
    for (int i = 0; i < numPendingOffs;)
    {
        auto& p = pendingOffs[(size_t) i];
        if (! running || p.samplesLeft < n)
        {
            midiOut.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), running ? juce::jlimit (0, n - 1, p.samplesLeft) : 0);
            p = pendingOffs[(size_t) --numPendingOffs];
        }
        else
        {
            p.samplesLeft -= n;
            ++i;
        }
    }

    // Render voices.
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& b = v.smp->buffer;
        const int len = b.getNumSamples();
        const float* sL = b.getReadPointer (0);
        const float* sR = b.getReadPointer (1);
        float* oL = outL[(size_t) v.out];
        float* oR = outR[(size_t) v.out];

        for (int i = v.startOffset; i < n; ++i)
        {
            if (v.chokeAt >= 0 && i >= v.chokeAt && ! v.fading)
            {
                v.fading = true;
                v.fadeLen = juce::jmax (1, (int) (sr * 0.004));
                v.fadeLeft = v.fadeLen;
            }
            if (! v.fading && v.age >= v.decaySamples)
            {
                v.fading = true;
                v.fadeLeft = v.fadeLen;
            }

            const int ip = (int) std::floor (v.pos);
            if (ip < 0 || ip + 1 >= len) { v.active = false; break; }

            const float fr = (float) (v.pos - (double) ip);
            float l = sL[ip] + fr * (sL[ip + 1] - sL[ip]);
            float r = sR[ip] + fr * (sR[ip + 1] - sR[ip]);

            if (v.ftype != FilterType::Off)
            {
                float* io[2] = { &l, &r };
                for (int c = 0; c < 2; ++c)
                {
                    const float x = *io[c];
                    const float v3 = x - v.ic2[c];
                    const float v1 = v.a1 * v.ic1[c] + v.a2 * v3;
                    const float v2 = v.ic2[c] + v.a2 * v.ic1[c] + v.a3 * v3;
                    v.ic1[c] = 2.0f * v1 - v.ic1[c];
                    v.ic2[c] = 2.0f * v2 - v.ic2[c];
                    *io[c] = v.ftype == FilterType::LowPass ? v2
                           : v.ftype == FilterType::BandPass ? v1
                                                             : x - v.k * v1 - v2;
                }
            }

            float e = 1.0f;
            if (v.attackSamples > 0 && v.age < v.attackSamples)
                e = (float) v.age / (float) v.attackSamples;
            if (v.fading)
            {
                e *= (float) v.fadeLeft / (float) v.fadeLen;
                if (--v.fadeLeft <= 0) { v.active = false; break; }
            }

            oL[i] += l * v.gainL * e;
            oR[i] += r * v.gainR * e;
            v.pos += v.inc;
            ++v.age;
        }

        v.startOffset = 0;
        v.chokeAt = -1;
        if (! v.active)
            v.smp.reset(); // never the last reference: lanes or `retired` still hold it
    }

    if (! hostPlaying && preview)
        internalPpq = ppqEnd;

    const float master = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("master")->load(), -60.0f);
    buffer.applyGain (master);

    midi.swapWith (midiOut);
}

//==============================================================================
cg::SamplePtr GrooveboxProcessor::loadSample (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 1)
        return {};

    const auto maxLen = (juce::int64) (reader->sampleRate * 30.0);
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);

    auto sd = std::make_shared<SampleData>();
    sd->buffer.setSize (2, len);
    sd->buffer.clear();
    reader->read (&sd->buffer, 0, len, 0, true, true);
    if (reader->numChannels == 1)
        sd->buffer.copyFrom (1, 0, sd->buffer, 0, 0, len);
    sd->sampleRate = reader->sampleRate;
    sd->file = f;
    return sd;
}

void GrooveboxProcessor::swapSample (int li, cg::SamplePtr sp)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (lanes[(size_t) li].sample, sp);
    }
    if (sp != nullptr)
        retired.push_back (std::move (sp));
}

void GrooveboxProcessor::timerCallback()
{
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const cg::SamplePtr& p) { return p.use_count() <= 1; }),
                   retired.end());
}

//==============================================================================
cg::Fit GrooveboxProcessor::fileFit (const juce::File& f, Role role) const
{
    Features feat;
    if (! analyzer.get (f, feat))
        return {};
    return fitScore (feat, role, gen.genre);
}

cg::Fit GrooveboxProcessor::laneFit (int li) const
{
    if (li < 0 || li >= numLanes) return {};
    const auto& lane = lanes[(size_t) li];
    if (lane.sample == nullptr) return {};
    return fileFit (lane.sample->file, lane.role);
}

int GrooveboxProcessor::pickGoodSample (int li)
{
    const auto& lane = lanes[(size_t) li];
    const int count = lane.files.size();
    if (count == 0) return -1;

    std::vector<std::pair<int, int>> scored; // (score, index)
    for (int i = 0; i < count; ++i)
    {
        const auto fit = fileFit (lane.files.getReference (i), lane.role);
        if (fit.score >= 0) scored.push_back ({ fit.score, i });
    }

    if (scored.size() < 3)
        return rng.nextInt (count); // not analyzed yet: plain random

    std::sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first > b.first; });

    // Good fits (70+), or at least the top quarter of what's been analyzed.
    size_t pool = 0;
    while (pool < scored.size() && scored[pool].first >= 70) ++pool;
    pool = juce::jmax (pool, juce::jmax ((size_t) 1, scored.size() / 4));

    // Avoid handing back the current sample when there's a choice.
    for (int tries = 0; tries < 8; ++tries)
    {
        const int idx = scored[(size_t) rng.nextInt ((int) pool)].second;
        if (idx != lane.fileIndex || pool == 1) return idx;
    }
    return scored[0].second;
}

juce::Array<juce::File> GrooveboxProcessor::listAudioFiles (const juce::File& folder, bool recursive)
{
    juce::Array<juce::File> files;
    if (! folder.isDirectory())
        return files;

    // Capped walk: a huge folder can never stall anything.
    for (const auto& entry : juce::RangedDirectoryIterator (folder, recursive, kAudioPattern, juce::File::findFiles))
    {
        if (entry.isHidden()) continue;
        files.add (entry.getFile());
        if (files.size() >= 5000) break;
    }
    NaturalFileOrder order;
    files.sort (order);
    return files;
}

void GrooveboxProcessor::setLaneFolder (int li, const juce::File& folder, bool guessRoleFromName, const juce::File& preferredFile)
{
    if (li < 0 || li >= numLanes || ! folder.isDirectory())
        return;

    {
        // Remember the folder right away so autosave never loses it while the scan runs.
        const juce::SpinLock::ScopedLockType sl (lock);
        lanes[(size_t) li].folder = folder;
        lanes[(size_t) li].name = folder.getFileName();
        if (preferredFile != juce::File()) lanes[(size_t) li].pendingFile = preferredFile;
    }

    const int token = ++laneToken[(size_t) li];

    if (runSynchronously)
    {
        applyLaneFolder (li, folder, listAudioFiles (folder), guessRoleFromName, preferredFile);
        return;
    }

    ++ioBusy;
    const int gen = stateGeneration;
    auto aliveFlag = alive;
    io.addJob ([this, aliveFlag, li, folder, guessRoleFromName, preferredFile, token, gen]
    {
        auto files = listAudioFiles (folder);
        juce::MessageManager::callAsync ([this, aliveFlag, li, folder, guessRoleFromName, preferredFile, token, gen, files]
        {
            if (! aliveFlag->load()) return;
            --ioBusy;
            if (token != laneToken[(size_t) li] || gen != stateGeneration) return; // superseded
            applyLaneFolder (li, folder, files, guessRoleFromName, preferredFile);
        });
    });
}

void GrooveboxProcessor::applyLaneFolder (int li, const juce::File& folder, const juce::Array<juce::File>& files,
                                          bool guessRoleFromName, const juce::File& preferredFile)
{
    if (li < 0 || li >= numLanes)
        return;

    juce::Array<juce::File> incoming (files); // copied outside the lock, swapped in below
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& lane = lanes[(size_t) li];
        lane.folder = folder;
        lane.files.swapWith (incoming);
        lane.fileIndex = -1;
        lane.pendingFile = juce::File();
        lane.name = folder.getFileName();
        if (guessRoleFromName)
        {
            auto r = guessRole (folder.getFileName());
            if (r == Role::Other) r = guessRole (folder.getParentDirectory().getFileName());
            if (r != Role::Other) lane.role = r;
        }
    }

    if (files.isEmpty())
    {
        swapSample (li, nullptr);
    }
    else
    {
        const int preferred = preferredFile != juce::File() ? files.indexOf (preferredFile) : -1;
        loadSampleIndex (li, preferred >= 0 ? preferred : 0);
    }

    analyzer.enqueue (files, files.isEmpty() ? juce::File() : files[juce::jmax (0, lanes[(size_t) li].fileIndex)]);
    ++modelVersion;
}

void GrooveboxProcessor::loadSampleIndex (int li, int index)
{
    if (li < 0 || li >= numLanes)
        return;

    auto& lane = lanes[(size_t) li];
    const int count = lane.files.size();
    if (count == 0)
        return;

    index = ((index % count) + count) % count;

    // Skip unreadable files in the scroll direction.
    for (int tries = 0; tries < juce::jmin (count, 20); ++tries)
    {
        const int idx = (index + tries) % count;
        if (auto sp = loadSample (lane.files[idx]))
        {
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                lane.fileIndex = idx;
            }
            swapSample (li, std::move (sp));
            return;
        }
    }
}

void GrooveboxProcessor::stepSample (int li, int delta)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].files.isEmpty())
        return;

    const auto& lane = lanes[(size_t) li];
    const int count = lane.files.size();
    int idx = lane.fileIndex + delta;

    // With the fit filter on, skip samples known to fit poorly (unanalyzed ones still show).
    if (fitFilter)
        for (int tries = 0; tries < count; ++tries)
        {
            const int wrapped = ((idx % count) + count) % count;
            const auto fit = fileFit (lane.files.getReference (wrapped), lane.role);
            if (fit.score < 0 || fit.score >= 50) break;
            idx += delta;
        }

    loadSampleIndex (li, idx);
    audition (li);
}

void GrooveboxProcessor::randomSample (int li)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].files.isEmpty() || lanes[(size_t) li].lockSample)
        return;
    loadSampleIndex (li, pickGoodSample (li));
    audition (li);
}

void GrooveboxProcessor::shuffleAllSamples()
{
    for (int li = 0; li < numLanes; ++li)
        if (! lanes[(size_t) li].files.isEmpty() && ! lanes[(size_t) li].lockSample)
            loadSampleIndex (li, pickGoodSample (li));
    ++modelVersion;
}

std::vector<std::pair<juce::File, juce::Array<juce::File>>> GrooveboxProcessor::findLibraryFolders (const juce::File& root)
{
    // Breadth-first, max 3 levels deep, capped: picking a whole drive can't freeze FL.
    std::vector<std::pair<juce::File, juce::Array<juce::File>>> found;
    std::vector<std::pair<juce::File, int>> todo { { root, 0 } };
    int visited = 0;

    for (size_t q = 0; q < todo.size() && visited < 3000 && found.size() < 300; ++q)
    {
        const auto [dir, depth] = todo[q];
        ++visited;

        auto files = listAudioFiles (dir, false);
        if (! files.isEmpty())
            found.push_back ({ dir, files });

        if (depth < 3)
            for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findDirectories))
                if (! entry.isHidden())
                    todo.push_back ({ entry.getFile(), depth + 1 });
    }

    NaturalFileOrder order;
    std::sort (found.begin(), found.end(), [&order] (const auto& a, const auto& b)
               { return order.compareElements (a.first, b.first) < 0; });
    return found;
}

void GrooveboxProcessor::scanLibrary (const juce::File& root)
{
    if (! root.isDirectory())
        return;
    libraryRoot = root;

    if (runSynchronously)
    {
        applyLibrary (root, findLibraryFolders (root));
        return;
    }

    ++ioBusy;
    const int token = ++libraryToken;
    const int gen = stateGeneration;
    auto aliveFlag = alive;
    io.addJob ([this, aliveFlag, root, token, gen]
    {
        auto dirs = findLibraryFolders (root);
        juce::MessageManager::callAsync ([this, aliveFlag, root, token, gen, dirs]
        {
            if (! aliveFlag->load()) return;
            --ioBusy;
            if (token != libraryToken || gen != stateGeneration) return;
            applyLibrary (root, dirs);
        });
    });
}

void GrooveboxProcessor::applyLibrary (const juce::File& root, const std::vector<std::pair<juce::File, juce::Array<juce::File>>>& dirs)
{
    juce::ignoreUnused (root);

    auto dirRole = [] (const juce::File& d)
    {
        auto r = guessRole (d.getFileName());
        return r != Role::Other ? r : guessRole (d.getParentDirectory().getFileName());
    };

    std::vector<bool> used (dirs.size(), false);

    // Folders already used by sample-locked lanes stay with them.
    for (int li = 0; li < numLanes; ++li)
        if (lanes[(size_t) li].lockSample)
            for (size_t d = 0; d < dirs.size(); ++d)
                if (dirs[d].first == lanes[(size_t) li].folder) used[d] = true;

    auto assign = [&] (int li, size_t d, bool guess)
    {
        ++laneToken[(size_t) li]; // cancels any scan still running for this lane
        applyLaneFolder (li, dirs[d].first, dirs[d].second, guess, {});
        used[d] = true;
    };

    // 1) match lanes to folders by role
    for (int li = 0; li < numLanes; ++li)
    {
        if (lanes[(size_t) li].lockSample) continue;
        for (size_t d = 0; d < dirs.size(); ++d)
            if (! used[d] && dirRole (dirs[d].first) == lanes[(size_t) li].role)
            {
                assign (li, d, false);
                break;
            }
    }

    // 2) empty lanes take the leftovers, 3) then add lanes for anything still unused
    size_t next = 0;
    auto nextUnused = [&]() -> int
    {
        while (next < dirs.size() && used[next]) ++next;
        return next < dirs.size() ? (int) next : -1;
    };

    for (int li = 0; li < numLanes; ++li)
        if (lanes[(size_t) li].files.isEmpty() && ! lanes[(size_t) li].lockSample
            && (lanes[(size_t) li].folder == juce::File() || ! lanes[(size_t) li].folder.isDirectory()))
            if (int d = nextUnused(); d >= 0)
                assign (li, (size_t) d, true);

    while (numLanes < kMaxLanes)
    {
        const int d = nextUnused();
        if (d < 0) break;
        addLane();
        assign (numLanes - 1, (size_t) d, true);
    }

    ++modelVersion;
}

//==============================================================================
void GrooveboxProcessor::newSeed()
{
    seed = (juce::uint32) rng.nextInt();
}

void GrooveboxProcessor::generateAll()
{
    newSeed();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int li = 0; li < numLanes; ++li)
            if (! lanes[(size_t) li].lockPattern)
                cg::generateLane (lanes[(size_t) li], gen, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::generateLane (int li)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].lockPattern) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        cg::generateLane (lanes[(size_t) li], gen, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::mutateAll()
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int li = 0; li < numLanes; ++li)
            if (! lanes[(size_t) li].lockPattern)
                cg::mutateLane (lanes[(size_t) li], mutateAmount, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::mutateOneLane (int li)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].lockPattern) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        cg::mutateLane (lanes[(size_t) li], mutateAmount, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::copyLaneParams (int from, int to)
{
    for (auto* what : { "level", "pan", "pitch", "cutoff" })
        if (auto* src = apvts.getParameter (paramId (from, what)))
            if (auto* dst = apvts.getParameter (paramId (to, what)))
                dst->setValueNotifyingHost (src->getValue());
}

void GrooveboxProcessor::resetLaneParams (int li)
{
    for (auto* what : { "level", "pan", "pitch", "cutoff" })
        if (auto* p = apvts.getParameter (paramId (li, what)))
            p->setValueNotifyingHost (p->getDefaultValue());
}

void GrooveboxProcessor::addLane()
{
    const int nl = numLanes;
    if (nl >= kMaxLanes) return;
    resetLaneParams (nl);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        lanes[(size_t) nl] = Lane {};
        lanes[(size_t) nl].role = Role::Perc;
        lanes[(size_t) nl].name = "Lane " + juce::String (nl + 1);
        numLanes = nl + 1;
    }
    ++modelVersion;
}

void GrooveboxProcessor::removeLane (int li)
{
    const int nl = numLanes;
    if (li < 0 || li >= nl || nl <= 1) return;

    cg::SamplePtr old;
    Lane removed;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = lanes[(size_t) li].sample;
        removed = std::move (lanes[(size_t) li]);
        for (int j = li; j < nl - 1; ++j)
            lanes[(size_t) j] = std::move (lanes[(size_t) j + 1]);
        lanes[(size_t) nl - 1] = Lane {};
        numLanes = nl - 1;
    }

    for (auto& t : laneToken) ++t; // pending folder scans refer to old lane positions

    // Level, pan, pitch and cutoff move up with their lanes.
    for (int j = li; j < nl - 1; ++j)
        copyLaneParams (j + 1, j);
    resetLaneParams (nl - 1);

    if (old != nullptr)
        retired.push_back (std::move (old));
    removed.sample.reset();
    ++modelVersion;
}

//==============================================================================
juce::File GrooveboxProcessor::exportFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Groovebox").getChildFile ("MIDI");
    dir.createDirectory();
    return dir;
}

juce::File GrooveboxProcessor::exportMidi()
{
    struct Ev { double t; float v; int note; };
    std::vector<Ev> events;
    events.reserve (4096);

    const double total = juce::jlimit (1, 64, exportBars) * 4.0;
    const float swingScale = apvts.getRawParameterValue ("swing")->load();
    const float humScale   = apvts.getRawParameterValue ("humanize")->load();

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int nl = numLanes;
        bool anySolo = false;
        for (int li = 0; li < nl; ++li) anySolo = anySolo || lanes[(size_t) li].solo;

        for (int li = 0; li < nl; ++li)
        {
            const auto& lane = lanes[(size_t) li];
            if (lane.mute || (anySolo && ! lane.solo)) continue;
            // Every step exactly once, wrapped into the clip, so the MIDI loops seamlessly
            // (a step nudged early at the clip start lands at the clip end).
            const auto clipSteps = (juce::int64) std::ceil (total / lane.stepBeats() - 1.0e-9);
            collectEvents (lane, li, seed, 0.0, total, lastBpm.load(), swingScale, humScale,
                           [&] (double t, float v, const Step&)
                           {
                               t = std::fmod (t, total);
                               if (t < 0.0) t += total;
                               events.push_back ({ juce::jmin (t, total - 1.0 / 960.0), v, kBaseNote + li });
                           },
                           clipSteps);
        }
    }

    juce::MidiMessageSequence seq;
    constexpr double tpq = 960.0;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / lastBpm.load())), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (const auto& e : events)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.01f, 1.0f, e.v)), e.t * tpq);
        seq.addEvent (juce::MidiMessage::noteOff (1, e.note), (e.t + juce::jlimit (0.01, 0.125, total - e.t)) * tpq);
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote ((int) tpq);
    mf.addTrack (seq);

    auto name = juce::String ("Groovebox ") + juce::String (genreName (gen.genre)).upToFirstOccurrenceOf (" (", false, false)
              + " " + juce::String (exportBars) + " bars " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".mid";
    auto file = exportFolder().getChildFile (juce::File::createLegalFileName (name));

    file.deleteFile();
    juce::FileOutputStream out (file);
    if (out.openedOk())
    {
        mf.writeTo (out);
        out.flush();
    }
    return file;
}

//==============================================================================
juce::String GrooveboxProcessor::stepsToString (const Lane& lane)
{
    juce::String s;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto& st = lane.steps[(size_t) i];
        if (! st.on) continue;
        s << i << ':' << juce::String (st.vel, 6) << ':' << juce::String (st.prob, 6) << ':'
          << juce::String (st.nudge, 6) << ':' << st.ratchet << ':' << juce::String (st.pitch, 4) << ':'
          << juce::String (st.pan, 6) << ':' << juce::String (st.cutoff, 6) << ':' << juce::String (st.start, 6) << ';';
    }
    return s;
}

void GrooveboxProcessor::stepsFromString (Lane& lane, const juce::String& s)
{
    for (auto& st : lane.steps) st = Step {};

    juce::StringArray items;
    items.addTokens (s, ";", "");
    for (const auto& item : items)
    {
        juce::StringArray f;
        f.addTokens (item, ":", "");
        if (f.size() < 5) continue;
        const int i = f[0].getIntValue();
        if (i < 0 || i >= kMaxSteps) continue;
        auto& st = lane.steps[(size_t) i];
        st.on = true;
        st.vel = juce::jlimit (0.0f, 1.0f, f[1].getFloatValue());
        st.prob = juce::jlimit (0.0f, 1.0f, f[2].getFloatValue());
        st.nudge = juce::jlimit (-0.5f, 0.5f, f[3].getFloatValue());
        st.ratchet = juce::jlimit (1, 4, f[4].getIntValue());
        if (f.size() >= 9)
        {
            st.pitch = juce::jlimit (-12.0f, 12.0f, f[5].getFloatValue());
            st.pan = juce::jlimit (-1.0f, 1.0f, f[6].getFloatValue());
            st.cutoff = juce::jlimit (0.0f, 1.0f, f[7].getFloatValue());
            st.start = juce::jlimit (0.0f, 1.0f, f[8].getFloatValue());
        }
    }
}

std::unique_ptr<juce::XmlElement> GrooveboxProcessor::createStateXml()
{
    auto xmlPtr = std::make_unique<juce::XmlElement> ("Groovebox");
    auto& xml = *xmlPtr;
    xml.setAttribute ("version", 2);
    if (auto params = apvts.copyState().createXml())
        xml.addChildElement (params.release());

    xml.setAttribute ("genre", gen.genre);
    xml.setAttribute ("density", gen.density);
    xml.setAttribute ("variation", gen.variation);
    xml.setAttribute ("irregular", gen.irregular);
    xml.setAttribute ("mutate", mutateAmount);
    xml.setAttribute ("seed", juce::String ((juce::int64) seed));
    xml.setAttribute ("library", libraryRoot.getFullPathName());
    xml.setAttribute ("exportBars", exportBars);
    xml.setAttribute ("fitFilter", fitFilter);

    const juce::SpinLock::ScopedLockType sl (lock);
    xml.setAttribute ("numLanes", numLanes.load());
    for (int li = 0; li < numLanes; ++li)
    {
        const auto& l = lanes[(size_t) li];
        auto* e = xml.createNewChildElement ("Lane");
        e->setAttribute ("role", (int) l.role);
        e->setAttribute ("name", l.name);
        e->setAttribute ("length", l.length);
        e->setAttribute ("rate", l.rate);
        e->setAttribute ("decay", l.decay);
        e->setAttribute ("attack", l.attack);
        e->setAttribute ("reso", l.reso);
        e->setAttribute ("filter", (int) l.filter);
        e->setAttribute ("reverse", l.reverse);
        e->setAttribute ("mode", (int) l.mode);
        e->setAttribute ("midiCh", l.midiChannel);
        e->setAttribute ("midiNote", l.midiNote);
        e->setAttribute ("lockSample", l.lockSample);
        e->setAttribute ("lockPattern", l.lockPattern);
        e->setAttribute ("swing", l.swing);
        e->setAttribute ("humanize", l.humanize);
        e->setAttribute ("mute", l.mute);
        e->setAttribute ("solo", l.solo);
        e->setAttribute ("choke", l.choke);
        e->setAttribute ("out", l.out);
        e->setAttribute ("folder", l.folder.getFullPathName());
        e->setAttribute ("file", l.sample != nullptr ? l.sample->file.getFullPathName() : l.pendingFile.getFullPathName());
        e->setAttribute ("steps", stepsToString (l));
    }
    return xmlPtr;
}

void GrooveboxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateXml())
        copyXmlToBinary (*xml, destData);
}

void GrooveboxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml);
}

void GrooveboxProcessor::applyStateXml (const juce::XmlElement& xmlRef)
{
    const auto* xml = &xmlRef;
    if (! xml->hasTagName ("Groovebox"))
        return;

    ++stateGeneration; // results of folder scans started before this load are dropped
    for (auto& t : laneToken) ++t;
    ++libraryToken;

    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    gen.genre     = juce::jlimit (0, (int) NumGenres - 1, xml->getIntAttribute ("genre", gen.genre));
    gen.density   = (float) xml->getDoubleAttribute ("density", gen.density);
    gen.variation = (float) xml->getDoubleAttribute ("variation", gen.variation);
    gen.irregular = (float) xml->getDoubleAttribute ("irregular", gen.irregular);
    mutateAmount  = (float) xml->getDoubleAttribute ("mutate", mutateAmount);
    seed          = (juce::uint32) xml->getStringAttribute ("seed", "1").getLargeIntValue();
    libraryRoot   = juce::File (xml->getStringAttribute ("library"));
    exportBars    = juce::jlimit (1, 64, xml->getIntAttribute ("exportBars", 4));
    fitFilter     = xml->getBoolAttribute ("fitFilter", false);

    struct Pending { juce::File folder, file; };
    std::vector<Pending> pending;
    std::vector<cg::SamplePtr> oldSamples;
    std::vector<juce::Array<juce::File>> oldFiles ((size_t) kMaxLanes);

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (size_t i = 0; i < lanes.size(); ++i)
        {
            auto& l = lanes[i];
            if (l.sample != nullptr) oldSamples.push_back (l.sample);
            l.files.swapWith (oldFiles[i]); // big lists are freed after the lock is released
            l = Lane {};
        }

        int li = 0;
        for (auto* e : xml->getChildWithTagNameIterator ("Lane"))
        {
            if (li >= kMaxLanes) break;
            auto& l = lanes[(size_t) li];
            l.role        = (Role) juce::jlimit (0, kNumRoles - 1, e->getIntAttribute ("role"));
            l.name        = e->getStringAttribute ("name", roleName (l.role));
            l.length      = juce::jlimit (1, kMaxSteps, e->getIntAttribute ("length", 16));
            l.rate        = juce::jlimit (0, (int) rates().size() - 1, e->getIntAttribute ("rate", kRate16));
            l.decay       = (float) e->getDoubleAttribute ("decay", 1.0);
            l.attack      = (float) e->getDoubleAttribute ("attack", 0.0);
            l.reso        = (float) e->getDoubleAttribute ("reso", 0.2);
            l.filter      = (FilterType) juce::jlimit (0, 3, e->getIntAttribute ("filter", 0));
            l.reverse     = e->getBoolAttribute ("reverse");
            l.mode        = (LaneMode) juce::jlimit (0, 1, e->getIntAttribute ("mode", 0));
            l.midiChannel = juce::jlimit (1, 16, e->getIntAttribute ("midiCh", 1));
            l.midiNote    = juce::jlimit (0, 127, e->getIntAttribute ("midiNote", 60));
            l.lockSample  = e->getBoolAttribute ("lockSample");
            l.lockPattern = e->getBoolAttribute ("lockPattern");
            l.swing       = (float) e->getDoubleAttribute ("swing");
            l.humanize    = (float) e->getDoubleAttribute ("humanize", 0.15);
            l.mute        = e->getBoolAttribute ("mute");
            l.solo        = e->getBoolAttribute ("solo");
            l.choke       = juce::jlimit (0, 4, e->getIntAttribute ("choke"));
            l.out         = juce::jlimit (-1, kNumOuts - 1, e->getIntAttribute ("out", -1));
            stepsFromString (l, e->getStringAttribute ("steps"));
            pending.push_back ({ juce::File (e->getStringAttribute ("folder")), juce::File (e->getStringAttribute ("file")) });
            ++li;
        }
        numLanes = juce::jmax (1, li);
    }

    for (auto& s : oldSamples)
        retired.push_back (std::move (s));

    // Reconnect sample folders (scanned in the background) and the exact sample that was selected.
    for (int li = 0; li < (int) pending.size(); ++li)
    {
        const auto& p = pending[(size_t) li];
        if (p.folder.isDirectory())
        {
            // Load the chosen sample straight away so it plays even before the folder scan finishes.
            if (p.file.existsAsFile())
            {
                lanes[(size_t) li].files = { p.file };
                loadSampleIndex (li, 0);
            }
            setLaneFolder (li, p.folder, false, p.file);
        }
        else if (p.file.existsAsFile())
        {
            lanes[(size_t) li].files = { p.file };
            loadSampleIndex (li, 0);
            analyzer.enqueue ({ p.file });
        }
    }

    ++modelVersion;
}

//==============================================================================
juce::File GrooveboxProcessor::setupsFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Groovebox").getChildFile ("Setups");
    dir.createDirectory();
    return dir;
}

juce::File GrooveboxProcessor::lastSessionFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Groovebox").getChildFile ("last-session.gbx");
}

bool GrooveboxProcessor::saveSetup (const juce::File& file)
{
    auto xml = createStateXml();
    if (xml == nullptr) return false;
    file.getParentDirectory().createDirectory();
    return xml->writeTo (file);
}

bool GrooveboxProcessor::loadSetup (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("Groovebox"))
        return false;
    applyStateXml (*xml);
    return true;
}

void GrooveboxProcessor::autosave()
{
    if (ioBusy.load() > 0)
        return; // wait until folder scans have finished so nothing half-loaded is saved

    auto xml = createStateXml();
    if (xml == nullptr) return;
    const auto text = xml->toString();
    if (text == lastAutosaved) return;

    auto f = lastSessionFile();
    f.getParentDirectory().createDirectory();
    if (f.replaceWithText (text))
        lastAutosaved = text;
}

//==============================================================================
juce::AudioProcessorEditor* GrooveboxProcessor::createEditor()
{
    return new GrooveboxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveboxProcessor();
}
