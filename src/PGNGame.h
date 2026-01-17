#if !defined(PGN_GAME_H_INCLUDED)
#define PGN_GAME_H_INCLUDED

#include "neural/encoder.h"
#include "neural/network.h"
#include "trainingdata/trainingdata_v6.h"
#include "pgn.h"
#include "polyglot_lib.h"
#include "PGNMoveInfo.h"

class PGNMoveInfo;

struct Options {
  bool verbose = false;
  bool lichess_mode = false;
};

/**
 * @brief Represents a chess game parsed from PGN format
 */
struct PGNGame {
  char result[PGN_STRING_SIZE];
  char fen[PGN_STRING_SIZE];
  std::vector<PGNMoveInfo> moves;

  /**
   * @brief Construct a PGNGame from polyglot pgn_t structure
   * @param pgn Pointer to polyglot pgn_t structure containing game data
   */
  explicit PGNGame(pgn_t* pgn);
  
  /**
   * @brief Convert game to training data chunks
   * @param options Processing options for chunk generation
   * @return Vector of V6 training data chunks
   */
  std::vector<lczero::V6TrainingData> getChunks(Options options) const;
};

#endif
