#include "ai.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace sg {

namespace {

std::vector<float> softmax(const std::vector<float> &v, float t) {
    float m = *std::max_element(v.begin(), v.end());
    std::vector<float> out(v.size());
    float sum = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        out[i] = std::exp((v[i] - m) / t);
        sum += out[i];
    }
    for (float &x : out) x /= sum;
    return out;
}

std::mt19937 &rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

} // namespace

float evaluate(const World &before, const World &after, int p) {
    const Fighter &me0 = before.f[p], &op0 = before.f[1 - p];
    const Fighter &me = after.f[p], &op = after.f[1 - p];
    float dealt = float(op0.hp - op.hp);
    float taken = float(me0.hp - me.hp);
    float score = dealt - 1.1f * taken;

    if (op.ko) score += 2000;
    if (me.ko) score -= 2000;

    // Frame advantage: whoever acts first next turn has the initiative.
    float adv = float(framesUntilFree(op) - framesUntilFree(me));
    score += std::clamp(adv, -40.0f, 40.0f) * 1.2f;

    // Meter is worth a little.
    score += 0.25f * float((me.meter - me0.meter) - (op.meter - op0.meter));

    // Mild preference for mid range and for not being cornered.
    float dist = std::fabs(me.pos.x - op.pos.x);
    score -= std::fabs(dist - 130.0f) * 0.07f;
    float wall = std::min(me.pos.x - STAGE_LEFT, STAGE_RIGHT - me.pos.x);
    if (wall < 120) score -= (120 - wall) * 0.08f;
    return score;
}

Action chooseAiAction(const World &w, int p, float temperature) {
    std::vector<Action> mine = availableActions(w, p);
    if (mine.empty()) return Action::Continue;
    std::vector<Action> theirs = availableActions(w, 1 - p);
    if (theirs.empty()) theirs = {Action::Continue};

    const size_t n = mine.size(), m = theirs.size();
    std::vector<float> M(n * m); // payoff for p (zero-sum approximation)
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < m; ++j) {
            Action a0 = p == 0 ? mine[i] : theirs[j];
            Action a1 = p == 0 ? theirs[j] : mine[i];
            World after = simulateTurn(w, a0, a1);
            M[i * m + j] = evaluate(w, after, p);
        }

    // Smoothed fictitious play: both sides repeatedly respond to the other's mix.
    std::vector<float> pm(n, 1.0f / n), qm(m, 1.0f / m);
    for (int it = 0; it < 40; ++it) {
        std::vector<float> ev(n, 0), eq(m, 0);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < m; ++j) {
                ev[i] += M[i * m + j] * qm[j];
                eq[j] -= M[i * m + j] * pm[i];
            }
        std::vector<float> np = softmax(ev, temperature), nq = softmax(eq, temperature);
        float k = 1.0f / (it + 2);
        for (size_t i = 0; i < n; ++i) pm[i] += (np[i] - pm[i]) * k;
        for (size_t j = 0; j < m; ++j) qm[j] += (nq[j] - qm[j]) * k;
    }

    std::discrete_distribution<size_t> dist(pm.begin(), pm.end());
    return mine[dist(rng())];
}

} // namespace sg
