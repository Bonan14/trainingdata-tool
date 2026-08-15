#include "PGNGame.h"
#include "StaticEvaluator.h"
#include "StockfishEvaluator.h"
#include "trainingdata.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <regex>

#include <sstream>
#include <vector>

float convert_sf_score_to_win_probability(float score) {
  return 2 / (1 + exp(-0.4 * score)) - 1;
}

bool extract_lichess_comment_score(const char* comment, float& Q) {
  std::string s(comment);
  // Note: brackets must be escaped with \\ in C++ regex strings
  static std::regex rgx("\\[%eval (-?\\d+(\\.\\d+)?)\\]");
  static std::regex rgx2("\\[%eval #(-?\\d+)\\]");
  std::smatch matches;
  try {
    if (std::regex_search(s, matches, rgx)) {
      Q = std::stof(matches[1].str());
      return true;
    } else if (std::regex_search(s, matches, rgx2)) {
      Q = matches[1].str().at(0) == '-' ? -128.0f : 128.0f;
      return true;
    }
  } catch (const std::exception& e) {
    // Failed to parse eval score
    return false;
  }
  return false;
  return false;
}

std::string poly_move_to_uci(move_t move, const board_t* board) {
  // Use Polyglot's board-aware canonical formatter so castling is emitted as
  // the king destination square (e.g. e1g1) rather than king-takes-rook
  // (e1h1), which standard UCI engines expect.
  char str[8];
  if (!move_to_can(move, board, str, sizeof(str))) {
    return "";
  }
  return str;
}

lczero::Move poly_move_to_lc0_move(move_t move, board_t* board,
                                   bool is_black_move) {
  // IMPORTANT: move_from() and move_to() return polyglot 0x88 format squares
  // lczero::Square::FromIdx() expects 0-63 indices
  // Use square_to_64() to convert from 0x88 to 0-63
  int from_0x88 = move_from(move);
  int to_0x88 = move_to(move);
  int from_64 = square_to_64(from_0x88);
  int to_64 = square_to_64(to_0x88);

  lczero::Square from = lczero::Square::FromIdx(from_64);
  lczero::Square to = lczero::Square::FromIdx(to_64);
  lczero::Move m;

  if (move_is_promote(move)) {
    lczero::PieceType prom_type = lczero::kKnight;
    // Polyglot: 0=None, 1=Kn, 2=Bi, 3=Ro, 4=Qu
    int promo = (move >> 12) & 7;
    switch (promo) {
      case 1:
        prom_type = lczero::kKnight;
        break;
      case 2:
        prom_type = lczero::kBishop;
        break;
      case 3:
        prom_type = lczero::kRook;
        break;
      case 4:
        prom_type = lczero::kQueen;
        break;
    }
    m = lczero::Move::WhitePromotion(from, to, prom_type);
    // Need to flip for black moves (except castling)
    if (is_black_move) {
      m.Flip();
    }
  } else if (move_is_castle(move, board)) {
    // For castling, files don't change with perspective, only ranks do
    // So castling is already in the correct orientation
    lczero::File rook_file =
        (to.file().idx > from.file().idx) ? lczero::kFileH : lczero::kFileA;
    m = lczero::Move::WhiteCastling(from.file(), rook_file);
    // Don't flip castling moves - they're perspective-independent
  } else {
    if (move_is_en_passant(move, board)) {
      m = lczero::Move::WhiteEnPassant(from, to);
    } else {
      m = lczero::Move::White(from, to);
    }
    // Lc0's board is always kept from white's perspective internally.
    // After ApplyMove(), Position::Mirror() is called to switch perspective.
    // When is_black_move is true, the polyglot board is from black's
    // perspective (after the previous mirror), so we need to flip the move
    // coordinates to white's perspective before applying it in lc0.
    if (is_black_move) {
      m.Flip();
    }
  }

  return m;
}

PGNGame::PGNGame(pgn_t* pgn) {
  strncpy(this->result, pgn->result, PGN_STRING_SIZE);
  strncpy(this->fen, pgn->fen, PGN_STRING_SIZE);

  char str[256];
  while (pgn_next_move(pgn, str, 256)) {
    this->moves.emplace_back(str, pgn->last_read_comment, pgn->last_read_nag);
  }
}

