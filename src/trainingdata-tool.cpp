#include "chess/position.h"
#include "pgn.h"
#include "polyglot_lib.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include "PGNGame.h"
#include "StockfishEvaluator.h"
#include "TrainingDataDedup.h"
#include "TrainingDataReader.h"
#include "TrainingDataWriter.h"

size_t max_files_per_directory = 10000;
int64_t max_games_to_convert = 10000000;
size_t chunks_per_file = 4096;
size_t dedup_uniq_buffersize = 50000;
float dedup_q_ratio = 1.0f;
std::string stockfish_path;
int sf_depth = 10;
int sf_hash_mb = 128;
std::string output_prefix = "supervised-";

inline bool file_exists(const std::string &name) {
  auto s = std::filesystem::status(name);
  return std::filesystem::is_regular_file(s);
}

inline bool directory_exists(const std::string &name) {
  auto s = std::filesystem::status(name);
  return std::filesystem::is_directory(s);
}

void convert_games(const std::string &pgn_file_name, Options options,
                   StockfishEvaluator *evaluator, const std::string &prefix) {
  int game_id = 0;
  pgn_t pgn[1];
  pgn_open(pgn, pgn_file_name.c_str());
  TrainingDataWriter writer(max_files_per_directory, chunks_per_file, prefix);
  while (pgn_next_game(pgn) && game_id < max_games_to_convert) {
    PGNGame game(pgn);
    writer.EnqueueChunks(game.getChunks(options, evaluator, sf_depth));
    game_id++;
    if (game_id % 1000 == 0) {
      std::cout << game_id << " games written." << std::endl;
    }
  }
  writer.Finalize();
  std::cout << "Finished writing " << game_id << " games." << std::endl;
  pgn_close(pgn);
}

