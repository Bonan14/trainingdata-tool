#!/usr/bin/env python3
import struct
import gzip
import os
import sys

# V6TrainingData structure layout
# uint32_t version;
# uint32_t input_format;
# float probabilities[1858];
# uint64_t planes[104];
# uint8_t castling_us_ooo;
# uint8_t castling_us_oo;
# uint8_t castling_them_ooo;
# uint8_t castling_them_oo;
# uint8_t side_to_move_or_enpassant;
# uint8_t rule50_count;
# uint8_t invariance_info;
# uint8_t dummy;
# float root_q;
# float best_q;
# float root_d;
# float best_d;
# float root_m;
# float best_m;
# float plies_left;
# float result_q;
# float result_d;
# float played_q;
# float played_d;
# float played_m;
# float orig_q;
# float orig_d;
# float orig_m;
# uint32_t visits;
# uint16_t played_idx;
# uint16_t best_idx;
# float policy_kld;
# uint32_t reserved;

STRUCT_FMT = (
    "<"      # Little endian
    "I"      # version
    "I"      # input_format
    "1858f"  # probabilities
    "104Q"   # planes
    "8B"     # castling/rule50/invariance/dummy
    "15f"    # root_q ... orig_m
    "I"      # visits
    "H"      # played_idx
    "H"      # best_idx
    "f"      # policy_kld
    "I"      # reserved
)

STRUCT_SIZE = 8356

def read_chunks(filename):
    print(f"Reading {filename}...")
    try:
        with gzip.open(filename, "rb") as f:
            while True:
                data = f.read(STRUCT_SIZE)
                if not data:
                    break
                if len(data) != STRUCT_SIZE:
                    print(f"Error: Incomplete chunk, got {len(data)} bytes, expected {STRUCT_SIZE}")
                    break
                
                unpacked = struct.unpack(STRUCT_FMT, data)
                
                # Extract relevant fields for verification
                version = unpacked[0]
                input_format = unpacked[1]
                # probabilities start at index 2, end at 2+1858
                probs_end = 2 + 1858
                # planes start at probs_end, end at probs_end+104
                planes_end = probs_end + 104
                # uint8s start at planes_end
                uint8s_start = planes_end
                castling_us_ooo = unpacked[uint8s_start]
                castling_us_oo = unpacked[uint8s_start+1]
                castling_them_ooo = unpacked[uint8s_start+2]
                castling_them_oo = unpacked[uint8s_start+3]
                side_to_move = unpacked[uint8s_start+4]
                rule50 = unpacked[uint8s_start+5]
                invariance = unpacked[uint8s_start+6]
                dummy = unpacked[uint8s_start+7]

                # Check 15 floats starting after uint8s
                floats_start = planes_end + 8
                root_q = unpacked[floats_start]
                best_q = unpacked[floats_start+1]
                root_d = unpacked[floats_start+2]
                best_d = unpacked[floats_start+3]
                
                result_q = unpacked[floats_start+7]
                
                # Check indices
                visits = unpacked[floats_start+15]
                played_idx = unpacked[floats_start+16]
                best_idx = unpacked[floats_start+17]
                policy_kld = unpacked[floats_start+18]
                
                yield {
                    "version": version,
                    "input_format": input_format,
                    "root_q": root_q,
                    "best_q": best_q,
                    "result_q": result_q,
                    "visits": visits,
                    "played_idx": played_idx,
                    "best_idx": best_idx,
                    "policy_kld": policy_kld,
                    "rule50": rule50,
                    "castling": (castling_us_ooo, castling_us_oo, castling_them_ooo, castling_them_oo),
                    "raw_size": len(data)
                }
    except Exception as e:
        print(f"Error reading {filename}: {e}")

def main():
    if len(sys.argv) < 2:
        print("Usage: verify_chunks.py <path_to_gzipped_chunk_file> [dir]")
        sys.exit(1)

    path = sys.argv[1]
    
    files = []
    if os.path.isdir(path):
        for root, _, filenames in os.walk(path):
            for f in filenames:
                if f.endswith(".gz"):
                    files.append(os.path.join(root, f))
    else:
        files.append(path)

    total_moves = 0
    for f in sorted(files):
        print(f"--- File: {f} ---")
        moves = list(read_chunks(f))
        total_moves += len(moves)
        num_moves = len(moves)
        for i, move in enumerate(moves):
            # Reverse increment for MLH (Moves Left Head): plies remaining until game end
            plies_left = num_moves - i - 1
            print(f"  Move {i} (MoveId={move['played_idx']}): PliesLeft={plies_left}, Version={move['version']}, Format={move['input_format']}, "
                  f"ResultQ={move['result_q']:.4f}, RootQ={move['root_q']:.4f}, BestQ={move['best_q']:.4f}, "
                  f"PlayedIdx={move['played_idx']}, BestIdx={move['best_idx']}, Visits={move['visits']}, "
                  f"Rule50={move['rule50']}, Castling={move['castling']}")
        print(f"  Total moves in file: {num_moves}\n")

    print(f"Total moves processed: {total_moves}")

if __name__ == "__main__":
    main()
