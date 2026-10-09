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
    auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 2; i <= kNumOuts; ++i)
        b = b.withOutput ("Out " + juce::String (i), juce::AudioChannelSet::stereo(), false);
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
    }

    seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    initDefaultLanes();
    generateAll();
    startTimer (1000);
}

GrooveboxProcessor::~GrooveboxProcessor()
{
    stopTimer();
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

void GrooveboxProcessor::triggerVoice (int li, float vel, int offset)
{
    const auto& lane = lanes[(size_t) li];
    if (lane.sample == nullptr || lane.sample->buffer.getNumSamples() < 2)
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
    v.active = true;
    v.smp = lane.sample;
    v.pos = 0.0;
    v.inc = std::pow (2.0, (double) lane.pitch / 12.0) * lane.sample->sampleRate / sr;

    const float velGain = juce::Decibels::decibelsToGain (-30.0f * (1.0f - juce::jlimit (0.0f, 1.0f, vel)));
    const float g = juce::Decibels::decibelsToGain (lane.levelDb, -60.0f) * velGain;
    const float angle = (juce::jlimit (-1.0f, 1.0f, lane.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    v.gainL = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    v.gainR = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;

    v.lane = li;
    v.chokeGroup = lane.choke;
    v.out = juce::jlimit (0, kNumOuts - 1, lane.out);
    v.startOffset = offset;
    v.chokeAt = -1;
    v.age = 0;
    v.decaySamples = lane.decay >= 0.999f ? std::numeric_limits<int>::max()
                                          : (int) (sr * 0.005 * std::pow (400.0, (double) lane.decay));
    v.fading = false;
    v.fadeLen = juce::jmax (1, (int) (sr * 0.012));
    v.fadeLeft = v.fadeLen;

    laneMeter[(size_t) li] = juce::jmax (laneMeter[(size_t) li].load(), vel);
}

void GrooveboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    // Output pointers per stereo pair (disabled pairs fall back to Main).
    std::array<float*, kNumOuts> outL {}, outR {};
    for (int i = 0; i < kNumOuts; ++i)
    {
        auto* bus = getBus (false, i);
        const bool on = bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2;
        const int idx = on ? getChannelIndexInProcessBlockBuffer (false, i, 0) : getChannelIndexInProcessBlockBuffer (false, 0, 0);
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

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int nl = numLanes.load();

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                const int li = m.getNoteNumber() - kBaseNote;
                if (li >= 0 && li < nl)
                    triggerVoice (li, m.getFloatVelocity(), juce::jlimit (0, n - 1, meta.samplePosition));
            }
        }

        for (int li = 0; li < nl; ++li)
            if (auditionFlag[(size_t) li].exchange (false))
                triggerVoice (li, 0.9f, 0);

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

            if (lane.mute || (anySolo && ! lane.solo) || lane.sample == nullptr)
                continue;

            collectEvents (lane, li, passSeed, ppq, ppqEnd, bpm, swingScale, humScale,
                           [&] (double t, float v)
                           {
                               const int off = juce::jlimit (0, n - 1, (int) ((t - ppq) / beatsPerSample));
                               triggerVoice (li, v, off);
                           });
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

            const int ip = (int) v.pos;
            if (ip + 1 >= len) { v.active = false; break; }

            const float fr = (float) (v.pos - (double) ip);
            const float l = sL[ip] + fr * (sL[ip + 1] - sL[ip]);
            const float r = sR[ip] + fr * (sR[ip + 1] - sR[ip]);

            float e = 1.0f;
            if (v.fading)
            {
                e = (float) v.fadeLeft / (float) v.fadeLen;
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
    midi.clear();
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

void GrooveboxProcessor::setLaneFolder (int li, const juce::File& folder, bool guessRoleFromName)
{
    if (li < 0 || li >= numLanes || ! folder.isDirectory())
        return;

    auto files = folder.findChildFiles (juce::File::findFiles, true, kAudioPattern);
    NaturalFileOrder order;
    files.sort (order);
    if (files.size() > 5000)
        files.removeRange (5000, files.size() - 5000);

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& lane = lanes[(size_t) li];
        lane.folder = folder;
        lane.files = files;
        lane.fileIndex = -1;
        lane.name = folder.getFileName();
        if (guessRoleFromName)
        {
            auto r = guessRole (folder.getFileName());
            if (r == Role::Other) r = guessRole (folder.getParentDirectory().getFileName());
            if (r != Role::Other) lane.role = r;
        }
    }

    if (files.isEmpty()) swapSample (li, nullptr);
    else                 loadSampleIndex (li, 0);

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
    loadSampleIndex (li, lanes[(size_t) li].fileIndex + delta);
    audition (li);
}

void GrooveboxProcessor::randomSample (int li)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].files.isEmpty())
        return;
    loadSampleIndex (li, rng.nextInt (lanes[(size_t) li].files.size()));
    audition (li);
}

