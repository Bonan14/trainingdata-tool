#if !defined(WDL_CONVERSION_H_INCLUDED)
#define WDL_CONVERSION_H_INCLUDED

#include <algorithm>
#include <cmath>
#include <vector>

// Single source of truth for turning an engine's scalar centipawn score
// into lc0's (Q, D) pair. Both evaluation paths use this so they can never
// drift apart: -pgn-eval-mode (reading evals out of PGN comments) and
// -stockfish (running the engine live) must map the same score to the same
// Q, or the two modes would produce inconsistent training data.

namespace wdl {

// Score -> (W, D, L), using lc0's own WDL model.
//
// This is the "WDL_mu" score type from search/classic/search.cc, run
// backwards. That code reports
//
//     uci_info.score = 100 * mu_uci
//
// i.e. mu IS the eval in pawns (the UCI score is just mu in centipawns).
// So going from a score back to a distribution needs no inversion of any
// display formula -- feed the eval in as mu and evaluate the same logistic
// pair WDLRescale() reconstructs with:
//
//     W = logistic((mu - 1) / s)      L = logistic((-mu - 1) / s)
//     Q = W - L                       D = 1 - W - L
//
// Do NOT use the "centipawn" score type (cp = 90*tan(1.5637541897*Q)) for
// this. That is a *display* convention for rendering Q as a
// centipawn-looking number, not a calibrated win-probability model.
// Measured against real game outcomes it is badly off in both directions:
// at +0.37 pawns it claims Q=+0.25 where the true figure is +0.02, and at
// +2.45 pawns it claims +0.78 where the truth is +1.00.
// (scripts/measure_pgn_wdl.py reproduces that comparison.)
//
// `scale` is pinned at 1.0 by lc0's decode convention and is not fitted --
// see Options::wdl_scale in PGNGame.h.
//
// The spread schedule: flat below `join`, centipawn-derived above it. It is
// UNCONDITIONAL -- there is no constant-spread mode. The old -wdl-spread and
// -wdl-max knobs provided one and have been removed; the schedule subsumes
// both, since it keeps W off the saturation rail without needing a cap.
//
// A single constant spread cannot serve both ends of the eval range. Narrow
// values model the draw plateau near equality but make lc0 abandon its own
// WDL_mu score type in won positions -- it switches to
// 45*tan(1.56728071628*wl), which diverges as wl -> 1 and reports a +4
// position as +8.56. Wide values keep lc0 on the WDL_mu path but claim a
// level position is barely drawish. Measured crossovers: spread 0.85 breaks
// at eval 3.04, 1.2 at 4.50, 1.4 at 5.43.
//
// Above `join` the spread is therefore not chosen at all -- it is pinned by
// requiring lc0's OTHER score type to agree as well:
//
//     W - L  ==  atan(100*eval/90) / 1.5637541897
//
// i.e. the centipawn rendering of the target reproduces the Stockfish eval
// that produced it. Because mu = (a-b)/(a+b) = eval holds for ANY spread
// (the spread cancels), pinning W-L this way costs nothing and buys exact
// agreement from both score types plus a monotonically falling D.
//
// Below `join` that equation has no solution: mu = 1 forces W = 0.5 exactly,
// while the centipawn convention puts wl = 0.536 at 100cp, and W >= W-L
// makes the two irreconcilable. The constraint also degenerates as eval -> 1
// (the solved spread collapses toward 0, turning the target into a step
// function), so the join must sit where the schedule is still sensible
// rather than at its mathematical limit. Below it the spread is held at its
// join value (0.659 at join 1.5) -- continuous, and the only free choice in
// the whole curve.
//
// join 1.5 gives D = 0.6401 at equality; join 2.0 gives 0.5263. Reference-net
// measurements put the true figure between 0.68 and 0.88, so 1.5 is the
// closer of the two. The join cannot be pushed to its limit: at join 1.0 the
// curve degenerates to D(0) = 1.0000, a step function.
namespace detail {

inline double WdlLogistic(double x) {
  if (x >= 0.0) return x < 700.0 ? 1.0 / (1.0 + std::exp(-x)) : 1.0;
  return x > -700.0 ? std::exp(x) / (1.0 + std::exp(x)) : 0.0;
}

// W - L for a given eval and spread.
inline double WdlQOf(double ev, double s) {
  return WdlLogistic((ev - 1.0) / s) - WdlLogistic((-ev - 1.0) / s);
}

// The W-L that renders back as exactly `ev` under lc0's centipawn score type.
inline double WdlQTarget(double ev) {
  return std::atan(ev * 100.0 / 90.0) / 1.5637541897;
}

// Solve WdlQOf(ev, s) == WdlQTarget(ev). For ev > 1 the left side falls
// monotonically from 1 to 0 as s goes 0 -> inf, so the root is unique and
// plain bisection is enough.
inline double SolveSpread(double ev) {
  const double tgt = WdlQTarget(ev);
  double lo = 1e-3, hi = 60.0;
  for (int i = 0; i < 100; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (WdlQOf(ev, mid) > tgt) lo = mid; else hi = mid;
  }
  return 0.5 * (lo + hi);
}

// Bisection is far too slow to run per position, so the schedule is
// tabulated once at 0.01-pawn resolution and interpolated. Evals in real
// Fishtest PGNs top out around 84 pawns, so 128 is ample headroom; beyond
// the table the last entry is reused.
constexpr double kSpreadStep = 0.01;
constexpr int kSpreadMaxPawns = 128;

inline const std::vector<double>& SpreadTable() {
  static const std::vector<double> table = [] {
    std::vector<double> t(
        static_cast<size_t>(kSpreadMaxPawns / kSpreadStep) + 2, 0.0);
    for (size_t i = 0; i < t.size(); ++i) {
      t[i] = SolveSpread(static_cast<double>(i) * kSpreadStep);
    }
    return t;
  }();
  return table;
}

}  // namespace detail

// The join used unless a caller overrides it -- 1.5 pawns, derived above.
constexpr float kDefaultJoin = 1.5f;

// Spread to use for a position whose eval is `score_pawns`, given the join.
// A non-positive join has no meaning now that the schedule is unconditional,
// so it falls back to kDefaultJoin rather than producing a degenerate curve.
inline float SpreadForEval(float score_pawns, float join) {
  const double j = join > 0.0f ? static_cast<double>(join)
                               : static_cast<double>(kDefaultJoin);
  const double ev = std::fabs(static_cast<double>(score_pawns));
  const double use = ev < j ? j : ev;
  const auto& t = detail::SpreadTable();
  const double pos = use / detail::kSpreadStep;
  const size_t i = static_cast<size_t>(pos);
  if (i + 1 >= t.size()) return static_cast<float>(t.back());
  const double f = pos - static_cast<double>(i);
  return static_cast<float>(t[i] * (1.0 - f) + t[i + 1] * f);
}

// There is no max_wl cap any more. It existed because a constant spread let
// the logistic saturate to EXACTLY 1.0 once (mu - 1)/spread passed about 17,
// making the target assert that a merely-winning position was already
// decided. The schedule keeps W off that rail by itself -- W is 0.9915 at
// eval 30 and still only 0.9993 at 100, never reaching 1.0 -- so the cap had
// nothing left to do and went with -wdl-spread.
inline void ScoreToWDL(float score_pawns, float scale, float& q, float& d,
                       float join = kDefaultJoin) {
  const float mu = scale * score_pawns;
  // The effective spread comes from the schedule. mu is unaffected by it:
  // the spread cancels out of (a-b)/(a+b), which is why it can vary per
  // position without breaking the round-trip.
  const float s_eff = SpreadForEval(score_pawns, join);
  const float w = 1.0f / (1.0f + std::exp(-(mu - 1.0f) / s_eff));
  const float l = 1.0f / (1.0f + std::exp(-(-mu - 1.0f) / s_eff));
  q = w - l;
  // W and L already sum to <= 1 by construction, so D needs no clamping
  // against |Q| the way it would if Q came from an unrelated formula.
  d = (std::max)(0.0f, 1.0f - w - l);
}

// Convenience wrapper for callers holding integer centipawns.
inline float CentipawnToQ(int centipawns, float scale,
                          float join = kDefaultJoin) {
  float q, d;
  ScoreToWDL(static_cast<float>(centipawns) / 100.0f, scale, q, d, join);
  return q;
}

// How certain a 50-move draw is, given a halfmove clock.
//
// Callers pass the clock the *played move leaves behind*, so that the two
// move types which reset it -- pawn moves and captures -- are scored at full
// value, and only moves that extend it are decremented.
//
// 0 while the clock is below `damp_start`, rising linearly to 1 at 100 plies
// -- the point at which the game IS drawn, whatever is on the board. The
// dead zone exists because a moderate clock carries no information: 20-odd
// plies of maneuvering is ordinary endgame play, not shuffling, and damping
// from ply 1 would bias every long endgame toward a draw.
inline float Rule50DrawCertainty(int halfmove_clock, int damp_start) {
  constexpr int kFiftyMovePlies = 100;  // game.cpp: ply_nb >= 100 -> DRAW_FIFTY
  if (damp_start >= kFiftyMovePlies) return 0.0f;
  if (halfmove_clock <= damp_start) return 0.0f;
  if (halfmove_clock >= kFiftyMovePlies) return 1.0f;
  return static_cast<float>(halfmove_clock - damp_start) /
         static_cast<float>(kFiftyMovePlies - damp_start);
}

// Blend a (Q, D) toward a certain draw as the halfmove clock runs out.
//
// This is a straight interpolation in W/D/L space,
//
//     (W, D, L)_out = (1-c)*(W, D, L)_in + c*(0, 1, 0)
//
// which reduces to the two lines below and keeps the triple on the simplex.
// At c=1 the result is exactly Q=0, D=1, matching the actual rule.
//
// Damping Q alone would be worse than doing nothing: it would produce
// Q->0 with D unchanged, i.e. "certain, and equally likely to be won or
// lost" -- the opposite of the drawn position it is meant to describe.
//
// Only for evaluations that do not already model the rule. A real engine's
// score does (Stockfish damps its own eval by the halfmove clock, and its
// search sees the terminal draw outright), so applying this on top of
// -stockfish or -pgn-eval-mode would double-count it.
inline void ApplyRule50Draw(int halfmove_clock, int damp_start, float& q,
                            float& d) {
  const float c = Rule50DrawCertainty(halfmove_clock, damp_start);
  if (c <= 0.0f) return;
  q *= (1.0f - c);
  d += (1.0f - d) * c;
}

}  // namespace wdl

#endif
