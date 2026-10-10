#include "Generator.h"

namespace cg
{
namespace
{
// 16-step probability templates, one char per step: '0'..'9' = 0%..90%, 'x' = always.
// Role order: Kick, Snare, Clap, ClosedHat, OpenHat, Ride, Cymbal, Shaker,
//             Perc, Tom, Rim, Click, Texture, FX, Bass, Other
const char* const kFallback[kNumRoles] = {
    "x000x000x000x000", "0000x0000000x000", "0000x0000000x000", "13x313x313x313x3",
    "0040004000400050", "0050005000500050", "3000000000000000", "5555555555555555",
    "0030020000300200", "0000000000000030", "0003000000300030", "0101010101010101",
    "4000000000000000", "2000000000000000", "00x000x000x000x0", "0020020020020020",
    "0000000000000000", "0000000000000000", "0000000000000000" // Foley, Bed, Vox: generated their own way
};

struct GenreDef
{
    const char* name;
    float swing;
    float irregularBias;
    double bpm;      // typical tempo, used for preview
    float roll;      // chance of hat/perc rolls (ratchets), e.g. trap
    const char* pat[kNumRoles]; // nullptr = fallback
    float foliage;   // 0..1 how much quiet off-grid foley the genre wants
};

const GenreDef kGenres[] = {
    { "House", 0.12f, 0.0f, 124.0, 0.0f,
      { nullptr, nullptr, nullptr, "13x313x313x313x3", "00700070007000x0", nullptr, nullptr, "4646464646464646",
        "0030040000300400", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }, 0.3f },
    { "Deep House", 0.22f, 0.0f, 120.0, 0.0f,
      { nullptr, "0000300000003000", nullptr, "02x202x202x202x2", "0030003000300040", nullptr, nullptr, "3535353535353535",
        "0020030000200300", nullptr, "0004000200040002", nullptr, nullptr, nullptr, "00x000x000x0003x", nullptr }, 0.4f },
    { "Tech House", 0.10f, 0.0f, 126.0, 0.0f,
      { nullptr, "0000000000000000", nullptr, "25x525x525x525x5", "00x000x000x000x0", nullptr, nullptr, nullptr,
        "0050060030050060", nullptr, "0300030003000304", nullptr, nullptr, nullptr, "0xx50xx50xx50xx5", nullptr }, 0.3f },
    { "Minimal", 0.18f, 0.05f, 125.0, 0.0f,
      { nullptr, "0000000000000000", "0000500000005000", "02x302x302x302x3", "0000000000000040", nullptr, nullptr, "2323232323232323",
        "0030000200300002", nullptr, "0004000000030004", "2131213121312131", nullptr, nullptr, nullptr, nullptr }, 0.5f },
    { "Techno", 0.03f, 0.0f, 132.0, 0.0f,
      { nullptr, "0000000000000000", "0000400000004000", "57x757x757x757x7", "00x000x000x000x0", "30x030x030x030x0", nullptr, nullptr,
        "0040003000400030", "0000000000300040", nullptr, "1111111111111111", nullptr, nullptr, "0x500x500x500x50", nullptr }, 0.3f },
    { "Dub Techno", 0.08f, 0.0f, 120.0, 0.0f,
      { nullptr, "0000000000000000", "0000300000003000", "03x303x303x303x3", "0000000000000030", "0050005000500060", nullptr, "2424242424242424",
        "0000020000000300", nullptr, "0000000300000002", nullptr, "x000000000000000", nullptr, "0x000x000x000x00", nullptr }, 0.5f },
    { "Breaks", 0.08f, 0.05f, 130.0, 0.0f,
      { "x0x0000000xx0000", "0000x0030400x030", "0000300000003000", "x3x3x3x3x3x3x3x3", "0000000000000040", "5050505050505050", nullptr, nullptr,
        "0020030020030020", "0000000000000304", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }, 0.3f },
    { "UK Garage", 0.38f, 0.0f, 132.0, 0.0f,
      { "x000000000x00300", nullptr, nullptr, "02x402x302x402x3", "0000000000000040", nullptr, nullptr, "4545454545454545",
        "0030003000300030", nullptr, "0004000300040003", nullptr, nullptr, nullptr, "x00000x000x00000", nullptr }, 0.3f },
    { "IDM", 0.05f, 0.85f, 120.0, 0.0f,
      { "x000003000x00000", "0000600000006003", "0000300000003000", "3535353535353535", "0000003000000030", "3030303030303030", nullptr, "3333333333333333",
        "0302030203020302", "0000200000002030", "0203020302030203", "3434343434343434", nullptr, nullptr, "x0003000x0000300", "0303030303030303" }, 0.6f },
    { "Ambient", 0.10f, 0.25f, 90.0, 0.0f,
      { "6000000040000000", "0000000000000000", "0000000000002000", "0020002000200020", "0000000000000000", "0020000000200000", nullptr, "2020202020202020",
        "0010002000100020", "0000000000000000", "0000000000000000", "1010101010101010", "x000000000000000", "3000000000000000", "5000000000000000", nullptr }, 0.6f },
    { "Drum & Bass", 0.03f, 0.0f, 174.0, 0.03f,
      { "x000000000x00000", "0000x0000000x000", "0000300000003000", "x2x2x2x2x2x2x2x2", "0000000000000040", "5050505050505050", nullptr, "4444444444444444",
        "0020002000200020", "0000000000000030", "0002000300020003", nullptr, nullptr, nullptr, "x000000000x00000", nullptr }, 0.3f },
    { "Jungle", 0.06f, 0.1f, 165.0, 0.05f,
      { "x0x0000000xx0000", "0000x0040400x030", "0000300000003000", "x3x3x3x3x3x3x3x3", "0000000000000040", "5050505050505050", nullptr, nullptr,
        "0020030020030020", "0000000000000304", "0003000200030002", nullptr, nullptr, nullptr, "x000000000x00000", nullptr }, 0.3f },
    { "Dubstep / Halftime", 0.05f, 0.0f, 140.0, 0.04f,
      { "x000000000300000", "00000000x0000000", "00000000x0000000", "x3x3x3x3x3x3x3x3", "0000000000000030", "3030303030303030", nullptr, "2323232323232323",
        "0003000000300003", "0000000000000030", "0000000300000003", nullptr, nullptr, nullptr, "x00000000x000000", nullptr }, 0.3f },
    { "Trap", 0.0f, 0.0f, 140.0, 0.16f,
      { "x000000x00x00000", "00000000x0000000", "00000000x0000000", "xxxxxxxxxxxxxxxx", "0000000000000300", nullptr, nullptr, nullptr,
        "0000030000000300", "0000000000000020", "0004000000040000", nullptr, nullptr, nullptr, "x000000x00x00000", nullptr }, 0.3f },
    { "Hip-hop / Boom bap", 0.5f, 0.0f, 90.0, 0.0f,
      { "x0000003x0x00000", "0000x0000000x000", "0000x0000000x000", "x2x2x2x2x2x2x2x2", "0000000000000030", nullptr, nullptr, "3333333333333333",
        "0020000000200000", nullptr, "0000000300000003", nullptr, nullptr, nullptr, "x00000x0x0000000", nullptr }, 0.3f },
    { "Downtempo", 0.3f, 0.1f, 100.0, 0.0f,
      { "x000000040x00000", "0000x0000000x000", "0000300000003000", "02x202x202x202x2", "0000000000000030", "0030003000300030", nullptr, "2424242424242424",
        "0010002000100020", "0000000000000020", "0000000200000002", nullptr, "x000000000000000", nullptr, "x000000030000000", nullptr }, 0.4f },
    // Microhouse / Perlon-style minimal: clicks, cut-up micro hits, rims and percs talking to each other.
    { "Microhouse", 0.14f, 0.15f, 124.0, 0.02f,
      { nullptr, "0000000000000000", "0000400000004000", "02x302x302x302x3", "0000000000000030", nullptr, nullptr, "2323232323232323",
        "0300203003002030", nullptr, "0040002000300024", "3232323232323232", nullptr, nullptr, "0x000x300x000x03", nullptr }, 0.8f },
    // Deep and dusty (Giegling direction): soft kick, hazy hats, sparse perc, a lot of trailing foley.
    { "Deep & dusty", 0.18f, 0.1f, 120.0, 0.0f,
      { nullptr, "0000000000000000", "0000500000005000", "03x203x203x203x2", "0040004000400050", "0030003000300030", nullptr, "3434343434343434",
        "0020030000200300", nullptr, "0003000200030002", nullptr, nullptr, nullptr, "00x000x000x0003x", nullptr }, 0.9f },
    // 2-step / future garage (Burial direction): shuffled kick and snare, swung hats, foley tied to the snare.
    { "2-step / future garage", 0.42f, 0.1f, 138.0, 0.03f,
      { "x000000300x00000", "0000x0000000x000", "0000600000006000", "02x402x302x402x3", "0000000000000040", nullptr, nullptr, "4545454545454545",
        "0030003000300030", nullptr, "0004000300040003", nullptr, nullptr, nullptr, "x00000x000x00000", nullptr }, 1.0f },
};

constexpr int kNumTemplateGenres = (int) (sizeof (kGenres) / sizeof (kGenres[0]));
static_assert (kNumTemplateGenres == Hybrid, "genre table out of sync");

float charProb (char c)
{
    if (c == 'x' || c == 'X') return 1.0f;
    if (c >= '0' && c <= '9') return (float) (c - '0') / 10.0f;
    return 0.0f;
}

bool isGhostRole (Role r)
{
    switch (r)
    {
        case Role::ClosedHat: case Role::OpenHat: case Role::Shaker: case Role::Perc:
        case Role::Rim: case Role::Click: case Role::Ride: case Role::Snare: case Role::Tom: case Role::Other:
            return true;
        default:
            return false;
    }
}

bool isLongRole (Role r) { return r == Role::Texture || r == Role::FX || r == Role::Cymbal; }

void clearSteps (Lane& lane)
{
    for (auto& s : lane.steps)
        s = Step {};
}

void euclid (std::array<bool, kMaxSteps>& out, int n, int k, int rot)
{
    out.fill (false);
    if (n <= 0) return;
    k = juce::jlimit (0, n, k);
    for (int i = 0; i < n; ++i)
        out[(size_t) ((i + rot) % n)] = ((i * k) % n) < k;
}

float swingFor (Role r, float genreSwing)
{
    switch (r)
    {
        case Role::Kick: case Role::Texture: case Role::FX: case Role::Cymbal: return 0.0f;
        case Role::Bass: return genreSwing * 0.6f;
        default: return genreSwing;
    }
}
} // namespace

// 16-step probability template for a role, read from where the reference plays.
std::array<float, 16> referenceTemplate (const Groove& gr, Role role)
{
    std::array<float, 16> t {};
    const int band = bandFor (role);
    auto prob = [&] (int b, int i)
    {
        const float p = gr.prob[(size_t) b][(size_t) i];
        return p >= 0.85f ? 1.0f : p; // played (almost) every bar: always
    };
    for (int i = 0; i < 16; ++i)
    {
        const float p = prob (band, i);
        const float acc = gr.accent[(size_t) band][(size_t) i];
        switch (role)
        {
            case Role::Kick: case Role::ClosedHat: case Role::Shaker: case Role::Click:
                t[(size_t) i] = p; break;
            case Role::Snare: case Role::Clap:
                t[(size_t) i] = p * juce::jlimit (0.0f, 1.0f, (acc - 0.45f) / 0.45f); // the loudest mid hits are the backbeat
                if (t[(size_t) i] > 0.8f) t[(size_t) i] = 1.0f;
                break;
            case Role::OpenHat: case Role::Ride:
                t[(size_t) i] = (i % 2 == 0) ? juce::jmin (0.9f, p * acc * acc) : p * acc * 0.3f; // accented highs
                break;
            case Role::Perc: case Role::Rim: case Role::Tom:
                t[(size_t) i] = p < 0.7f ? p * 0.8f : 0.1f; // the in-between mid hits
                break;
            case Role::Bass:
            {
                const float low = prob (0, i);
                t[(size_t) i] = low >= 0.85f ? 0.0f : juce::jmax (low, i % 4 == 2 ? 0.8f : 0.0f); // around the kick
                break;
            }
            default:
                t[(size_t) i] = i == 0 ? 0.6f : 0.0f;
                break;
        }
    }
    return t;
}

float genreFoliage (int g)
{
    if (g >= 0 && g < kNumTemplateGenres) return kGenres[g].foliage;
    return g == Free ? 0.5f : 0.3f;
}

// Foley: sparse, quiet, off the grid (1/32 steps nudged anywhere), a different bit of the sample each time.
static void generateFoley (Lane& lane, const GenSettings& s, float foliage, juce::Random& rng)
{
    for (auto& st : lane.steps) st = Step {};
    lane.rate = 7; // 1/32
    lane.length = juce::jlimit (32, kMaxSteps, juce::jlimit (1, 32, s.bars) * 32);
    lane.swing = 0.0f;
    lane.humanize = 0.5f;
    const float density = (0.03f + 0.09f * juce::jlimit (0.0f, 1.0f, s.density)) * (0.5f + foliage);
    for (int i = 0; i < lane.length; ++i)
    {
        if (rng.nextFloat() >= density) continue;
        auto& st = lane.steps[(size_t) i];
        st.on = true;
        st.vel = 0.12f + rng.nextFloat() * 0.33f;
        st.prob = 0.35f + rng.nextFloat() * 0.5f;
        st.nudge = (rng.nextFloat() - 0.5f) * 0.9f;
        st.start = rng.nextFloat() * 0.5f;
        st.pitch = (rng.nextFloat() - 0.5f) * 3.0f;
        st.pan = (rng.nextFloat() - 0.5f) * 0.8f;
        st.cutoff = 0.6f + rng.nextFloat() * 0.4f;
    }
}

// Vocal chops: a few hits off the beat, each from a different part of the sample.
static void generateVox (Lane& lane, const GenSettings& s, juce::Random& rng)
{
    for (auto& st : lane.steps) st = Step {};
    lane.rate = kRate16;
    lane.length = juce::jlimit (32, kMaxSteps, juce::jlimit (2, 64, s.bars) * 16);
    lane.swing = 0.2f;
    static const float pitches[] = { 0.0f, 0.0f, 0.0f, -5.0f, 7.0f, -12.0f, 5.0f };
    const int perPhrase = 1 + rng.nextInt (3);
    for (int phrase = 0; phrase < lane.length / 32; ++phrase)
        for (int h = 0; h < perPhrase; ++h)
        {
            const int pos = phrase * 32 + (rng.nextInt (16) * 2 + 1) % 32;
            auto& st = lane.steps[(size_t) pos];
            st.on = true;
            st.vel = 0.5f + rng.nextFloat() * 0.35f;
            st.prob = 0.5f + rng.nextFloat() * 0.4f;
            st.start = rng.nextFloat() * 0.9f;
            st.pitch = pitches[rng.nextInt (7)];
            st.nudge = (rng.nextFloat() - 0.5f) * 0.3f;
        }
}

double genreBpm (int g)
{
    if (g >= 0 && g < kNumTemplateGenres) return kGenres[g].bpm;
    return 124.0;
}

const char* genreName (int g)
{
    if (g >= 0 && g < kNumTemplateGenres) return kGenres[g].name;
    if (g == Hybrid) return "Hybrid (random mix)";
    if (g == Free)   return "Free (no rules)";
    if (g == Reference) return "From reference groove";
    return "?";
}

void generateLane (Lane& lane, const GenSettings& s, juce::Random& rng)
{
    int g = s.genre;
    const bool freeMode = (g == Free);
    const bool refMode  = (g == Reference) && s.ref != nullptr && s.ref->valid;
    if (g == Reference && ! refMode)
        g = House; // no reference loaded yet

    if (g == Hybrid)
        g = rng.nextInt (kNumTemplateGenres);

    const auto& def  = kGenres[(freeMode || refMode) ? 0 : juce::jlimit (0, kNumTemplateGenres - 1, g)];
    const Role  role = lane.role;
    const int   ri   = (int) role;

    if (lane.follow >= 0) return;                  // followers play after their leader, not on steps
    if (role == Role::Bed) { clearSteps (lane); lane.length = 16; lane.rate = kRate16; return; } // runs underneath, no steps
    if (role == Role::Foley) { generateFoley (lane, s, (freeMode || refMode) ? 0.6f : def.foliage, rng); return; }
    if (role == Role::Vox) { generateVox (lane, s, rng); return; }

    // Build the 16-step probability template for this lane.
    std::array<float, 16> tpl {};
    if (freeMode)
    {
        const float base = 0.05f + rng.nextFloat() * 0.45f;
        for (auto& p : tpl)
            p = rng.nextFloat() < base ? 0.3f + rng.nextFloat() * 0.7f : 0.0f;
    }
    else if (refMode)
        tpl = referenceTemplate (*s.ref, role);
    else
    {
        const char* pat = def.pat[ri] != nullptr ? def.pat[ri] : kFallback[ri];
        for (int i = 0; i < 16; ++i)
            tpl[(size_t) i] = charProb (pat[i]);
    }

    float avgP = 0.0f;
    for (auto p : tpl) avgP += p;
    avgP /= 16.0f;

    const float var   = juce::jlimit (0.0f, 1.0f, s.variation);
    const float densF = 0.25f + juce::jlimit (0.0f, 1.0f, s.density) * 1.5f;
    float irr = juce::jlimit (0.0f, 1.0f, s.irregular + def.irregularBias);
    if (freeMode) irr = juce::jmax (irr, 0.3f + rng.nextFloat() * 0.5f);

    float irrChance = irr;
    if (role == Role::Kick && g != IDM && ! freeMode)
        irrChance = irr > 0.6f ? (irr - 0.6f) * 2.0f : 0.0f; // keep the four-on-the-floor unless pushed hard

    if (refMode) irr = juce::jmin (irr, 0.5f); // the reference is the grid; keep most lanes straight
    const float genreSwing = freeMode ? rng.nextFloat() * 0.4f : (refMode ? 0.0f : def.swing); // the groove brings the timing
    lane.swing    = juce::jlimit (0.0f, 0.75f, swingFor (role, genreSwing) + (rng.nextFloat() - 0.5f) * var * 0.08f);
    lane.humanize = juce::jlimit (0.0f, 1.0f, 0.08f + var * 0.3f);
    lane.choke    = (role == Role::ClosedHat || role == Role::OpenHat) ? 1 : 0;

    clearSteps (lane);

    const bool ghostRole = isGhostRole (role);
    const bool longRole  = isLongRole (role);

    if (rng.nextFloat() < irrChance)
    {
        // Irregular lane: odd length, odd rate, Euclidean placement.
        static const int shortLens[] = { 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 18, 20, 21, 24, 28 };
        static const int longLens[]  = { 24, 28, 32, 40, 48, 64 };

        lane.length = longRole ? longLens[rng.nextInt (6)] : shortLens[rng.nextInt (15)];

        const float rr = rng.nextFloat();
        if (role == Role::Kick || role == Role::Bass || longRole) lane.rate = rr < 0.8f ? kRate16 : 4; // 1/16 or 1/16D
        else if (rr < 0.55f) lane.rate = kRate16;
        else if (rr < 0.70f) lane.rate = 6;  // 1/16T
        else if (rr < 0.80f) lane.rate = 7;  // 1/32
        else if (rr < 0.88f) lane.rate = 2;  // 1/8
        else if (rr < 0.94f) lane.rate = 4;  // 1/16D
        else                 lane.rate = 3;  // 1/8T

        const int hits = juce::jmax (1, juce::roundToInt ((float) lane.length * juce::jlimit (0.04f, 0.9f, avgP * densF)));
        std::array<bool, kMaxSteps> e {};
        euclid (e, lane.length, longRole ? juce::jmin (hits, 2) : hits, rng.nextInt (lane.length));

        bool first = true;
        for (int i = 0; i < lane.length; ++i)
        {
            auto& st = lane.steps[(size_t) i];
            if (e[(size_t) i])
            {
                st.on  = true;
                st.vel = first ? 0.9f : 0.45f + rng.nextFloat() * 0.4f;
                if (! first && rng.nextFloat() < var * 0.6f)
                    st.prob = 1.0f - rng.nextFloat() * var * 0.5f;
                if (rng.nextFloat() < irr * 0.5f)
                    st.nudge = (rng.nextFloat() - 0.5f) * irr * 0.36f;
                if (ghostRole && rng.nextFloat() < var * 0.1f + irr * 0.08f)
                    st.ratchet = rng.nextFloat() < 0.25f ? 3 + rng.nextInt (2) : 2;
                first = false;
            }
            else if (ghostRole && rng.nextFloat() < var * 0.08f)
            {
                st.on    = true;
                st.vel   = 0.18f + rng.nextFloat() * 0.17f;
                st.prob  = 0.5f + rng.nextFloat() * 0.4f;
                st.nudge = (rng.nextFloat() - 0.5f) * var * 0.2f;
            }
        }
        return;
    }

    // Straight lane: 16 steps (or 4 bars for long sounds) at 1/16.
    const int targetLen = juce::jlimit (16, kMaxSteps, s.bars * 16);
    lane.rate   = kRate16;
    lane.length = longRole ? juce::jmax (64, targetLen) : 16;

    for (int i = 0; i < lane.length; ++i)
    {
        float p;
        if (longRole) p = i % 64 == 0 ? tpl[0] : (i % 64 == 32 ? tpl[0] * 0.4f : 0.0f);
        else          p = tpl[(size_t) i];

        auto& st = lane.steps[(size_t) i];

        if (p >= 0.999f)
        {
            if (role != Role::Kick && s.density < 0.2f && rng.nextFloat() < (0.2f - s.density) * 3.0f)
                continue;
            st.on  = true;
            st.vel = 0.82f + rng.nextFloat() * 0.12f;
        }
        else if (p > 0.0f)
        {
            if (rng.nextFloat() < juce::jmin (0.97f, p * densF))
            {
                st.on  = true;
                st.vel = juce::jlimit (0.1f, 1.0f, 0.3f + p * 0.5f + (rng.nextFloat() - 0.5f) * var * 0.3f);
                if (var > 0.15f && p < 0.7f)
                    st.prob = 1.0f - rng.nextFloat() * var * 0.6f;
            }
        }
        else if (ghostRole && rng.nextFloat() < var * 0.07f * densF)
        {
            st.on    = true;
            st.vel   = 0.15f + rng.nextFloat() * 0.17f;
            st.prob  = 0.4f + rng.nextFloat() * 0.45f;
            st.nudge = (rng.nextFloat() - 0.5f) * var * 0.2f;
        }

        if (st.on && p < 0.999f && rng.nextFloat() < var * 0.25f)
            st.nudge = juce::jlimit (-0.5f, 0.5f, st.nudge + (rng.nextFloat() - 0.5f) * var * 0.12f);

        if (st.on && ghostRole && rng.nextFloat() < var * 0.05f + (freeMode ? 0.0f : def.roll))
            st.ratchet = def.roll > 0.1f ? 2 + rng.nextInt (3) : (rng.nextFloat() < 0.3f ? 3 : 2);
    }

    // With more variation, hats and percussion get a 2-bar phrase whose second bar differs slightly.
    if (! longRole && ghostRole && var > 0.45f && rng.nextFloat() < var)
    {
        for (int i = 0; i < 16; ++i)
            lane.steps[(size_t) (i + 16)] = lane.steps[(size_t) i];
        lane.length = 32;

        Lane tmp; // pattern-only copy, no heap allocation (runs under the audio lock)
        tmp.role   = lane.role;
        tmp.length = 32;
        tmp.steps  = lane.steps;
        mutateLane (tmp, var * 0.4f, rng);
        for (int i = 16; i < 32; ++i)
            lane.steps[(size_t) i] = tmp.steps[(size_t) i];
        lane.length = 32;
    }

    // Longer loops: repeat the phrase and let it evolve bar by bar, with fills every 4 and 8 bars.
    if (! longRole && targetLen > lane.length)
    {
        const int base = lane.length;
        for (int i = base; i < targetLen; ++i)
            lane.steps[(size_t) i] = lane.steps[(size_t) (i % base)];
        lane.length = targetLen;

        const float evolve = (role == Role::Kick || role == Role::Bass ? 0.06f : 0.22f) * var;
        if (evolve > 0.0f)
            for (int bar = base / 16; bar < targetLen / 16; ++bar)
                mutateRange (lane, bar * 16, bar * 16 + 16, evolve, rng);

        const bool fillRole = role == Role::Snare || role == Role::Clap || role == Role::ClosedHat
                           || role == Role::Tom || role == Role::Perc || role == Role::Rim;
        if (fillRole && var > 0.15f)
            for (int bar = 3; bar < targetLen / 16; bar += 4)
            {
                const float strength = (bar % 8 == 7) ? 1.0f : 0.5f;
                for (int i = bar * 16 + 12; i < bar * 16 + 16; ++i)
                {
                    if (rng.nextFloat() >= var * 0.6f * strength) continue;
                    auto& st = lane.steps[(size_t) i];
                    if (! st.on)
                    {
                        st = Step {};
                        st.on = true;
                        st.vel = 0.35f + rng.nextFloat() * 0.4f;
                    }
                    if (rng.nextFloat() < 0.3f * strength)
                        st.ratchet = 2 + rng.nextInt (2);
                }
            }
    }
}

void mutateRange (Lane& lane, int from, int to, float amount, juce::Random& rng)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    from = juce::jlimit (0, kMaxSteps, from);
    to = juce::jlimit (from, kMaxSteps, to);

