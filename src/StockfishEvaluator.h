#ifndef STOCKFISH_EVALUATOR_H
#define STOCKFISH_EVALUATOR_H

#include <string>
#include <cstdio>
#include <cstdint>
#include <vector>

// Manages communication with Stockfish via UCI protocol
class StockfishEvaluator {
 public:
  explicit StockfishEvaluator(const std::string& stockfish_path);
  ~StockfishEvaluator();

  // Initialize the engine (send uci, wait for uciok)
  bool init();

  // Set position from FEN string
  void setPosition(const std::string& fen);

  // Set position using startpos/fen and move list
  void setPositionMoves(const std::string& start_fen, const std::vector<std::string>& moves);

  struct Result {
    int score_cp;
    std::string best_move; // Long algebraic notation (e.g. "e2e4")
    uint32_t nodes;
    
    Result() : score_cp(0), nodes(0) {}
  };

  // Evaluate current position at given depth
  Result evaluate(int depth);

  // Convert centipawn score to win probability Q value [-1, 1]
  static float cpToWinProbability(int centipawns);

  // Shutdown the engine
  void quit();

  // Check if engine is running
  bool isRunning() const {
#ifdef _WIN32
    return pipe_ != nullptr;
#else
    return write_fd_ >= 0;
#endif
  }

 private:
  std::string stockfish_path_;
  
#ifdef _WIN32
  FILE* pipe_;
#else
  int child_pid_;
  int write_fd_;
  int read_fd_;
#endif
  
  // Send command to engine
  void sendCommand(const std::string& cmd);
  
  // Read line from engine output
  std::string readLine();
  
  // Wait for specific response
  bool waitFor(const std::string& expected, int timeout_ms = 5000);
};

#endif // STOCKFISH_EVALUATOR_H