std::vector<lczero::V6TrainingData> PGNGame::getChunks(
    Options options, StockfishEvaluator* evaluator, int sf_depth) const {
  std::vector<lczero::V6TrainingData> chunks;
  lczero::ChessBoard starting_board;
  std::string starting_fen =
      std::strlen(this->fen) > 0 ? this->fen : lczero::ChessBoard::kStartposFen;
  std::vector<std::string> uci_moves;

  {
    std::istringstream fen_str(starting_fen);
    std::string board;
    std::string who_to_move;
    std::string castlings;
    std::string en_passant;
    fen_str >> board >> who_to_move >> castlings >> en_passant;
    if (fen_str.eof()) {
      starting_fen.append(" 0 0");
    }
  }

  if (options.verbose) {
    std::cout << "Started new game, starting FEN: '" << starting_fen << "'"
              << std::endl;
  }

  starting_board.SetFromFen(starting_fen, nullptr, nullptr);

  lczero::PositionHistory position_history;
  position_history.Reset(starting_board, 0, 0);
  board_t board[1];
  board_from_fen(board, starting_fen.c_str());

  lczero::GameResult game_result;
  if (strcmp(this->result, "1-0") == 0) {
    game_result = lczero::GameResult::WHITE_WON;
  } else if (strcmp(this->result, "0-1") == 0) {
    game_result = lczero::GameResult::BLACK_WON;
  } else if (strcmp(this->result, "1/2-1/2") == 0) {
    game_result = lczero::GameResult::DRAW;
  } else {
    game_result = lczero::GameResult::DRAW;  // fallback for unrecognized result
  }

  char str[256];
  // Iterate over moves with robust SAN cleaning and safe handling
  for (size_t i = 0; i < this->moves.size(); ++i) {
    const auto& pgn_move = this->moves[i];

    // ----- SAN cleaning -------------------------------------------------
    std::string san = pgn_move.move;
    // Trim leading/trailing whitespace
    san.erase(0, san.find_first_not_of(" \t\r\n"));
    if (!san.empty()) san.erase(san.find_last_not_of(" \t\r\n") + 1);
    // Remove move numbers like "1.", "23..."
    size_t dotPos = san.find('.');
    if (dotPos != std::string::npos) {
      bool precedingDigits = true;
      for (size_t j = 0; j < dotPos; ++j) {
        if (!isdigit(san[j])) {
          precedingDigits = false;
          break;
        }
      }
      if (precedingDigits) {
        san = san.substr(dotPos + 1);
        san.erase(0, san.find_first_not_of(" \t"));
      }
    }
    // Discard any PGN comment start '{' and everything after it
    size_t bracePos = san.find('{');
    if (bracePos != std::string::npos) san = san.substr(0, bracePos);
    // Remove trailing annotation symbols (!, ?, +, #, =)
    while (!san.empty() &&
           (san.back() == '!' || san.back() == '?' || san.back() == '+' ||
            san.back() == '#' || san.back() == '=')) {
      san.pop_back();
    }
    // Remove trailing period
    if (!san.empty() && san.back() == '.') san.pop_back();
    // -------------------------------------------------------------------

    int move = move_from_san(san.c_str(), board);
    if (move == MoveNone || !move_is_legal(move, board)) {
      // Continuing after an illegal SAN would leave the board and position
      // history at the previous ply while the next PGN move belongs to a
      // later position, producing a corrupted game. Abort this game instead.
      std::cerr << "Aborting game: illegal move \"" << pgn_move.move
                << "\" (parsed as \"" << san << "\")" << std::endl;
      return {};
    }

    if (options.verbose) {
      move_to_san(move, board, str, 256);
      std::cout << "Read move: " << str << std::endl;
      if (pgn_move.comment[0]) {
        std::cout << str << " pgn comment: " << pgn_move.comment << std::endl;
      }
    }

    bool bad_move = false;
    if (pgn_move.nag[0]) {
      if (pgn_move.nag[0] == '2' || pgn_move.nag[0] == '4' ||
          pgn_move.nag[0] == '5' || pgn_move.nag[0] == '6') {
        bad_move = true;
      }
    }

    // Determine if it's black's move by checking if the position history
    // indicates so
    bool is_black_move = position_history.IsBlackToMove();
    lczero::Move lc0_move = poly_move_to_lc0_move(move, board, is_black_move);

    auto legal_moves = position_history.Last().GetBoard().GenerateLegalMoves();

    // Evaluation
    float Q = 0.0f;
    float D = 0.0f;
    uint32_t visits = 1;
    std::string sf_best_move_str;

    if (options.stockfish_mode && evaluator) {
      // Use move history instead of FEN to prevent engine hangs
      evaluator->setPositionMoves(starting_fen, uci_moves);
      auto sf_result = evaluator->evaluate(sf_depth);
      if (!sf_result.ok) {
        // The search failed or timed out; writing a partially populated
        // result would corrupt the training data. Reject this game.
        std::cerr << "Aborting game: Stockfish evaluation failed" << std::endl;
        return {};
      }
      Q = StockfishEvaluator::cpToWinProbability(sf_result.score_cp);
      D = sf_result.draw_prob;
      visits = sf_result.nodes;
      sf_best_move_str = sf_result.best_move;

      if (options.verbose) {
        std::cout << "SF eval: " << sf_result.score_cp << " cp, Q=" << Q
                  << ", bestmove=" << sf_best_move_str << std::endl;
      }
    } else if (options.lichess_mode) {
      float lichess_score;
      if (pgn_move.comment[0] &&
          extract_lichess_comment_score(pgn_move.comment, lichess_score)) {
        Q = convert_sf_score_to_win_probability(lichess_score);
      } else {
        // Without a parsed %eval, the position would be written with a fake
        // Q of 0.0 indistinguishable from an equal evaluation. Abort this
        // game instead to keep the move/evaluation sequence aligned.
        std::cerr << "Aborting game: no %eval found for move \""
                  << pgn_move.move << "\"" << std::endl;
        return {};
      }
    } else {
      // Normal mode: use static evaluation
      int cp = StaticEvaluator::evaluate(board);
      Q = StaticEvaluator::cpToWinProbability(cp);
      if (options.verbose) {
        std::cout << "Static eval: " << cp << " cp, Q=" << Q << std::endl;
      }
    }

    // Restore filtering of moves explicitly marked as bad by NAG annotation
    if (options.lichess_mode && bad_move) {
      if (options.verbose) {
        std::cout << "Skipping bad move (NAG) \"" << pgn_move.move << "\""
                  << std::endl;
      }
      // Apply the move to keep the move/evaluation sequence aligned while
      // omitting this position from the chunks.
      uci_moves.push_back(poly_move_to_uci(move, board));
      position_history.Append(lc0_move);
      move_do(board, move);
      continue;
    }

    // Resolve best_move. Fall back to the known-legal played move so a
    // failed lookup can never leave a null move to be policy-mapped.
    lczero::Move best_move = lc0_move;
    if (!sf_best_move_str.empty()) {
      for (const auto& m : legal_moves) {
        // On Black's turn legal_moves are in lc0's mirrored side-to-move
        // coordinates, while Stockfish returns absolute UCI coordinates.
        // Flip a copy only for the string comparison and retain the original
        // canonical move for the training data.
        // ToString(false) produces coordinate notation e.g. "e2e4"
        lczero::Move cmp = m;
        if (is_black_move) cmp.Flip();
        if (cmp.ToString(false) == sf_best_move_str) {
          best_move = m;
          break;
        }
      }
    }

    // Note: plies_left is calculated as placeholder here (0).
    // It will be updated in post-processing after we know total game length.
    int plies_left_placeholder = 0;

    lczero::V6TrainingData chunk = get_v6_training_data(
        game_result, position_history, lc0_move, legal_moves, Q, best_move,
        visits, plies_left_placeholder, D);
    chunks.push_back(chunk);
    if (options.verbose) {
      std::string result;
      switch (game_result) {
        case lczero::GameResult::WHITE_WON:
          result = "1-0";
          break;
        case lczero::GameResult::BLACK_WON:
          result = "0-1";
          break;
        case lczero::GameResult::DRAW:
          result = "1/2-1/2";
          break;
        default:
          result = "???";
          break;
      }
      std::cout << "Write chunk: [" << poly_move_to_uci(move, board) << ", "
                << result << ", " << Q << "]" << std::endl;
    }

    // Track move for Stockfish history (canonical form needs the pre-move
    // board, e.g. for castling king-destination notation)
    uci_moves.push_back(poly_move_to_uci(move, board));

    // Apply move
    position_history.Append(lc0_move);
    move_do(board, move);
  }

  // Post-process chunks to update played_q (eval of played move) and
  // plies_left (MLH) Logic: The position after playing the move is the next
  // chunk's position. The eval of next chunk (best_q) is from opponent's
  // perspective. So value of played move for us is -next_chunk.best_q.

  if (!chunks.empty()) {
    int total_plies = static_cast<int>(chunks.size());
    for (size_t i = 0; i < chunks.size(); ++i) {
      // MLH: plies remaining until game end
      float plies_left = static_cast<float>(total_plies - i - 1);
      chunks[i].plies_left = plies_left;
      chunks[i].root_m = plies_left;
      chunks[i].best_m = plies_left;
      chunks[i].played_m = plies_left;

      // Update played_q (played move eval) from next position
      if (i < chunks.size() - 1) {
        chunks[i].played_q = -chunks[i + 1].best_q;
      }
    }
    // For the last chunk, the played move led directly to the game result
    chunks.back().played_q = chunks.back().result_q;
  }

  if (options.verbose) {
    std::cout << "Game end." << std::endl;
  }

  return chunks;
}