    for (int i = from; i < to; ++i)
    {
        auto& st = lane.steps[(size_t) i];

        if (st.on)
        {
            const float removeChance = amount * (st.vel < 0.7f ? 0.3f : 0.1f);
            if (lane.role != Role::Kick && rng.nextFloat() < removeChance)
            {
                st.on = false;
                continue;
            }
            if (rng.nextFloat() < amount * 0.4f)
                st.vel = juce::jlimit (0.05f, 1.0f, st.vel + (rng.nextFloat() - 0.5f) * 0.5f * amount);
            if (rng.nextFloat() < amount * 0.2f)
                st.prob = juce::jlimit (0.2f, 1.0f, st.prob - rng.nextFloat() * 0.4f * amount);
            if (rng.nextFloat() < amount * 0.15f)
                st.nudge = juce::jlimit (-0.5f, 0.5f, st.nudge + (rng.nextFloat() - 0.5f) * 0.2f * amount);
            if (rng.nextFloat() < amount * 0.08f)
                st.ratchet = st.ratchet > 1 ? 1 : 2;
        }
        else if (rng.nextFloat() < amount * 0.12f)
        {
            st = Step {};
            st.on    = true;
            st.vel   = 0.2f + rng.nextFloat() * 0.4f;
            st.prob  = 0.6f + rng.nextFloat() * 0.4f;
        }
    }

