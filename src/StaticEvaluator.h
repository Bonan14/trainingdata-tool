#ifndef STATIC_EVALUATOR_H
#define STATIC_EVALUATOR_H

#include "polyglot_lib.h"
#include <cstdint>

// Static position evaluator for normal mode (no engine)
// Returns evaluation in centipawns from side-to-move perspective

/**
 * @brief Static position evaluator for chess positions
 * 
 * Provides material-based evaluation with piece-square tables,
 * pawn structure analysis, and mobility evaluation.
 */
class StaticEvaluator {
 public:
  /**
   * @brief Evaluate position, returns centipawns from side-to-move perspective
   * @param board Pointer to polyglot board structure
   * @return Evaluation in centipawns (positive = better for side to move)
   */
  static int evaluate(board_t* board);
  
  /**
   * @brief Convert centipawns to win probability in [-1, 1] range
   * @param cp Evaluation in centipawns
   * @return Win probability where 1.0 = certain win, -1.0 = certain loss
   */
  static float cpToWinProbability(int cp);

private:
  // Material values (centipawns)
  static constexpr int PAWN_VALUE   = 100;
  static constexpr int KNIGHT_VALUE = 320;
  static constexpr int BISHOP_VALUE = 330;
  static constexpr int ROOK_VALUE   = 500;
  static constexpr int QUEEN_VALUE  = 900;
  
  // Bonuses/penalties
  static constexpr int BISHOP_PAIR_BONUS = 50;
  static constexpr int DOUBLED_PAWN_PENALTY = -20;
  static constexpr int ISOLATED_PAWN_PENALTY = -15;
  static constexpr int PASSED_PAWN_BONUS_BASE = 20;
  static constexpr int MOBILITY_BONUS = 4;
  
  // Piece-Square Tables (from white's perspective, index 0 = a1)
  static const int PST_PAWN[64];
  static const int PST_KNIGHT[64];
  static const int PST_BISHOP[64];
  static const int PST_ROOK[64];
  static const int PST_QUEEN[64];
  static const int PST_KING_MG[64];
  static const int PST_KING_EG[64];
  
  static int evaluateMaterial(board_t* board);
  static int evaluatePST(board_t* board, int phase);
  static int evaluatePawnStructure(board_t* board);
  static int evaluateMobility(board_t* board);
  static int getPhase(board_t* board);
};

#endif // STATIC_EVALUATOR_H
