#include "StockfishEvaluator.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#endif

StockfishEvaluator::StockfishEvaluator(const std::string& stockfish_path)
    : stockfish_path_(stockfish_path)
#ifdef _WIN32
    , pipe_(nullptr)
#else
    , child_pid_(-1), write_fd_(-1), read_fd_(-1)
#endif
{}

StockfishEvaluator::~StockfishEvaluator() {
  quit();
}

#ifdef _WIN32
// Windows implementation using CreateProcess and pipes
bool StockfishEvaluator::init() {
  // Simplified Windows implementation - use _popen with both read/write
  // For production, would need proper CreateProcess with STARTUPINFO pipes
  
  std::string cmd = "cmd /c \"" + stockfish_path_ + "\"";
  pipe_ = _popen(stockfish_path_.c_str(), "r+");
  
  if (!pipe_) {
    std::cerr << "Failed to start Stockfish: " << stockfish_path_ << std::endl;
    return false;
  }
  
  sendCommand("uci");
  waitFor("uciok");
  sendCommand("isready");
  waitFor("readyok");
  
  return true;
}

#else
// POSIX implementation using fork/exec
bool StockfishEvaluator::init() {
  int stdin_pipe[2];   // Parent writes, child reads
  int stdout_pipe[2];  // Child writes, parent reads

  if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
    std::cerr << "Failed to create pipes" << std::endl;
    return false;
  }

  child_pid_ = fork();
  
  if (child_pid_ < 0) {
    std::cerr << "Failed to fork" << std::endl;
    return false;
  }
  
  if (child_pid_ == 0) {
    // Child process
    close(stdin_pipe[1]);   // Close write end of stdin pipe
    close(stdout_pipe[0]);  // Close read end of stdout pipe
    
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stdout_pipe[1], STDERR_FILENO);
    
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    
    execl(stockfish_path_.c_str(), stockfish_path_.c_str(), nullptr);
    
    // If exec fails
    std::cerr << "Failed to exec Stockfish: " << stockfish_path_ << std::endl;
    _exit(1);
  }
  
  // Parent process
  close(stdin_pipe[0]);   // Close read end of stdin pipe
  close(stdout_pipe[1]);  // Close write end of stdout pipe
  
  write_fd_ = stdin_pipe[1];
  read_fd_ = stdout_pipe[0];
  
  // Make read non-blocking for timeout handling
  // fcntl(read_fd_, F_SETFL, fcntl(read_fd_, F_GETFL) | O_NONBLOCK);
  
  // Initialize UCI
  sendCommand("uci");
  if (!waitFor("uciok", 5000)) {
    std::cerr << "Stockfish did not respond to uci command" << std::endl;
    quit();
    return false;
  }
  
  sendCommand("setoption name Threads value 1");
  sendCommand("setoption name Hash value 16");
  sendCommand("isready");
  
  if (!waitFor("readyok", 5000)) {
    std::cerr << "Stockfish did not respond to isready command" << std::endl;
    quit();
    return false;
  }
  
  return true;
}
#endif

void StockfishEvaluator::setPosition(const std::string& fen) {
  std::string cmd = "position fen " + fen;
  sendCommand(cmd);
}

int StockfishEvaluator::evaluate(int depth) {
  std::ostringstream cmd;
  cmd << "go depth " << depth;
  sendCommand(cmd.str());
  
  // Read output until we get "bestmove"
  int score = 0;
  bool found_score = false;
  
  std::string line;
  while (true) {
    line = readLine();
    if (line.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    
    // Parse score from info lines
    // Example: "info depth 10 seldepth 15 score cp 35 nodes 12345 ..."
    // Or: "info depth 10 score mate 5 ..."
    
    if (line.find("score cp ") != std::string::npos) {
      std::regex cp_regex("score cp (-?\\d+)");
      std::smatch matches;
      if (std::regex_search(line, matches, cp_regex)) {
        score = std::stoi(matches[1].str());
        found_score = true;
      }
    } else if (line.find("score mate ") != std::string::npos) {
      std::regex mate_regex("score mate (-?\\d+)");
      std::smatch matches;
      if (std::regex_search(line, matches, mate_regex)) {
        int mate_in = std::stoi(matches[1].str());
        // Convert mate score to high centipawn value
        score = mate_in > 0 ? 10000 + (100 - mate_in) : -10000 - (100 + mate_in);
        found_score = true;
      }
    }
    
    if (line.find("bestmove") != std::string::npos) {
      break;
    }
  }
  
  return found_score ? score : 0;
}

float StockfishEvaluator::cpToWinProbability(int centipawns) {
  // Handle mate scores
  if (centipawns >= 10000) return 1.0f;
  if (centipawns <= -10000) return -1.0f;
  
  // Standard conversion formula
  double winrate = 1.0 / (1.0 + std::pow(10.0, -centipawns / 400.0));
  return static_cast<float>(2.0 * winrate - 1.0);
}

void StockfishEvaluator::quit() {
#ifdef _WIN32
  if (pipe_) {
    fprintf(pipe_, "quit\n");
    fflush(pipe_);
    _pclose(pipe_);
    pipe_ = nullptr;
  }
#else
  if (write_fd_ >= 0) {
    sendCommand("quit");
    close(write_fd_);
    close(read_fd_);
    write_fd_ = -1;
    read_fd_ = -1;
  }
  if (child_pid_ > 0) {
    waitpid(child_pid_, nullptr, 0);
    child_pid_ = -1;
  }
#endif
}

void StockfishEvaluator::sendCommand(const std::string& cmd) {
#ifdef _WIN32
  if (pipe_) {
    fprintf(pipe_, "%s\n", cmd.c_str());
    fflush(pipe_);
  }
#else
  if (write_fd_ >= 0) {
    std::string line = cmd + "\n";
    write(write_fd_, line.c_str(), line.size());
  }
#endif
}

std::string StockfishEvaluator::readLine() {
#ifdef _WIN32
  if (!pipe_) return "";
  
  char buffer[4096];
  if (fgets(buffer, sizeof(buffer), pipe_)) {
    std::string line(buffer);
    // Remove trailing newline
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
      line.pop_back();
    }
    return line;
  }
  return "";
#else
  if (read_fd_ < 0) return "";
  
  // Use select to check if data is available (with 100ms timeout)
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(read_fd_, &read_fds);
  
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;  // 100ms timeout
  
  int ret = select(read_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
  if (ret <= 0) {
    return "";  // Timeout or error
  }
  
  std::string line;
  char c;
  while (read(read_fd_, &c, 1) == 1) {
    if (c == '\n') break;
    if (c != '\r') line += c;
  }
  return line;
#endif
}


bool StockfishEvaluator::waitFor(const std::string& expected, int timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  
  while (true) {
    std::string line = readLine();
    if (line.find(expected) != std::string::npos) {
      return true;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed >= timeout_ms) {
      return false;
    }
    
    if (line.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}