void GrooveboxProcessor::shuffleAllSamples()
{
    for (int li = 0; li < numLanes; ++li)
        if (! lanes[(size_t) li].files.isEmpty())
            loadSampleIndex (li, rng.nextInt (lanes[(size_t) li].files.size()));
    ++modelVersion;
}

void GrooveboxProcessor::scanLibrary (const juce::File& root)
{
    if (! root.isDirectory())
        return;
    libraryRoot = root;

    auto hasAudio = [] (const juce::File& d)
    { return d.getNumberOfChildFiles (juce::File::findFiles, kAudioPattern) > 0; };

    juce::Array<juce::File> dirs;
    for (const auto& d : root.findChildFiles (juce::File::findDirectories, true))
    {
        const int depth = d.getRelativePathFrom (root).retainCharacters ("/\\").length() + 1;
        if (depth <= 3 && hasAudio (d))
            dirs.add (d);
        if (dirs.size() >= 300) break;
    }
    if (dirs.isEmpty() && hasAudio (root))
        dirs.add (root);

    NaturalFileOrder order;
    dirs.sort (order);

    auto dirRole = [] (const juce::File& d)
    {
        auto r = guessRole (d.getFileName());
        return r != Role::Other ? r : guessRole (d.getParentDirectory().getFileName());
    };

    std::vector<bool> used ((size_t) dirs.size(), false);

    // 1) match lanes to folders by role
    for (int li = 0; li < numLanes; ++li)
        for (int d = 0; d < dirs.size(); ++d)
            if (! used[(size_t) d] && dirRole (dirs[d]) == lanes[(size_t) li].role)
            {
                setLaneFolder (li, dirs[d], false);
                used[(size_t) d] = true;
                break;
            }

    // 2) empty lanes take the leftovers, 3) then add lanes for anything still unused
    int next = 0;
    auto nextUnused = [&]() -> int
    {
        while (next < dirs.size() && used[(size_t) next]) ++next;
        return next < dirs.size() ? next : -1;
    };

    for (int li = 0; li < numLanes; ++li)
        if (lanes[(size_t) li].folder == juce::File())
            if (int d = nextUnused(); d >= 0)
            {
                setLaneFolder (li, dirs[d], true);
                used[(size_t) d] = true;
            }

    while (numLanes < kMaxLanes)
    {
        const int d = nextUnused();
        if (d < 0) break;
        addLane();
        setLaneFolder (numLanes - 1, dirs[d], true);
        used[(size_t) d] = true;
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
            cg::generateLane (lanes[(size_t) li], gen, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::generateLane (int li)
{
    if (li < 0 || li >= numLanes) return;
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
            cg::mutateLane (lanes[(size_t) li], mutateAmount, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::mutateOneLane (int li)
{
    if (li < 0 || li >= numLanes) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        cg::mutateLane (lanes[(size_t) li], mutateAmount, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::addLane()
{
    const int nl = numLanes;
    if (nl >= kMaxLanes) return;
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
                           [&] (double t, float v)
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
          << juce::String (st.nudge, 6) << ':' << st.ratchet << ';';
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
    }
}

void GrooveboxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("Groovebox");
    xml.setAttribute ("version", 1);
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
        e->setAttribute ("level", l.levelDb);
        e->setAttribute ("pan", l.pan);
        e->setAttribute ("pitch", l.pitch);
        e->setAttribute ("decay", l.decay);
        e->setAttribute ("swing", l.swing);
        e->setAttribute ("humanize", l.humanize);
        e->setAttribute ("mute", l.mute);
        e->setAttribute ("solo", l.solo);
        e->setAttribute ("choke", l.choke);
        e->setAttribute ("out", l.out);
        e->setAttribute ("folder", l.folder.getFullPathName());
        e->setAttribute ("file", l.sample != nullptr ? l.sample->file.getFullPathName() : juce::String());
        e->setAttribute ("steps", stepsToString (l));
    }

    copyXmlToBinary (xml, destData);
}

void GrooveboxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("Groovebox"))
        return;

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

    struct Pending { juce::File folder, file; };
    std::vector<Pending> pending;
    std::vector<cg::SamplePtr> oldSamples;

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& l : lanes)
        {
            if (l.sample != nullptr) oldSamples.push_back (l.sample);
            l = Lane {};
        }

        int li = 0;
        for (auto* e : xml->getChildWithTagNameIterator ("Lane"))
        {
            if (li >= kMaxLanes) break;
            auto& l = lanes[(size_t) li];
            l.role     = (Role) juce::jlimit (0, kNumRoles - 1, e->getIntAttribute ("role"));
            l.name     = e->getStringAttribute ("name", roleName (l.role));
            l.length   = juce::jlimit (1, kMaxSteps, e->getIntAttribute ("length", 16));
            l.rate     = juce::jlimit (0, (int) rates().size() - 1, e->getIntAttribute ("rate", kRate16));
            l.levelDb  = (float) e->getDoubleAttribute ("level");
            l.pan      = (float) e->getDoubleAttribute ("pan");
            l.pitch    = (float) e->getDoubleAttribute ("pitch");
            l.decay    = (float) e->getDoubleAttribute ("decay", 1.0);
            l.swing    = (float) e->getDoubleAttribute ("swing");
            l.humanize = (float) e->getDoubleAttribute ("humanize", 0.15);
            l.mute     = e->getBoolAttribute ("mute");
            l.solo     = e->getBoolAttribute ("solo");
            l.choke    = juce::jlimit (0, 4, e->getIntAttribute ("choke"));
            l.out      = juce::jlimit (0, kNumOuts - 1, e->getIntAttribute ("out"));
            stepsFromString (l, e->getStringAttribute ("steps"));
            pending.push_back ({ juce::File (e->getStringAttribute ("folder")), juce::File (e->getStringAttribute ("file")) });
            ++li;
        }
        numLanes = juce::jmax (1, li);
    }

    for (auto& s : oldSamples)
        retired.push_back (std::move (s));

    // Reconnect sample folders and the exact sample that was selected.
    for (int li = 0; li < (int) pending.size(); ++li)
    {
        const auto& p = pending[(size_t) li];
        if (p.folder.isDirectory())
        {
            const auto keepName = lanes[(size_t) li].name;
            const auto keepRole = lanes[(size_t) li].role;
            setLaneFolder (li, p.folder, false);
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                lanes[(size_t) li].name = keepName;
                lanes[(size_t) li].role = keepRole;
            }
            const int idx = lanes[(size_t) li].files.indexOf (p.file);
            if (idx >= 0) loadSampleIndex (li, idx);
        }
        else if (p.file.existsAsFile())
        {
            lanes[(size_t) li].files = { p.file };
            loadSampleIndex (li, 0);
        }
    }

    ++modelVersion;
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
