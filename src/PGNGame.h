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
  // W = logistic((mu-1)/spread), L = logistic((-mu-1)/spread), giving
  // Q = W-L and D = 1-W-L together. This is lc0's "WDL_mu" model run
  // backwards -- search.cc reports score = 100*mu, so mu is simply the
  // eval in pawns. See wdl::ScoreToWDL in WdlConversion.h.
  //
  // wdl_scale MUST be 1.0 and is NOT a fittable parameter. lc0 inverts a
  // value target with a = log(1/l-1), b = log(1/w-1), mu = (a-b)/(a+b),
  // and reports score = 100*mu -- so mu IS the eval by convention, and any
  // other scale makes lc0 read back scale*eval on every position. Verified
  // by round-trip: scale 1.0 recovers an encoded eval of 3.00 as 3.0000,
  // scale 1.13 as 3.3900 (+13%), scale 0.77 as 2.3100 (-23%).
  //
  // Both of those wrong values were once defaults here, and both came from
  // fitting: 1.13 against Fishtest outcomes (a population of two
  // near-identical Stockfishes with ~86% adjudication, which reports D~1.00
  // at eval 0 and saturates the target), and 0.77 against a reference net's
  // reported WDL (which is post-WDLRescale search output, not the value
  // head, and which disagrees non-monotonically between nets). Each was
  // correcting a bias that only existed because scale was free. Do not
  // re-fit it; scripts/measure_pgn_wdl.py's grid search over (scale,
  // spread) is retained for inspecting a corpus, not for setting these.
  //
  // Do not substitute lc0's "centipawn" score type (cp = 90*tan(1.5637541897*Q))
  // as the win-probability model either -- it is a display convention. It
  // does appear below as the CONSTRAINT that pins the spread above the join,
  // which is a different use: there we demand agreement with it so lc0's two
  // score paths do not diverge, rather than treating it as calibrated.
  //
  // Deliberately NOT lc0's WDLDrawRateReference. That parameter describes
  // the net you are *running* (looked up by running it from startpos and
  // reading its WDL), but here we are generating training data, and these
  // are Stockfish games with their own book, time control and adjudication
  // -- a different distribution entirely. Targeting an lc0 net's draw rate
  // would aim at the wrong thing, and would be circular if that net is the
  // one being trained.
  //
  // Caveat on the source data: ~86% of Fishtest games end by adjudication,
  // and cutechess adjudicates a draw exactly when the eval sits near zero,
  // so the observed outcome curve is partly the adjudication rule reporting
  // itself. That is precisely why fitting to it was the wrong objective.
  float wdl_scale = 1.0f;
  // Eval (in pawns) at which the spread schedule switches from flat to
  // centipawn-derived. See wdl::SpreadForEval. Must be > 0: the schedule is
  // unconditional now, and the old constant-spread escape hatch (wdl_spread,
  // wdl_max, and wdl_join 0) has been removed.
  //
  // At 1.5 the curve keeps lc0 on its WDL_mu score path at EVERY eval from 0
  // to 30 -- no constant spread manages that -- with mu exact throughout, D
  // falling monotonically (0.640 at eval 0, 0.297 at 1.5, 0.120 at 3, 0.027
  // at 8, 0.002 at 30) and W never saturating: 0.9915 at eval 30 and still
  // only 0.9993 at 100, which is why no separate cap is needed.
  float wdl_join = 1.5f;
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
  // 0.7 is the suggested value: on the curve above that is
  // already a decisively winning position, not a mere edge. Deliberately
  // NOT lower -- a forfeit from a balanced position (agreement near 0) is
  // genuinely unknowable, and guessing a winner there would inject exactly
  // the kind of wrong label this exists to remove. Those are left alone.
  //
  // Defaults ON at 0.7. This corrects data that is actively wrong rather
  // than merely absent, so it is not something to have to remember to
  // enable; pass -forfeit-repair 0 to restore the historical behaviour.
  float forfeit_repair_threshold = 0.7f;
  // Drop games shorter than this many plies instead of converting them.
  //
  // Fishtest occasionally emits 1-3 ply games when a worker crashes at the
  // start of a match: the opening book position is fine and the eval is
  // near level, but a decisive result gets awarded anyway, so every
  // position in them is mislabelled. Under a count-mode sampler they are
  // worse than useless, because a 2-ply game still contributes its full
  // position_count -- the same one or two positions repeated dozens of
  // times. The previous corpus had 37 of these and they had to be found
  // and stripped afterwards with a separate script; this makes it a
  // conversion-time decision instead.
  //
  // Defaults to 10 plies (5 full moves). Nothing Fishtest produces that
  // short is trustworthy: with cutechess resign adjudication needing
  // several consecutive losing scores before it fires, a game ending
  // inside 10 plies is nearly always a crashed or abandoned worker rather
  // than a real collapse. A genuine instant loss out of an imbalanced UHO
  // book position is possible, so this does discard a small number of real
  // games -- that is the intended trade, since the mislabelled ones are
  // actively harmful and the real ones are merely a handful. Pass
  // -min-plies 0 to convert everything.
  int min_plies = 10;
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
