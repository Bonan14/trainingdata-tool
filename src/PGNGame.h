#if !defined(PGN_GAME_H_INCLUDED)
#define PGN_GAME_H_INCLUDED

#include "neural/encoder.h"
#include "neural/network.h"
#include "trainingdata/trainingdata_v6.h"
#include "pgn.h"
#include "polyglot_lib.h"
#include "PGNMoveInfo.h"

class PGNMoveInfo;
class StockfishEvaluator;

struct Options {
  bool verbose = false;
  // Reads the eval already embedded in the PGN's move comments (Fishtest/
  // cutechess-cli's "SCORE/DEPTH TIMEs" format, e.g. "-0.76/18 1.813s") --
  // no engine spawned, no re-search. This used to be lichess_mode, which
  // parsed Lichess's [%eval] annotation instead; repurposed since Fishtest
  // PGNs carry real LTC-depth evals of their own, not Lichess-style ones,
  // and re-evaluating them with -stockfish would throw that away just to
  // recompute something weaker.
  bool pgn_eval_mode = false;
  bool stockfish_mode = false;
  // WDL model for pgn_eval_mode: mu = wdl_scale * eval, then
  // W = logistic((mu-1)/wdl_spread), L = logistic((-mu-1)/wdl_spread),
  // giving Q = W-L and D = 1-W-L together. This is lc0's "WDL_mu" model
  // run backwards -- search.cc reports score = 100*mu, so mu is simply the
  // eval in pawns. See wdl::ScoreToWDL in WdlConversion.h.
  //
  // Defaults are FITTED against the actual outcomes of the games being
  // converted: bucket positions by the eval in their PGN comment, count
  // the real win/draw/loss frequencies, and fit W and L to them.
  // RMSE 0.015 on (W, L) -- see scripts/measure_pgn_wdl.py, which runs
  // exactly this fit and prints the values to use. Stable across Fishtest
  // files: five separate ones fit to scale 1.11-1.27, spread 0.20-0.21.
  //
  // wdl_scale landing near 1.0 is a consistency check, not a coincidence:
  // the model says mu IS the eval, so a large correction would have meant
  // the model was wrong. Do not substitute lc0's "centipawn" score type
  // (cp = 90*tan(1.5637541897*Q)) here -- that is a display convention,
  // not a calibrated win-probability model, and measured against real
  // outcomes it is badly miscalibrated at both ends.
  //
  // Deliberately NOT lc0's WDLDrawRateReference. That parameter describes
  // the net you are *running* (looked up by running it from startpos and
  // reading its WDL), but here we are generating training data, and these
  // are Stockfish games with their own book, time control and adjudication
  // -- a different distribution entirely. Targeting an lc0 net's draw rate
  // would aim at the wrong thing, and would be circular if that net is the
  // one being trained.
  //
  // Caveat: ~86% of Fishtest games end by adjudication, and cutechess
  // adjudicates a draw exactly when the eval sits near zero, so this curve
  // is partly shaped by the adjudication rule rather than pure chess. It
  // is still the real label distribution in the data, and is consistent
  // with result_q/result_d, which come from the same recorded results.
  //
  // Raising wdl_scale sharpens: a given eval maps to a larger mu, so the
  // transition to a decided result happens at a smaller eval. wdl_spread
  // is lc0's scale_reference and follows from the draw rate at an equal
  // position: spread = 1/log((1+r)/(1-r)), equivalently
  // D(equal) = 1 - 2*logistic(-1/spread). Re-fit rather than eyeballing if
  // these change; scripts/measure_pgn_wdl.py fits both.
  float wdl_scale = 1.13f;
  float wdl_spread = 0.21f;
  // Halfmove clock at which static evaluation starts blending its (Q, D)
  // toward a certain draw, reaching a full draw at the 100-ply limit.
  // Static mode only: a real engine's score already accounts for the rule,
  // so -stockfish and -pgn-eval-mode must not apply it a second time.
  // See wdl::ApplyRule50Draw in WdlConversion.h.
  int r50_damp_start = 40;
  // Total pseudo visit count written per position, and the budget the played
  // move's policy share is drawn from. 0 disables it: visits stays 1 and the
  // policy target stays one-hot, which is the historical behaviour. A PGN
  // contains no search, so any value here is reconstructed from the
  // evaluation rather than measured -- see the call site in PGNGame.cpp.
  // Repair forfeited games instead of trusting the PGN result.
  //
  // A Fishtest game can end because an engine crashed or lost on time. The
  // result is then awarded against that engine whatever the position was,
  // so the stored label contradicts the stored evaluation -- the net is
  // told a dead-lost position was a win. Measured on the Fishtest-SEE
  // corpus, ~0.04% of games carry a decisive result their own final
  // evaluation does not support.
  //
  // When the final position's evaluation clearly contradicts the recorded
  // result, the evaluation is believed and the result is flipped. "Clearly"
  // is this threshold, applied to the signed agreement
  // best_q * result_q on the final ply: both are relative to the same side
  // to move there, so the product is +1 when the position supports the
  // result and negative when it contradicts it.
  //
  // 0.7 is the suggested value: at the wdl_scale/wdl_spread above that is
  // already a decisively winning position, not a mere edge. Deliberately
  // NOT lower -- a forfeit from a balanced position (agreement near 0) is
  // genuinely unknowable, and guessing a winner there would inject exactly
  // the kind of wrong label this exists to remove. Those are left alone.
  //
  // Defaults ON at 0.7. This corrects data that is actively wrong rather
  // than merely absent, so it is not something to have to remember to
  // enable; pass -forfeit-repair 0 to restore the historical behaviour.
  float forfeit_repair_threshold = 0.7f;
  int visit_budget = 0;
  // Divide the share the played move did not take among the other legal moves
  // using a temperature-scaled softmax over relative static evaluations with
  // a baseline probability floor. Preserves tactical sacrifices from being
  // zeroed out, suppresses blunders, and avoids defensive-state collapse.
  // Requires visit_budget, since without it the played move takes everything.
  bool policy_static_eval = false;
  // Softmax temperature in centipawns. Smaller sharpens: a move T centipawns
  // behind the best alternative gets weight exp(-1) of it, so T is literally
  // "how many centipawns of loss costs a factor of e". Measured over ~50k
  // positions from four Fishtest PGNs at -visit-budget 850, floor 0.001, with
  // policy_capture_lookahead on -- the target's own entropy, which is the
  // floor the policy cross-entropy converges to:
  //     even spread  1.144 nats
  //     T=300        1.116
  //     T=200        1.119
  //     T=120        1.031
  //     T=80         1.000
  //     T=40         0.976
  //     T=30         0.932
  //     T=20         0.822        <- default
  //     T=10         0.765
  // 20 rather than the 40 that was right before the capture lookahead: the
  // lookahead widens the score scale (a hanging piece is now a real 300cp
  // instead of a PST rounding error), which pushes the also-rans onto the
  // floor and leaves several near-equal safe moves sharing the leftover. At
  // T=40 that came to 0.976 nats; T=20 restores the 0.81-0.82 the sharper
  // pre-lookahead setting reached, on a ranking that now knows what is
  // hanging. T=10 buys another 0.06 nats but sharpens hard on the opinion of
  // a one-ply evaluator, which is more confidence than it has earned.
  float policy_eval_temp = 20.0f;
  // Weight added to every alternative before normalising, so no legal move
  // with leftover to share can reach probability zero. A zero policy is not
  // merely "unlikely" -- it is unreachable for MCTS, and every sacrifice
  // evaluates negative at one ply, so zeros teach the net to never look at
  // them. The floor is added per move, so its total contribution scales with
  // the move count, and the average position here has ~28 legal moves. At
  // T=40 over the same sample: floor 0.01 -> 0.904 nats, 54x; floor 0.001 ->
  // 0.812 nats, 104x. 0.01 was contributing ~0.28 of weight against the best
  // move's 1.0 -- a fifth of the leftover spread uniformly regardless of
  // evaluation, costing 0.09 nats and halving the discrimination.
  float policy_eval_floor = 0.001f;
  // Before scoring an alternative, subtract whatever the opponent's single
  // best capture reply wins, resolved with SEE. evaluate() is a one-ply score
  // and cannot see that the alternative just hung a piece; without this, the
  // material term happily reports a piece left en prise as no worse than any
  // other quiet move. This is the cheap 90% of what a quiescence search would
  // buy: one SEE per attacked piece, no move generation, no recursion.
  // Only has an effect together with policy_static_eval.
  bool policy_capture_lookahead = true;
};

struct PGNGame {
  char result[PGN_STRING_SIZE];
  // Why the game ended, straight from the PGN tag. Fishtest writes
  // "adjudication", "normal", "time forfeit" or "abandoned"; the last two
  // mean the result was awarded against an engine that crashed or ran out
  // of clock, so the label describes the engine rather than the position.
  char termination[PGN_STRING_SIZE];
  char fen[PGN_STRING_SIZE];
  std::vector<PGNMoveInfo> moves;

  PGNGame() {
    result[0] = '\0';
    fen[0] = '\0';
  }
  explicit PGNGame(pgn_t* pgn);
  std::vector<lczero::V6TrainingData> getChunks(Options options,
                                                 StockfishEvaluator* evaluator = nullptr,
                                                 int sf_depth = 10) const;
};

#endif