    // Occasionally swap neighbours to shift accents.
    for (int i = from; i + 1 < to; ++i)
        if (rng.nextFloat() < amount * 0.05f)
            std::swap (lane.steps[(size_t) i], lane.steps[(size_t) i + 1]);
}

void mutateLane (Lane& lane, float amount, juce::Random& rng)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    const int len = juce::jlimit (1, kMaxSteps, lane.length);
    mutateRange (lane, 0, len, amount, rng);

    // Rarely nudge the lane length itself, for slow polymeter drift (short lanes only).
    if (len <= kViewSteps && amount > 0.5f && rng.nextFloat() < (amount - 0.5f) * 0.3f)
        lane.length = juce::jlimit (1, kViewSteps, len + (rng.nextBool() ? 1 : -1));
}

Role guessRole (const juce::String& nameIn)
{
    const auto name = nameIn.toLowerCase();
    juce::StringArray tokens;
    tokens.addTokens (name, " _-.()[]0123456789", "");
    tokens.removeEmptyStrings();

    auto hasToken = [&] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (tokens.contains (w)) return true;
        return false;
    };
    auto contains = [&] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (name.contains (w)) return true;
        return false;
    };

    if (contains ({ "open hat", "openhat", "open_hat", "open-hat", "ohat" }) || hasToken ({ "oh", "ohh", "open" })) return Role::OpenHat;
    if (contains ({ "kick", "bassdrum", "bass drum" }) || hasToken ({ "bd", "kik", "kck" }))                       return Role::Kick;
    if (contains ({ "snare" }) || hasToken ({ "sd", "snr" }))                                                    return Role::Snare;
    if (contains ({ "clap" }) || hasToken ({ "clp", "cp" }))                                                     return Role::Clap;
    if (contains ({ "hihat", "hi-hat", "hi hat", "hat", "closed" }) || hasToken ({ "hh", "chh", "ch" }))         return Role::ClosedHat;
    if (contains ({ "ride" }))                                                                                   return Role::Ride;
    if (contains ({ "crash", "cymbal", "china", "splash" }) || hasToken ({ "cym" }))                             return Role::Cymbal;
    if (contains ({ "shaker", "shake", "tamb", "maraca" }))                                                      return Role::Shaker;
    if (contains ({ "tom" }))                                                                                    return Role::Tom;
    if (contains ({ "rim", "stick" }))                                                                           return Role::Rim;
    if (contains ({ "click", "glitch", "tick", "blip" }))                                                        return Role::Click;
    if (contains ({ "vinyl", "crackle", "rain", "hiss", "room tone", "roomtone", "bed", "drone", "noise floor" }))  return Role::Bed;
    if (contains ({ "foley", "field", "rustle", "found", "household", "paper", "cloth", "keys" }))                return Role::Foley;
    if (contains ({ "vocal", "vox", "voice", "chop", "acapella", "acappella" }))                                  return Role::Vox;
    if (contains ({ "texture", "atmos", "ambien", "noise" }))                                                     return Role::Texture;
    if (contains ({ "riser", "impact", "sweep", "effect" }) || hasToken ({ "fx", "sfx" }))                       return Role::FX;
    if (contains ({ "bass", "808", "sub" }))                                                                     return Role::Bass;
    if (contains ({ "perc", "conga", "bongo", "cowbell", "clave", "wood", "block", "shot" }))                     return Role::Perc;
    return Role::Other;
}

} // namespace cg
