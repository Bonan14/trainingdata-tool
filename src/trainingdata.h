#if !defined(TRAININGDATA_H_INCLUDED)
#define TRAININGDATA_H_INCLUDED

#include "neural/encoder.h"
#include "neural/network.h"
#include "trainingdata/trainingdata_v6.h"

#include <cstdint>
#include <utility>
#include <vector>

// (policy index, weight) for the moves that are not the one played. Weights
// are computed from relative static evaluations using temperature-scaled softmax
// with a baseline epsilon floor, ensuring all legal moves retain positive
// probability (preserving tactical exploration and sacrifices).
using EvalPolicyWeights = std::vector<std::pair<uint16_t, float>>;

lczero::V6TrainingData get_v6_training_data(
        lczero::GameResult game_result, const lczero::PositionHistory& history,
        lczero::Move played_move, lczero::MoveList legal_moves, float Q,
        lczero::Move best_move, uint32_t visits, int plies_left,
        float D = 0.0f, float played_policy_share = 1.0f,
        const EvalPolicyWeights* eval_weights = nullptr);

#endif