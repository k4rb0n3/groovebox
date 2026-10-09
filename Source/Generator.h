#pragma once

#include "LaneData.h"

namespace cg
{
enum Genre
{
    House, DeepHouse, TechHouse, Minimal, Techno, DubTechno, Breaks, Garage, IDM, Ambient,
    DrumAndBass, Jungle, Halftime, Trap, HipHop, Downtempo,
    Hybrid, // every lane borrows from a random genre
    Free,   // no genre rules, random densities and placements
    NumGenres
};

const char* genreName (int g);
double genreBpm (int g); // typical tempo

struct GenSettings
{
    int   genre     = House;
    float density   = 0.5f; // 0..1
    float variation = 0.3f; // 0..1, ghosts, velocity spread, probability < 100%
    float irregular = 0.0f; // 0..1, odd lengths, Euclidean placement, odd rates
    int   bars      = 1;    // loop length the generator writes (1..64 bars); patterns evolve bar by bar
};

// Replace a lane's pattern with a new one.
void generateLane (Lane& lane, const GenSettings& s, juce::Random& rng);

// Evolve a lane's existing pattern; amount 0..1.
void mutateLane (Lane& lane, float amount, juce::Random& rng);

// Evolve steps [from, to) only, without touching the lane length.
void mutateRange (Lane& lane, int from, int to, float amount, juce::Random& rng);

// Guess a role from a folder or file name.
Role guessRole (const juce::String& name);

} // namespace cg
