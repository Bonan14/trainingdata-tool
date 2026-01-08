# trainingdata-tool
Tool to generate [lc0](https://github.com/LeelaChessZero/lc0) training data. Useful for [Supervised Learning](https://github.com/dkappe/leela-chess-weights/wiki/Supervised-Learning) from PGN games.

## How to build

After cloning the repository locally, don't forget to run the following commands in order to also clone the git submodules:

```bash
git submodule sync --recursive
git submodule update --recursive --init
```

Install dependencies and build:

```bash
# Ubuntu/Debian
sudo apt-get update && sudo apt-get install -y cmake zlib1g-dev

# Build
cmake -S . -B build
cmake --build build
```

## Usage

Pass PGN input files and it will output training data in the same way lc0 selfplay does:

```bash
./build/trainingdata-tool games.pgn
```

### Options

| Option | Description |
|--------|-------------|
| `-v` | Verbose mode - shows detailed progress |
| `-lichess-mode` | Extract SF eval scores from Lichess commented games |
| `-stockfish <path>` | Use Stockfish binary to evaluate positions |
| `-sf-depth <N>` | Stockfish search depth (default: 10) |
| `-files-per-dir <N>` | Max files per directory (default: 10000) |
| `-max-games-to-convert <N>` | Limit number of games to process |
| `-chunks-per-file <N>` | Training chunks per output file |
| `-deduplication-mode` | Deduplicate existing training data |

### Examples

**Basic conversion:**
```bash
./build/trainingdata-tool games.pgn
```

**With Stockfish evaluation (generates Q-values):**
```bash
./build/trainingdata-tool -stockfish ./stockfish -sf-depth 15 games.pgn
```

**Lichess mode (for games with `[%eval]` comments):**
```bash
./build/trainingdata-tool -lichess-mode lichess_games.pgn
```

**Verbose with limits:**
```bash
./build/trainingdata-tool -v -max-games-to-convert 1000 -files-per-dir 500 games.pgn
```

## Output

Training data is written to `supervised-N/` directories containing `.gz` chunk files, one game per file.
