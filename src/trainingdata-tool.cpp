#include "chess/position.h"
#include "pgn.h"
#include "polyglot_lib.h"

#include <cstring>
#include <filesystem>
#include <iostream>

#include "PGNGame.h"
#include "TrainingDataDedup.h"
#include "TrainingDataReader.h"
#include "TrainingDataWriter.h"
#include "Config.h"

inline bool file_exists(const std::string &name) {
  auto s = std::filesystem::status(name);
  return std::filesystem::is_regular_file(s);
}

inline bool directory_exists(const std::string &name) {
  auto s = std::filesystem::status(name);
  return std::filesystem::is_directory(s);
}

/**
 * @brief Convert games from a PGN file to training data
 * @param pgn_file_name Path to the PGN file
 * @param options Processing options
 * @param prefix Output file prefix
 * @param config Configuration object containing processing parameters
 */
void convert_games(const std::string &pgn_file_name, Options options,
                    const std::string &prefix, const Config& config) {
  int game_id = 0;
  pgn_t pgn[1];
  pgn_open(pgn, pgn_file_name.c_str());
  TrainingDataWriter writer(config.max_files_per_directory(), config.chunks_per_file(), prefix);
  while (pgn_next_game(pgn) && game_id < config.max_games_to_convert()) {
    PGNGame game(pgn);
    writer.EnqueueChunks(game.getChunks(options));
    game_id++;
    if (game_id % 1000 == 0) {
      std::cout << game_id << " games written." << std::endl;
    }
  }
  writer.Finalize();
  std::cout << "Finished writing " << game_id << " games." << std::endl;
  pgn_close(pgn);
}

/**
 * @brief Main entry point for the training data tool
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings
 * @return 0 on success, 1 on error
 */
int main(int argc, char *argv[]) {
  lczero::InitializeMagicBitboards();
  polyglot_init();
  
  Config config;
  Options options;
  
  // Parse command-line arguments with proper bounds checking
  for (size_t idx = 0; idx < static_cast<size_t>(argc); ++idx) {
    const std::string arg = argv[idx];
    
    if (arg == "-v") {
      std::cout << "Verbose mode ON" << std::endl;
      config.set_verbose(true);
      options.verbose = true;
    } else if (arg == "-lichess-mode") {
      std::cout << "Lichess mode ON" << std::endl;
      config.set_lichess_mode(true);
      options.lichess_mode = true;
    } else if (arg == "-deduplication-mode") {
      config.set_deduplication_mode(true);
      std::cout << "Position de-duplication mode ON" << std::endl;
    } else if (arg == "-files-per-dir") {
      // Bounds checking for argument access
      if (idx + 1 >= static_cast<size_t>(argc)) {
        std::cerr << "Error: Missing argument for " << arg << std::endl;
        return 1;
      }
      const int value = std::atoi(argv[idx + 1]);
      if (value <= 0) {
        std::cerr << "Error: " << arg << " must be positive" << std::endl;
        return 1;
      }
      config.set_max_files_per_directory(static_cast<size_t>(value));
      std::cout << "Max files per directory set to: " << value << std::endl;
      idx++; // Skip the next argument as it's been consumed
    } else if (arg == "-max-games-to-convert") {
      if (idx + 1 >= static_cast<size_t>(argc)) {
        std::cerr << "Error: Missing argument for " << arg << std::endl;
        return 1;
      }
      const int64_t value = std::atoll(argv[idx + 1]);
      if (value <= 0) {
        std::cerr << "Error: " << arg << " must be positive" << std::endl;
        return 1;
      }
      config.set_max_games_to_convert(value);
      std::cout << "Max games to convert set to: " << value << std::endl;
      idx++;
    } else if (arg == "-chunks-per-file") {
      if (idx + 1 >= static_cast<size_t>(argc)) {
        std::cerr << "Error: Missing argument for " << arg << std::endl;
        return 1;
      }
      const int value = std::atoi(argv[idx + 1]);
      if (value <= 0) {
        std::cerr << "Error: " << arg << " must be positive" << std::endl;
        return 1;
      }
      config.set_chunks_per_file(static_cast<size_t>(value));
      std::cout << "Chunks per file set to: " << value << std::endl;
      idx++;
    } else if (arg == "-dedup-uniq-buffersize") {
      if (idx + 1 >= static_cast<size_t>(argc)) {
        std::cerr << "Error: Missing argument for " << arg << std::endl;
        return 1;
      }
      const int value = std::atoi(argv[idx + 1]);
      if (value <= 0) {
        std::cerr << "Error: " << arg << " must be positive" << std::endl;
        return 1;
      }
      config.set_dedup_uniq_buffersize(static_cast<size_t>(value));
      std::cout << "Deduplication buffersize set to: " << value << std::endl;
      idx++;
    } else if (arg == "-dedup-q-ratio") {
      if (idx + 1 >= static_cast<size_t>(argc)) {
        std::cerr << "Error: Missing argument for " << arg << std::endl;
        return 1;
      }
      const float value = std::stof(argv[idx + 1]);
      if (value < 0.0f || value > 1.0f) {
        std::cerr << "Error: " << arg << " must be between 0.0 and 1.0" << std::endl;
        return 1;
      }
      config.set_dedup_q_ratio(value);
      std::cout << "Deduplication Q ratio set to: " << value << std::endl;
      idx++;
    } else if (arg == "-output") {
      if (idx + 1 >= static_cast<size_t>(argc)) {
        std::cerr << "Error: Missing argument for " << arg << std::endl;
        return 1;
      }
      config.set_output_prefix(std::string(argv[idx + 1]));
      std::cout << "Output prefix set to: " << argv[idx + 1] << std::endl;
      idx++;
    }
  }

  // Process input files
  TrainingDataWriter writer(config.max_files_per_directory(), config.chunks_per_file(),
                            "deduped-");
  for (size_t idx = 1; idx < static_cast<size_t>(argc); ++idx) {
    const std::string arg = argv[idx];
    
    // Skip option arguments (they start with '-') or are option values
    if (arg.empty() || arg[0] == '-') {
      continue;
    }
    
    // Also skip numeric values that were already consumed as option arguments
    bool is_option_value = false;
    if (idx > 0) {
      const std::string prev_arg = argv[idx - 1];
      if (prev_arg == "-files-per-dir" || 
          prev_arg == "-max-games-to-convert" ||
          prev_arg == "-chunks-per-file" ||
          prev_arg == "-dedup-uniq-buffersize" ||
          prev_arg == "-dedup-q-ratio" ||
          prev_arg == "-output") {
        is_option_value = true;
      }
    }
    
    if (is_option_value) {
      continue;
    }
    
    if (config.deduplication_mode()) {
      if (!directory_exists(arg)) {
        std::cerr << "Warning: Directory does not exist: " << arg << std::endl;
        continue;
      }
      TrainingDataReader reader(arg);
      training_data_dedup(reader, writer, config.dedup_uniq_buffersize(), config.dedup_q_ratio());
    } else {
      // Sanitize input path to prevent path traversal
      const std::string sanitized_path = Config::sanitize_path(arg);
      if (sanitized_path.empty()) {
        std::cerr << "Error: Invalid or unsafe file path: " << arg << std::endl;
        continue;
      }
      
      if (!file_exists(sanitized_path)) {
        std::cerr << "Warning: File does not exist: " << sanitized_path << std::endl;
        continue;
      }
      
      if (options.verbose) {
        std::cout << "Opening '" << sanitized_path << "'" << std::endl;
      }
      convert_games(sanitized_path, options, config.output_prefix(), config);
    }
  }
  
  return 0;
}