int main(int argc, char *argv[]) {
  std::cout << "TrainingData Tool v1.1 (Stockfish Arg Fix)" << std::endl;
  lczero::InitializeMagicBitboards();
  polyglot_init();
  Options options;
  bool deduplication_mode = false;

  for (size_t idx = 0; idx < argc; ++idx) {
    if (0 == static_cast<std::string>("-v").compare(argv[idx])) {
      std::cout << "Verbose mode ON" << std::endl;
      options.verbose = true;
    } else if (0 ==
               static_cast<std::string>("-pgn-eval-mode").compare(argv[idx])) {
      std::cout << "PGN eval mode ON (reading evals already in the PGN's "
                   "move comments -- no engine spawned)"
                << std::endl;
      options.pgn_eval_mode = true;
    } else if (0 ==
               static_cast<std::string>("-wdl-scale").compare(argv[idx])) {
      if (idx + 1 >= static_cast<size_t>(argc) || argv[idx + 1][0] == '-') {
        std::cerr << "Error: -wdl-scale requires a positive float argument."
                  << std::endl;
        return 1;
      }
      const char* scale_arg = argv[++idx];
      options.wdl_scale = std::atof(scale_arg);
      if (options.wdl_scale <= 0.0f) {
        std::cerr << "Error: -wdl-scale must be a positive number, got '"
                  << scale_arg << "'." << std::endl;
        return 1;
      }
      std::cout << "WDL scale (pgn-eval-mode) set to: " << options.wdl_scale
                << std::endl;
    } else if (0 ==
               static_cast<std::string>("-wdl-spread").compare(argv[idx])) {
      if (idx + 1 >= static_cast<size_t>(argc) || argv[idx + 1][0] == '-') {
        std::cerr << "Error: -wdl-spread requires a positive float argument."
                  << std::endl;
        return 1;
      }
      const char* spread_arg = argv[++idx];
      options.wdl_spread = std::atof(spread_arg);
      if (options.wdl_spread <= 0.0f) {
        std::cerr << "Error: -wdl-spread must be a positive number, got '"
                  << spread_arg << "'." << std::endl;
        return 1;
      }
      std::cout << "WDL spread (pgn-eval-mode) set to: " << options.wdl_spread
                << std::endl;
    } else if (0 ==
               static_cast<std::string>("-visit-budget").compare(argv[idx])) {
      if (idx + 1 >= static_cast<size_t>(argc) || argv[idx + 1][0] == '-') {
        std::cerr << "Error: -visit-budget requires a positive integer."
                  << std::endl;
        return 1;
      }
      const char* budget_arg = argv[++idx];
      options.visit_budget = std::atoi(budget_arg);
      if (options.visit_budget < 0) {
        std::cerr << "Error: -visit-budget must be >= 0, got '" << budget_arg
                  << "'." << std::endl;
        return 1;
      }
      std::cout << "Pseudo visit budget set to: " << options.visit_budget
                << " (policy share = 0.5 + |Q|/2, remainder spread over the "
                   "other legal moves)"
                << std::endl;
    } else if (0 ==
               static_cast<std::string>("-r50-damp-start").compare(argv[idx])) {
      if (idx + 1 >= static_cast<size_t>(argc) || argv[idx + 1][0] == '-') {
        std::cerr << "Error: -r50-damp-start requires an integer argument."
                  << std::endl;
        return 1;
      }
      const char* r50_arg = argv[++idx];
      options.r50_damp_start = std::atoi(r50_arg);
      if (options.r50_damp_start < 0 || options.r50_damp_start > 100) {
        std::cerr << "Error: -r50-damp-start must be between 0 and 100 plies, "
                     "got '"
                  << r50_arg << "'." << std::endl;
        return 1;
      }
      std::cout << "Rule-50 damping (static eval) starts at halfmove clock: "
                << options.r50_damp_start << std::endl;
    } else if (0 == static_cast<std::string>("-stockfish").compare(argv[idx])) {
      if (idx + 1 >= static_cast<size_t>(argc) || argv[idx + 1][0] == '-') {
        std::cerr << "Error: -stockfish requires a path argument." << std::endl;
        return 1;
      }
      stockfish_path = argv[++idx];
      std::cout << "Stockfish mode ON, binary: " << stockfish_path << std::endl;
      options.stockfish_mode = true;
    } else if (0 == static_cast<std::string>("-sf-depth").compare(argv[idx])) {
      if (idx + 1 >= static_cast<size_t>(argc) || argv[idx + 1][0] == '-') {
        std::cerr << "Error: -sf-depth requires a positive integer argument."
                  << std::endl;
        return 1;
      }
      const char* depth_arg = argv[++idx];
      sf_depth = std::atoi(depth_arg);
      if (sf_depth <= 0) {
        std::cerr << "Error: -sf-depth must be a positive integer, got '"
                  << depth_arg << "'." << std::endl;
        return 1;
      }
      std::cout << "Stockfish depth set to: " << sf_depth << std::endl;
    } else if (0 == static_cast<std::string>("-sf-hash").compare(argv[idx])) {
      if (idx + 1 >= static_cast<size_t>(argc) || argv[idx + 1][0] == '-') {
        std::cerr << "Error: -sf-hash requires a positive integer argument."
                  << std::endl;
        return 1;
      }
      const char* hash_arg = argv[++idx];
      sf_hash_mb = std::atoi(hash_arg);
      if (sf_hash_mb <= 0) {
        std::cerr << "Error: -sf-hash must be a positive integer (MB), got '"
                  << hash_arg << "'." << std::endl;
        return 1;
      }
      std::cout << "Stockfish hash set to: " << sf_hash_mb << " MB"
                << std::endl;
    } else if (0 ==
               static_cast<std::string>("-files-per-dir").compare(argv[idx])) {
      max_files_per_directory = std::atoi(argv[idx + 1]);
      std::cout << "Max files per directory set to: " << max_files_per_directory
                << std::endl;
    } else if (0 == static_cast<std::string>("-max-games-to-convert")
                        .compare(argv[idx])) {
      max_games_to_convert = std::atoi(argv[idx + 1]);
      std::cout << "Max games to convert set to: " << max_games_to_convert
                << std::endl;
    } else if (0 == static_cast<std::string>("-chunks-per-file")
                        .compare(argv[idx])) {
      chunks_per_file = std::atoi(argv[idx + 1]);
      std::cout << "Chunks per file set to: " << chunks_per_file << std::endl;
    } else if (0 == static_cast<std::string>("-deduplication-mode")
                        .compare(argv[idx])) {
      deduplication_mode = true;
      std::cout << "Position de-duplication mode ON" << std::endl;
    } else if (0 == static_cast<std::string>("-dedup-uniq-buffersize")
                        .compare(argv[idx])) {
      dedup_uniq_buffersize = std::atoi(argv[idx + 1]);
      std::cout << "Deduplication buffersize set to: " << dedup_uniq_buffersize
                << std::endl;
    } else if (0 ==
               static_cast<std::string>("-dedup-q-ratio").compare(argv[idx])) {
      dedup_q_ratio = std::stof(argv[idx + 1]);
      std::cout << "Deduplication Q ratio set to: " << dedup_q_ratio
                << std::endl;
    } else if (0 == static_cast<std::string>("-output").compare(argv[idx])) {
      if (idx + 1 >= static_cast<size_t>(argc) || argv[idx + 1][0] == '-') {
        std::cerr << "Error: -output requires a prefix argument." << std::endl;
        return 1;
      }
      output_prefix = argv[++idx];
      std::cout << "Output prefix set to: " << output_prefix << std::endl;
    }
  }

  // Initialize Stockfish if requested
  std::unique_ptr<StockfishEvaluator> evaluator;
  if (options.stockfish_mode) {
    evaluator =
        std::make_unique<StockfishEvaluator>(stockfish_path, sf_hash_mb);
    if (!evaluator->init()) {
      std::cerr << "Failed to initialize Stockfish. Exiting." << std::endl;
      return 1;
    }
    std::cout << "Stockfish initialized successfully." << std::endl;
  }

  TrainingDataWriter writer(max_files_per_directory, chunks_per_file,
                            "deduped-");
  for (size_t idx = 1; idx < argc; ++idx) {
    std::string arg = argv[idx];
    // Skip option flags and their values
    if (arg[0] == '-') {
      // Skip the value for options that take a parameter
      if (arg == "-stockfish" || arg == "-sf-depth" || arg == "-sf-hash" ||
          arg == "-wdl-scale" || arg == "-wdl-spread" ||
          arg == "-r50-damp-start" || arg == "-visit-budget" ||
          arg == "-files-per-dir" ||
          arg == "-max-games-to-convert" || arg == "-chunks-per-file" ||
          arg == "-dedup-uniq-buffersize" || arg == "-dedup-q-ratio" ||
          arg == "-output") {
        ++idx;  // Skip the next argument (the value)
      }
      continue;
    }

    if (deduplication_mode) {
      if (!directory_exists(argv[idx])) continue;
      TrainingDataReader reader(argv[idx]);
      training_data_dedup(reader, writer, dedup_uniq_buffersize, dedup_q_ratio);
    } else {
      if (!file_exists(argv[idx])) continue;

      // Check for .pgn extension (simple case-insensitive check)
      std::string path = argv[idx];
      if (path.length() < 4 ||
          (strcasecmp(path.substr(path.length() - 4).c_str(), ".pgn") != 0)) {
        if (options.verbose) {
          std::cout << "Skipping non-PGN file: " << path << std::endl;
        }
        continue;
      }

      if (options.verbose) {
        std::cout << "Opening '" << argv[idx] << "'" << std::endl;
      }
      convert_games(argv[idx], options, evaluator.get(), output_prefix);
    }
  }
}
