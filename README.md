# trainingdata-tool

Tool to generate [lc0](https://github.com/LeelaChessZero/lc0) training data. Useful for [Supervised Learning](https://github.com/dkappe/leela-chess-weights/wiki/Supervised-Learning) from PGN games.

## How to build

### 1. Clone submodules

After cloning the repository locally, ensure all git submodules are initialized and updated:

```bash
git submodule sync --recursive
git submodule update --recursive --init
```

### 2. Build instructions

#### Linux (Ubuntu / Debian)

Install dependencies and build with CMake:

```bash
sudo apt-get update && sudo apt-get install -y cmake g++ zlib1g-dev

# Configure and build
cmake -S . -B build
cmake --build build -j$(nproc)
```

The binary will be located at `./build/trainingdata-tool`.

#### Windows (Visual Studio / MSVC)

Prerequisites: [Visual Studio 2019 or 2022](https://visualstudio.microsoft.com/) with the **"Desktop development with C++"** workload installed, or the standalone Visual Studio Build Tools with CMake.

*(Note: On Windows, `zlib` is bundled directly in the repository under `zlib/`, so no external zlib installation is required.)*

Open PowerShell or Developer PowerShell for VS and run:

```powershell
# 1. Configure CMake (generates Visual Studio solution in build/)
cmake -S . -B build

# 2. Build Release binary
cmake --build build --config Release
```

The compiled executable will be located at `.\build\Release\trainingdata-tool.exe`.

#### Windows (MinGW / GCC)

If you prefer compiling with MinGW-w64:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```

The compiled executable will be located at `.\build\trainingdata-tool.exe`.

## Usage

Pass PGN input files and it will output training data in the same format lc0 selfplay produces:

```bash
# Linux
./build/trainingdata-tool games.pgn

# Windows
.\build\Release\trainingdata-tool.exe -pgn-eval-mode games.pgn
```

### Options

| Option | Description |
| --- | --- |
| `-v` | Verbose mode - shows detailed progress |
| `-pgn-eval-mode` | Read the eval already in each move's PGN comment (Fishtest/cutechess-cli's `SCORE/DEPTH TIMEs` format, e.g. `-0.76/18 1.813s`) instead of re-evaluating -- no engine spawned |
| `-wdl-scale <F>` | Scale in the WDL model for `-pgn-eval-mode` (default: `1.0`). Pinned by lc0's decode convention -- not a fitted parameter, see below |
| `-wdl-join <F>` | Eval at which the spread schedule switches from flat to solved (default: `1.5`). The only real dial; sets draw mass at equality. Must be > 0 |
| `-visit-budget <N>` | Pseudo visit count written per position (default: `0` / one-hot). When set (e.g. `850`), sets total visits and distributes played move policy share as $0.5 + \|Q\|/2$, spreading remainder over other legal moves |
| `-policy-static-eval` | Divide the share the played move did not take among the other legal moves by their static evaluation instead of evenly (default: off). Requires `-visit-budget`; see below |
| `-policy-eval-temp <F>` | Softmax temperature in centipawns for `-policy-static-eval` (default: `20`). Smaller sharpens the spread over alternatives |
| `-no-policy-capture-lookahead` | Turn off the SEE capture-reply penalty that `-policy-static-eval` applies by default (see below). Scores alternatives on the raw one-ply eval instead |
| `-see-selftest` | Run the static-exchange-evaluation self-test and exit. Prints one line per hand-checked position |
| `-policy-eval-floor <F>` | Weight added to every alternative so none reaches probability zero (default: `0.001`). Larger flattens the spread |
| `-r50-damp-start <N>` | Halfmove clock at which static evaluation starts decrementing toward a draw (default: `40`). Static mode only -- see below |
| `-stockfish <path>` | Use Stockfish binary to evaluate positions. Takes the engine's real WDL output directly, so the two flags above don't apply |
| `-sf-depth <N>` | Stockfish search depth (default: 10) |
| `-sf-hash <N>` | Stockfish hash table size in MB (default: 128) |
| `-files-per-dir <N>` | Max files per directory (default: 10000) |
| `-max-games-to-convert <N>` | Limit number of games to process |
| `-chunks-per-file <N>` | Chunks per output file in deduplication mode only; PGN conversion always writes one game per file |
| `-deduplication-mode` | Deduplicate existing training data |
| `-dedup-uniq-buffersize <N>` | Unique buffer size for deduplication mode (default: 50000) |
| `-dedup-q-ratio <F>` | Q-ratio threshold used during deduplication (default: 1.0) |
| `-threads <N>` | Number of worker threads for parallel game conversion (default: all CPU threads, e.g. `8`) |
| `-name <name>` | Custom dataset name prefix for output folders (default: `supervised` -> creates `supervised-0/`, `supervised-1/`, etc. E.g. `-name "Fishtest-New"` creates `Fishtest-New-0/`, `Fishtest-New-1/`) |
| `-output-dir <path>` | Target output directory where folders will be created (e.g. `-output-dir "C:/Users/.../training-data" -name "Fishtest"`) |
| `-output <prefix>` | Raw output prefix for generated training data files (default: `supervised-`) |

By default, the tool uses static evaluation unless `-stockfish` or `-pgn-eval-mode` is enabled.

### Examples

**Basic conversion:**

```bash
./build/trainingdata-tool games.pgn
```

**With Stockfish evaluation (generates Q-values):**

```bash
./build/trainingdata-tool -stockfish ./stockfish -sf-depth 15 games.pgn
```

**PGN eval mode (for games whose move comments already carry an eval, e.g. Fishtest PGNs):**

```bash
./build/trainingdata-tool -pgn-eval-mode fishtest_games.pgn
```

See [WDL reconstruction in PGN eval mode](#wdl-reconstruction-in-pgn-eval-mode) below for how `Q`/`D` are derived and how to tune target sharpness.

**Verbose with limits:**

```bash
./build/trainingdata-tool -v -max-games-to-convert 1000 -files-per-dir 500 games.pgn
```

## Output

Training data is written to `supervised-N/` directories containing `.gz` chunk files, one game per file.

## WDL reconstruction in PGN eval mode

### How the two eval modes differ

`-stockfish` mode asks the engine directly and gets a **real** WDL back (via `UCI_ShowWDL`), so `Q = (win - loss)/1000` and `D = draw/1000` are the engine's own numbers -- no model, no fitting. Nothing below applies to it.

`-pgn-eval-mode` only has the scalar in the PGN comment, so `(Q, D)` has to be reconstructed. That's what the rest of this section describes.

Both paths share `src/WdlConversion.h`, so a given centipawn value maps to the same `Q` either way. (If an engine doesn't report a WDL, `-stockfish` falls back to exactly the same reconstruction `-pgn-eval-mode` uses.)

### Why this exists

`-pgn-eval-mode` gives you a bare centipawn value per move, not a `(Q, D)` pair. So `Q` (win-loss) and `D` (draw probability) both have to be recovered from that single scalar.

**Both come from one model: lc0's `WDL_mu`, run backwards.** `search.cc` reports `score = 100 * mu` for that score type, so `mu` is simply the eval in pawns. Feed it into the same logistic pair `WDLRescale()` reconstructs with:

```
mu = scale · eval
W  = logistic((mu - 1) / spread)      L = logistic((-mu - 1) / spread)
Q  = W - L                            D = 1 - W - L
```

`Q` and `D` come out of the same `(W, L)`, so they're a consistent distribution by construction and can't disagree.

The two parameters in that formula are **not** two knobs of the same kind, and this is the single most important thing to understand about them:

- **`scale` is pinned at exactly `1.0`.** It is not fittable and there is nothing to measure.
- **`spread` is genuinely free** -- so free that it is no longer a constant at all, but a schedule selected by `-wdl-join`.

### `-wdl-scale` is pinned at 1.0, not fitted

lc0 inverts a value target with `a = log(1/l - 1)`, `b = log(1/w - 1)`, `mu = (a - b)/(a + b)`, and then reports `score = 100 · mu`. So `mu` **is** the eval in pawns, by convention. Setting `scale` to anything other than `1.0` means lc0 reads back `scale · eval` on every position in the corpus.

Round-trip, encoding a true eval of `3.00`:

| `-wdl-scale` | lc0 reads back | error |
| --- | --- | --- |
| **`1.0`** | **`3.0000`** | **none** |
| `1.13` | `3.3900` | +13% on every target |
| `0.77` | `2.3100` | -23% on every target |

Both of those wrong values were once shipped here as defaults, and both came from fitting something that should never have been fitted:

- **`1.13` was fitted to Fishtest outcomes.** That population is two near-identical Stockfishes with ~86% adjudication, and cutechess adjudicates a draw exactly when the eval sits near zero -- so it reports `D ≈ 1.00` at eval 0, and fitting to it saturates the value target.
- **`0.77` was fitted to a reference net's reported WDL.** Also unsound, for two independent reasons. Nets disagree with each other far too much to anchor anything: probing a KDA net gave a badly non-monotonic curve, with `W` of 0.564 at eval `+0.75` *falling* to 0.296 at `+1.5` and 0.255 at `+2.5`, then jumping to 0.893 at `+3.0`. And the UCI-reported WDL is not the raw value head anyway -- lc0 applies `WDLRescale` to it in search, so that measurement was of the search output, not the net.

Each fit was quietly correcting a systematic bias that only existed because the scale was free in the first place. **Leave `-wdl-scale` at `1.0`.** If you find yourself wanting to fit it, the model is telling you something else is wrong.

### `-wdl-join` selects a spread schedule

Because `mu = (a - b)/(a + b)` and the spread cancels out of that ratio, **`mu` is exact for any spread whatsoever**. The spread can therefore vary per position at zero cost to the round-trip -- which is what makes a schedule possible, and what makes a single constant unnecessary.

It is also what makes a single constant *insufficient*. lc0's `WDL_mu` branch abandons `mu` and falls back to rendering `45·tan(1.56728·wl)` whenever `|wl| + d >= 0.996` **or** `|centipawn| >= |100·mu|`. The second condition binds first, and the tangent diverges as `wl → 1`. Measured crossovers for a constant spread:

| constant spread | falls back at eval | consequence |
| --- | --- | --- |
| `0.85` | `3.04` | reports a `+4` position as `+8.56`, and a `+8` as `+113` |
| `1.2` | `4.50` | |
| `1.4` (lc0's `WDLMaxS` clamp) | `5.43` | |

There is no constant that escapes this. Staying on the path needs `Q < atan(100·ev/45)/1.56728`, which at `+8` is `0.9664` -- while a truthful target there needs `0.9965`. A spread wide enough to avoid fallback at `+8` (`>= 1.91`) simultaneously claims a level position is only 24.5% likely to draw. A grid search for a linear `s(ev)` valid out to `+100` returned parameters asserting a `+8` position has a 16% chance of *losing*.

The resolution is a two-branch schedule, `wdl::SpreadForEval`:

- **Above the join**, the spread is not chosen at all -- it is *solved*, per position, so that `W - L == atan(100·eval/90)/1.5637541897`. That is the constraint that lc0's other score type (the centipawn rendering) reproduces the source eval, so both of lc0's score paths agree and neither falls back. This is a derivation, not a fit.
- **Below the join**, that same equation has no solution: `mu = 1` forces `W = 0.5` exactly, while the centipawn convention puts `wl = 0.536` at 100cp, and `W >= W - L` makes the two irreconcilable. There is no solution anywhere in eval `0.2`--`1.0`, and at eval `0` the constraint degenerates entirely (`Q = 0` gives `mu = 0` for *every* `D`, so draw mass at equality is simply unconstrained). Below the join the spread is therefore held flat at its join value, `0.659`.

Resulting curve at `-wdl-join 1.5`, evaluated straight out of `wdl::ScoreToWDL`:

| eval | W | D | L |
| --- | --- | --- | --- |
| `0` | 0.1800 | **0.6401** | 0.1800 |
| `+3` | 0.8489 | 0.1204 | 0.0307 |
| `+8` | 0.9532 | 0.0265 | 0.0203 |
| `+10` | 0.9645 | 0.0181 | 0.0174 |
| `+30` | 0.9915 | 0.0024 | 0.0062 |

`D` is monotonically non-increasing across the whole range (checked at every 0.01 pawn from 0 to 30), and `W` never saturates -- it reaches `0.9915` at eval 30 and is still only `0.9993` at eval 100 -- so `L` never reaches zero and lc0 stays on its `WDL_mu` path throughout. No constant spread manages that.

The table above is the model itself, reproducible by anyone compiling against `WdlConversion.h`. A conversion run over 207,484 positions produced per-band averages consistent with it, but those are corpus statistics, not the curve.

### The flags

| Flag | Default | Effect |
| --- | --- | --- |
| `-wdl-scale <F>` | `1.0` | **Leave it.** Pinned by lc0's decode convention; any other value rescales every target in the corpus. |
| `-wdl-join <F>` | `1.5` | The one real dial. Sets the eval at which the spread schedule switches from flat to solved -- and thereby the draw mass at equality. Must be > 0. |

Those are the only two. **`-wdl-spread` and `-wdl-max` have been removed** along with the constant-spread model: the schedule produces the spread per position, and it no longer saturates `W`, so the cap had nothing left to do. Passing either flag is a hard error rather than a silent no-op -- a dropped flag that used to reshape every value target in the corpus is not something to discover after a conversion run.

The schedule is unconditional; there is no longer a setting that turns it off.

All three modes now go through it. `-stockfish`'s fallback for engines that do not report `UCI_ShowWDL`, and static-eval mode, previously used the constant-spread curve while `-pgn-eval-mode` used the schedule, so the three disagreed above the join despite comments claiming otherwise. They now produce the same curve.

Every run prints the model it is actually using:

```
WDL model: scale 1 (pinned at 1.0 by lc0's decode convention), spread schedule join 1.5 pawns
```

That line appears whether or not the flags were passed. A wrong scale is invisible in the output chunks, which is how a `1.13` default survived unnoticed for as long as it did.

The join is really the draw-mass dial at equal material:

| `-wdl-join` | `D` at eval 0 |
| --- | --- |
| `1.0` | 1.0000 (degenerate) |
| `1.5` (default) | 0.6401 |
| `2.0` | 0.5263 |
| `2.5` | 0.4564 |

Reference-net measurement put the draw rate at equality between `0.68` and `0.88` across two samples, which is what makes `1.5` the closest of these. The `1.0` row is why the join cannot simply be pushed to its mathematical limit: the curve collapses into a step function that calls every equal position a certain draw.

**Two caveats worth carrying.** The low branch is a modelling choice rather than a derivation, and it covers most of the corpus. And the curve is continuous at the join but kinks in slope there -- C0, not C1. The join must also sit where the schedule is still sensible rather than at its mathematical limit: the solved spread collapses toward 0 as eval approaches 1, turning the target into a step function, so `1.5` rather than `1.01`.

Implementation note: solving per position is far too slow, so `SpreadForEval` tabulates the bisection at 0.01-pawn resolution out to 128 pawns.

### Recommended values

**Use the defaults:**

```bash
./build/trainingdata-tool -pgn-eval-mode games-finished.pgn
```

That is `-wdl-scale 1.0 -wdl-join 1.5`, and it is what the current corpus was built with. The only parameter here worth deliberately changing is the join, and only to move draw mass at equality.

**There is nothing to fit.** Earlier versions of this document described measuring `(scale, spread)` against your own PGNs with `scripts/measure_pgn_wdl.py` and feeding the result back in. That workflow is obsolete and was wrong in principle: the scale is pinned by lc0's convention rather than by any data, and the spread is now solved per position rather than searched. The measurement scripts still exist and are still useful for *inspecting* a corpus -- what its draw rate looks like, how its evals are distributed -- but their output is no longer an input to conversion.

### Why not lc0's `WDLDrawRateReference`?

Because that parameter describes the net you are *running* -- the lc0 blog tells you to look it up by running that net from startpos and reading its WDL output. Here we aren't running a net; we're generating training data, and these are Stockfish games with their own opening book, time control and adjudication rules, so a different draw-rate characteristic entirely. Targeting an lc0 net's draw rate would aim at the wrong distribution, and would be circular if that net is the one being trained.

This objection is now doubly strong: as recorded above, a net's *reported* WDL is post-`WDLRescale` search output rather than its value head, and nets disagree with each other non-monotonically. Anchoring the corpus to whichever net is at hand bakes that net's defects into the data.

### Historical: what the outcome fit actually measured

The measured outcome distribution from one Fishtest file (6000 games, mover's perspective) is still worth reading -- not as a fitting target, but as a picture of what adjudication does to a corpus:

| eval | W | D | L | Q (real) |
| --- | --- | --- | --- | --- |
| −2.46 | 0.000 | 0.002 | 0.998 | −0.998 |
| −1.19 | 0.001 | 0.159 | 0.840 | −0.839 |
| −0.65 | 0.004 | 0.782 | 0.214 | −0.210 |
| ±0.00 | 0.001 | **0.997** | 0.002 | −0.001 |
| +0.64 | 0.159 | 0.837 | 0.004 | +0.155 |
| +1.18 | 0.782 | 0.218 | 0.001 | +0.781 |
| +2.45 | 0.996 | 0.004 | 0.000 | +0.996 |

`D = 0.997` at equality is not a fact about chess. ~86% of Fishtest games end by adjudication and cutechess adjudicates a draw precisely when the eval sits near zero, so that number is mostly the adjudication rule reporting itself. Fitting a value target to it is how a corpus ends up asserting that every equal position is a certain draw.

### `-wdl-scale` was never a game-phase control

Worth keeping on record, since it was the most common thing people tried to do with the knob before it was pinned: **`-wdl-scale` could not make endgames sharper.** It maps an eval to a distribution with no idea whether the board holds 32 pieces or 5, so raising it never reached *down* into decisive endgames -- those are already saturated -- it reached *up* into quieter, and therefore usually earlier, positions. Measured over 780,109 positions from 6,000 Fishtest games:

| band | share | what raising the scale did there |
| --- | --- | --- |
| \|eval\| >= 1.5 | 26.7% | nothing -- already `D<0.04` at every setting |
| 0.4 -- 1.5 | 49.4% | the only band that moved |
| \|eval\| < 0.4 | 23.9% | nothing until the scale got large, then collapsed too |

The worked example: at `-wdl-scale 1.8`, a ply-0 opening-book position with a PGN eval of `-0.78` came out as `Q=-0.873, D=0.127`, where the measured outcome for that band (n=82,299) is `Q=0.445, D=0.550`. It asserted an 87%-decided game before a single move had been played. It never touched the endgame; it rewrote the opening.

If what you want is a value head that converts won endgames, this was never the lever -- the targets there are already maximal, and the problem lies in search or in the M head.

### Policy sharpness and the eval distribution

With a pseudo visit budget (`-visit-budget 850`), the policy target for the played move is derived from `Q`:

$$\text{played\_policy\_share} = \max(W, 1 - W) = 0.5 + \frac{|Q|}{2}$$

so policy sharpness is tied to the same curve. The eval distribution it acts on, measured across 96,000+ Fishtest positions:

| eval range | share of positions | context in game |
| --- | --- | --- |
| **$\le 0.50$ pawns** | **21.8%** | equal openings, quiet maneuvering, symmetrical endings |
| **$0.50$ – $1.50$ pawns** | **39.0%** | significant initiative, pawn advantage, contested middlegames |
| **$1.50$ – $4.00$ pawns** | **16.4%** | decisive tactical advantage, piece up |
| **$> 4.00$ pawns** | **22.8%** | adjudication threshold band & endgame tails |

Under the old constant `spread 0.21`, anything past `+1.50` produced `Q >= 0.965`, so with ~39% of a Fishtest batch sitting above that, close to half the dataset became 98–100% one-hot. The schedule is much gentler at the same evals -- `D` is still `0.1204` at `+3` and `0.0265` at `+8` -- so that particular trap is gone. The remaining lever on policy sharpness is `-visit-budget`, not the WDL parameters.

**Still true and unrelated to any of this:** do not use shallow depth-6 engine finishers to artificially extend adjudicated games. Playing out adjudicated positions with low-depth search floods the dataset with low-quality positions evaluated between `+5.00` and `+80.00`, ruining `plies_left` targets and inflating policy loss. For clean endgame data, rely on the adjudication rules and on Syzygy rescoring (`scripts/rescore_all.py` / `rescore_chunks.py`).

### Weighting the leftover by static eval (`-policy-static-eval`)

The played move takes `0.5 + |Q|/2`. By default everything left over is split
**evenly** over the other legal moves -- a target asserting that all thirty-odd
alternatives are equally good, which is false and which the network cannot
learn. It also fixes the policy cross-entropy floor: cross-entropy cannot go
below the target's own entropy, so an even spread pins it near 1.14 nats no
matter how well the network trains.

`-policy-static-eval` replaces the even split. Every alternative is played on a
copy of the board and scored with the same static evaluator used by the default
mode, then the leftover is divided by a temperature-scaled softmax taken
relative to the best alternative:

$$w_i = \exp\!\left(\frac{cp_i - cp_{\max}}{T}\right) + \varepsilon
\qquad
p_i = (1 - \text{played\_share}) \cdot \frac{w_i}{\sum_j w_j}$$

Two details matter. **The evaluation is negated** -- `StaticEvaluator` reports
from the side-to-move's point of view, and after playing an alternative it is
the opponent to move, so the raw score has to be flipped to mean "good for the
mover". **The subtraction of $cp_{\max}$** is what keeps `exp` from overflowing
and is why $T$ is measured relative to the best alternative rather than in
absolute centipawns.

#### `-policy-eval-temp` (default `20`)

How many centipawns of loss cost a factor of $e$ in weight. A move $T$
centipawns behind the best alternative gets $e^{-1}$ of its weight; one $2T$
behind gets $e^{-2}$.

Lower is sharper. Measured over ~50,000 positions from four Fishtest PGNs
(`-visit-budget 850`, floor `0.001`, capture lookahead on), this is the entropy
of the resulting targets -- which is exactly the floor the policy loss
converges to:

| `-policy-eval-temp` | target entropy | effective moves |
|---|---|---|
| even spread (feature off) | 1.144 nats | 3.14 |
| `300` | 1.116 nats | 3.05 |
| `200` | 1.119 nats | 3.06 |
| `120` | 1.031 nats | 2.80 |
| `80` | 1.000 nats | 2.72 |
| `40` | 0.976 nats | 2.65 |
| `30` | 0.932 nats | 2.54 |
| **`20`** (default) | **0.822 nats** | **2.27** |
| `10` | 0.765 nats | 2.15 |

Above ~200 the curve flattens as the softmax approaches uniform and the target
converges back on the even spread it was meant to replace.

The default is `20` rather than the `40` that was correct before the capture
lookahead existed. The lookahead widens the score scale -- a hanging piece is
now a real 300cp rather than a PST rounding error -- which pushes the also-rans
onto the floor and leaves several near-equal *safe* moves sharing the leftover
between them. At `40` that came out at 0.976 nats; `20` restores the 0.81-0.82
the sharper pre-lookahead setting reached, on a ranking that now knows what is
hanging. `10` buys another 0.06 nats but sharpens hard on the opinion of a
one-ply evaluator, which is more confidence than it has earned.

Note that "effective moves" is $e^{H}$ averaged over *all* positions including
decided ones. Roughly 10% are adjudicated wins where $|Q| = 1$, so the played
share is 1.0 and the target is one-hot regardless of this setting; that is why
6.7% of legal moves sit at zero in every configuration. The floor applies to
the leftover, and in a decided position there is no leftover.

#### `-policy-eval-floor` (default `0.001`)

A weight added to every alternative before normalising, so no legal move that
has any leftover to share can come out at exactly zero.

This is not cosmetic. A zero-probability move is not merely unlikely, it is
*unreachable* -- MCTS will not search it. Every sacrifice evaluates negative
after one ply, because a one-ply static evaluation cannot see the
compensation, so a hard zero teaches the network never to consider sacrifices
at all. An earlier revision of this feature dropped moves scoring at or below
zero outright; it zeroed about half of all legal moves and produced fully
one-hot targets on roughly a quarter of positions. The floor exists to make
that impossible.

The floor is added *per move*, so its total influence scales with how many
legal moves there are -- and with ~28 legal moves on average, `0.01` is not
small. Holding `-policy-eval-temp 40` and varying only the floor over the same
198,869 positions:

| `-policy-eval-floor` | target entropy | median best/worst alternative |
|---|---|---|
| `0.01` | 0.904 nats | 54x |
| **`0.001`** (default) | **0.812 nats** | **104x** |

At `0.01` the floor contributes ~0.28 of weight against the best move's 1.0,
spreading roughly a fifth of the leftover uniformly regardless of evaluation:
it costs 0.09 nats and halves the discrimination between the best and worst
alternative. Raise it if you want the targets softer, but raise it knowing
that is what it does.

#### The capture-reply lookahead (on by default)

`evaluate()` is a one-ply score. It counts material, piece-square tables and
pawn structure on the position *after* the alternative, and it has no idea that
the alternative just left a rook en prise -- a hung piece and a good quiet move
score the same. Left uncorrected, that is the single largest error in the
ranking.

So before scoring an alternative, the tool walks the mover's own pieces, and
for any that the opponent attacks it resolves the exchange with **static
exchange evaluation**, then subtracts the best capture the opponent has:

$$cp = -\,\text{evaluate}(\text{after}) - \max_{p}\ \text{SEE}(p)$$

SEE plays the capture sequence out on the square, both sides always recapturing
with their cheapest attacker, so a defended piece is not mistaken for a free
one -- and because attackers are removed from the board copy as they are used,
a slider behind one joins the sequence on its own. That is what makes this
different from just counting attackers.

It costs about **30%** on top of `-policy-static-eval` (400 games: 1.28 s to
1.67 s; 1.00 s with the feature off entirely). It is that cheap because there
is no move generation and no recursion: the cheapest-attacker scan doubles as
the "is anything actually attacked here" test, so the board copy is only paid
on squares that are.

Pass `-no-policy-capture-lookahead` to score on the raw one-ply eval instead.
If you do, put `-policy-eval-temp` back to around `40` -- the two are fitted
together.

`-see-selftest` runs the exchange logic against hand-checked positions
(undefended piece, defended piece, equal trade, a capture that is illegal
because only the king attacks, and an x-ray). Expected values follow from this
tool's piece values, so they move if those constants do.

#### What it still does not fix

SEE resolves the exchange on one square. It does not see a fork, a discovered
attack, a pin that only bites next move, or any tactic that needs a quiet move
in the middle -- that would take a real quiescence search, which costs roughly
an order of magnitude rather than 30%. It also ignores promotions, en passant,
and treats a pinned defender as a defender.

Sacrifices are pushed toward the floor rather than promoted. A real sacrifice
loses material by definition, and one ply of capture resolution cannot see the
compensation. The point of the floor is that they stay *reachable*, not that
they are taught as candidates.

### Inspecting a corpus

The scripts below **no longer feed anything back into conversion** -- see "There is nothing to fit" above. They remain useful for looking at what a corpus actually contains.

| Script | What it does |
| --- | --- |
| `measure_pgn_wdl.py` | Buckets PGN positions by the eval in their comment and counts real win/draw/loss frequencies per band. Read it as a description of the source games (and of how hard adjudication shapes them), not as a source of parameters. |
| `measure_pgn_draw_rate.py` | Draw rate per eval band, the narrower version of the above. |
| `measure_draw_rate.py` | Draw rate from a network's own V6 chunks. |
| `calibrate_s.py` | Average/median real `D` for near-equal positions only. Quickest sanity check that a dataset looks like you expect. |
| `calibrate_s2.py` | Compares real `D` against the model across `|Q|` bands. |
| `calibrate_s3.py` | Two-parameter grid search over V6 chunks. Historical -- the parameters it searches are no longer free. |

If your chunks are packed in `.tar` archives (as lc0 training runs ship them), extract one first:

```bash
tar -xf training-run2-....tar -C /tmp/chunks
py scripts/calibrate_s3.py "/tmp/chunks/*/*.gz" 800
```

**One trap that still applies when reading any of these:** `Q` and an eval are not the same scale. `Q=0.76` is a nearly-won position; an eval of `0.76` is only a modest edge. The model takes the **eval**, so anything measured against V6 chunks needs its `Q` mapped back to an eval before it can be compared with a PGN-derived number.

## The 50-move rule in static eval

Static evaluation counts material and structure. It has no search behind it, so it cannot see a draw coming: a position a rook up reads as won even when the halfmove clock is at 99 and the game is drawn on the next ply. Left alone, that writes confidently winning targets for positions that are dead drawn.

Only two kinds of move reset the halfmove clock -- a **pawn move** (push or promotion) and a **capture** (including en passant). Everything else pushes it one ply closer to the 100-ply limit. So the penalty is attached to the clock the played move *leaves behind*:

```
c = (clock_after_move - damp_start) / (100 - damp_start)     clamped to [0, 1]

Q -> Q * (1 - c)
D -> D + (1 - D) * c
```

which is a straight interpolation toward a certain draw in W/D/L space, `(W,D,L) -> (1-c)·(W,D,L) + c·(0,1,0)`. At the limit it gives exactly `Q=0, D=1`.

Scoring the move's *resulting* clock rather than the one it inherited is the whole point: a capture or pawn push is scored at full value however long the shuffling before it ran, because those are the moves that make progress. A quiet move gets decremented by a little more each time. Shuffling a rook while a promotion is available is penalised; playing the promotion is not.

Below `-r50-damp-start` (default `40`, i.e. 20 full moves) nothing happens at all. A moderate clock carries no information -- twenty-odd plies of endgame maneuvering is ordinary play, not shuffling -- and damping from ply 1 would bias every long endgame toward a draw. Raise it to penalise only genuine shuffling; lower it to push the value head harder toward draws in slow endgames.

Worked example (rook up, clock starting at 60, `-r50-damp-start 40`):

| move | clock after | c | Q | D |
| --- | --- | --- | --- | --- |
| `Rh1` | 61 | 0.35 | 0.650 | 0.350 |
| `Rh2` | 63 | 0.38 | 0.617 | 0.383 |
| `Rh3` | 65 | 0.42 | 0.583 | 0.417 |
| `g8=Q` | **0** | 0.00 | **1.000** | **0.000** |
| `Qg4+` | 2 | 0.00 | 1.000 | 0.000 |

> **Static mode only.** `-stockfish` and `-pgn-eval-mode` take their numbers from a real engine, which already accounts for the rule -- Stockfish damps its own eval by the halfmove clock, and its search sees the terminal draw outright. Applying this on top would double-count it, so neither mode does. `-pgn-eval-mode` output is byte-identical with and without the flag.

Note that `D` is now populated in static mode. It was previously always `0.0`, which claims a certain decisive result for every position -- damping `Q` alone would have made that worse, producing `Q=0, D=0` ("certain, and equally likely won or lost") for exactly the drawn positions this is meant to describe. Both come from `wdl::ScoreToWDL`, the same model the other two modes use.

## Finishing prematurely-adjudicated games

Fishtest/cutechess-cli test games are usually stopped early by adjudication (a sustained eval imbalance) rather than played out to an actual checkmate, stalemate, or rule-based draw. That's fine for measuring engine strength, but it means the moves-left-head (M) training target -- which `trainingdata-tool` computes as real plies remaining until the *recorded* end of the game -- never gets a chance to count down to an actual conclusion; it just stops short wherever adjudication cut the game off.

`scripts/finish_games.py` fixes that up front, before conversion: for every game whose `[Termination]` is `"adjudication"`, it keeps playing both sides with Stockfish -- shallow and fast by default, since the goal is just a real conclusion, not a strong one -- until the position is actually checkmate, stalemate, or a claimable rule draw (50-move/repetition/insufficient material), or a safety ply cap is hit. Games already at a real conclusion are copied straight through unchanged.

Each added move gets an eval comment in the exact same self-relative `{SCORE/DEPTH TIMEs}` / `{+M<N>/DEPTH TIMEs}` format real Fishtest comments use, so the output needs no further changes to be read straight into `-pgn-eval-mode`.

```bash
py scripts/finish_games.py games.pgn.gz --stockfish "C:\path\to\stockfish.exe"
```

This writes `games-finished.pgn` next to the input by default. It accepts multiple input files, and both plain `.pgn` and gzip-compressed `.pgn.gz` (output is always plain `.pgn` -- `trainingdata-tool` doesn't read gzip PGNs directly). A progress bar shows games processed, how many were extended, and how many reached a real conclusion vs. hit the ply cap unresolved.

Only **decisive** adjudications get finished by default -- a game adjudicated as a draw already has the right result, and there's no mate distance being cut short to correct, so playing it out with a shallow engine would just burn time for no benefit (often grinding to the ply cap in an equal position instead of resolving). Pass `--include-draws` to finish those too anyway.

| Option | Description |
| --- | --- |
| `--stockfish <path>` | Path to the Stockfish binary (default: the copy under `Documents\Stockfish`) |
| `--depth <N>` | Search depth per continuation move (default: 10) |
| `--max-extra-plies <N>` | Safety cap on plies added per game (default: 300) |
| `--workers <N>` | Parallel worker processes, one Stockfish instance each (default: `cpu_count - 1`) |
| `--threads <N>` | UCI `Threads` per Stockfish instance (default: 1 -- parallelism comes from `--workers` instead) |
| `--hash <N>` | UCI `Hash` MB per Stockfish instance (default: 64) |
| `--only-termination <value>` | Only finish games with this `[Termination]` value (repeatable; default: just `adjudication`) |
| `--include-draws` | Also finish games adjudicated as a draw (default: skipped -- see above) |
| `--output <path>` | Output path (single input file only) |
| `--output-dir <dir>` | Directory for outputs (multiple inputs) |
| `--limit <N>` | Stop after this many games per input file (handy for a quick test run) |
| `--resume` | Skip inputs whose finished output already exists -- see below |
| `--no-progress` | Disable the progress bar (and the game-counting pre-pass it needs) |

Quick test on a handful of games before committing to a full run:

```bash
py scripts/finish_games.py games.pgn --limit 20 --depth 8 --workers 2
```

### Surviving an interrupted run

A full multi-file run takes hours, and anything that kills the shell kills it. Two things make that cheap to recover from:

- Each output is written to `<name>-finished.pgn.partial` and renamed only after the file completes. A killed run therefore never leaves a truncated file under the real name.
- `--resume` skips any input whose finished output already exists, so a restart picks up at the file that was in flight.

```bash
py -u scripts/finish_games.py *.pgn.gz --output-dir finished-all --resume
```

Use `py -u`. Without it, stdout is block-buffered when redirected to a log, and the per-file `Done:` summaries are lost if the run is killed -- leaving no record of which files finished. The progress bar still appears either way (it goes to stderr), which makes the loss easy to miss.

> **One caveat.** An output left behind by a run from *before* the `.partial` mechanism existed may be truncated, and `--resume` will treat it as complete. Delete the most recently written output before resuming over such a run. To check a file rather than guess, compare game counts: `grep -c '^\[Event ' out.pgn` against the same count in the input.

Then feed the result straight into conversion:

```bash
./build/trainingdata-tool -pgn-eval-mode games-finished.pgn
```

## Rescoring with Syzygy tablebases

Rescoring replaces guessed labels with the truth: any position that reaches a tablebase gets its real game-theoretic result and its real distance to the end. That corrects both `result_q`/`result_d` and the `plies_left` (M) target.

`scripts/rescore_chunks.py` drives lc0's `rescore_chunk` across a whole tree:

```bash
py scripts/rescore_chunks.py C:\path\to\chunks --syzygy C:\path\to\syzygy --replace
```

| Option | Description |
| --- | --- |
| `--syzygy <dir>` | Tablebase directory (default: the local `syzygy-4-5`) |
| `--rescorer <path>` | `rescore_chunk` binary (default: the local build) |
| `--workers <N>` | Parallel processes (default: `cpu_count - 1`) |
| `--replace` | Move each rescored chunk over its original once written -- keeps disk flat |
| `--resume` | Skip chunks already carrying a `_rescored.gz` twin |
| `--dist-temp`, `--dist-offset`, `--dtz-boost` | Passed through to the rescorer |

**Why this binary and not lc0's `rescorer`.** The standalone `rescorer` takes a whole directory in one process, but `--delete-files` defaults to *true* and its `remove()` sits outside the try/catch -- so it deletes its inputs on failure as well as on success. `rescore_chunk` has no delete logic at all: it reads one chunk and writes `<stem>_rescored.gz` beside it. This driver adds the parallelism that costs, and never removes an original except via `--replace`, and then only after a confirmed successful write.

Budget roughly 0.12s per chunk per worker, including tablebase init (which is mmap'd and cheap). A million chunks is a few hours across 7 workers.

### What it actually changes

Measured over 20 converted Fishtest games (3,307 frames, 3-4-5 tablebases):

| field | frames changed |
| --- | --- |
| `plies_left` | 27.3% |
| `result_q` | 18.1% |
| `result_d` | 18.1% |

That `result_q` figure is not noise, and it is worth understanding. Games finished by a shallow search routinely fail to convert won positions: one sampled game reached a rook-up position evaluated at `+4.7`, shuffled (`Re4 Re8 Re4 Rd4`) without making progress, and was recorded `1/2-1/2` by the 50-move rule. The tablebase relabels it as the win it was.

So finishing and rescoring are complementary, not alternatives -- finishing gets the game to a real conclusion, rescoring fixes the conclusions the finisher got wrong. Deeper `--depth` in `finish_games.py` reduces how many need fixing, and larger tablebases catch more of the rest; with 3-4-5 only, a game that shuffles into a 50-move draw with six pieces on the board stays mislabelled.

> **Expect MLH warnings afterwards.** `verify_chunks.py` checks `plies_left` against a simple ply-order count, which is right for freshly converted chunks and *wrong by design* after rescoring -- the whole point is that M now reflects real distance-to-conclusion. Mismatches there are evidence the rescorer worked, not that something broke.

## Packing chunks into archives

`scripts/pack_chunks.py` turns the converted directory tree into `.tar` archives laid out the way lc0 ships its training runs (members stored as `<dir>/<chunk>.gz`, so extracting recreates the layout):

```bash
py scripts/pack_chunks.py C:\path\to\chunks --output-dir C:\path\to\archives
```

| Option | Description |
| --- | --- |
| `--output-dir <dir>` | Where archives are written (required) |
| `--group <N>` | Chunk directories per archive (default: 1) |
| `--compress none\|gz` | Default `none` -- see below |
| `--resume` | Skip archives that already exist |
| `--no-verify` | Skip re-reading each archive to confirm its member count |

Each archive is written to a `.partial` and renamed only after its member count is verified, so an interrupted run never leaves a truncated `.tar` looking complete. Sources are never modified.

**Compression defaults to none on purpose.** The members are already gzipped chunks; re-compressing the tar buys almost nothing for a lot of CPU, which is why lc0 distributes plain `.tar`. `--compress gz` exists if you want to measure it yourself.

> **These archives are for storage and transfer, not for training directly.**
> Nothing in `tf/` reads tars -- `train.py:fast_get_chunks` walks one level of subdirectories collecting loose `.gz` files, and `chunkparser.py` opens each one with `gzip.open`. Extract before training:
> 
> ```bash
> tar -xf archives/sup01-0.tar -C /path/to/chunks
> ```

## Verifying converted chunks

`scripts/verify_chunks.py` reads the `.gz` chunk files `trainingdata-tool` writes and prints the decoded `V6TrainingData` fields per move -- useful for sanity-checking a conversion, especially the moves-left (M) target after using `finish_games.py`. It reads the real on-disk `plies_left`/`root_m`/`best_m`/`played_m` fields (not a guess) and cross-checks `plies_left` against the ply-order count it should have; it should count down to exactly 0 on the real final move of the game, and the script flags any chunk where the two disagree.

```bash
py scripts/verify_chunks.py supervised-0/game_000000.gz
```

Pass a directory instead of a single file to walk every `.gz` chunk under it:

```bash
py scripts/verify_chunks.py supervised-0/
```

For each move it prints `PliesLeft` (the M target), `RootQ`/`BestQ`/`ResultQ`, the played/best move indices, visit count, rule50 count, and castling rights, plus a running total of moves processed across all files.
