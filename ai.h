#pragma once
#include "sim.h"

namespace sg {

// CPU opponent. It simulates every pair of (its move, your move), builds a
// payoff matrix and plays a smoothed mixed strategy, so it makes "reads"
// rather than reacting to your choice (which it never sees).
Action chooseAiAction(const World &w, int p, float temperature = 9.0f);

// Score of a finished turn from player p's point of view.
float evaluate(const World &before, const World &after, int p);

} // namespace sg
