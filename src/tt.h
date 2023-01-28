/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef TT_H_INCLUDED
#define TT_H_INCLUDED

#include "misc.h"
#include "types.h"

namespace Stockfish {

/// TTEntry struct is the 10 bytes transposition table entry, defined as below:
///
/// key        16 bit
/// depth       8 bit
/// generation  5 bit
/// pv node     1 bit
/// bound type  2 bit
/// move       16 bit
/// value      16 bit
/// eval value 16 bit
///
/// The generation is used to compare the age of different entries. The generation,
/// pv and bound bits are all stored in a single uint8_t, the generation being the
/// higher bits, and the others the lower bits. See below for more on the generation.

struct TTEntry {

  Move  move()  const { return (Move )move16; }
  Value value() const { return (Value)value16; }
  Value eval()  const { return (Value)eval16; }
  Depth depth() const { return (Depth)depth8 + DEPTH_OFFSET; }
  bool is_pv()  const { return (bool)(genBound8 & 0x4); }
  Bound bound() const { return (Bound)(genBound8 & 0x3); }
  void save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev);

private:
  friend class TranspositionTable;

  uint16_t key16;
  uint8_t  depth8;
  uint8_t  genBound8;
  uint16_t move16;
  int16_t  value16;
  int16_t  eval16;
};


/// A TranspositionTable is an array of Cluster, of size clusterCount. Each
/// cluster consists of ClusterSize number of TTEntry. Each non-empty TTEntry
/// contains information on exactly one position. The size of a Cluster should
/// divide the size of a cache line for best performance, as the cacheline is
/// prefetched when possible.

class TranspositionTable {

  static constexpr int ClusterSize = 3;

  struct Cluster {
    TTEntry entry[ClusterSize];
    char padding[2]; // Pad to 32 bytes
  };

  static_assert(sizeof(Cluster) == 32, "Unexpected Cluster size");

  // These constants are used to manipulate the 5 generation bits, controlling
  // the age of entries. With 5 bits, we can record up to 32 generations.
  // The global TT.generation8 (see below) tracks the current generation. The
  // generation is incremented by `MainThread::search()`, i.e. once for every
  // `go` command, allowing overflows. We *assume* that all TTEntries have been
  // set/hit within the last 32 generations, thus an entry's age is essentially
  // `TT.generation8 - (TTE.genBound8 & GENERATION_MASK)` (plus adjusting for the
  // overflows). Theoretically, if an entry survives unhit for 33 consecutive `go`
  // commands (33 moves in a game), it would then appear to be only 1 generation old.
  static constexpr unsigned GENERATION_NONBITS = 3;                       // nb of bits reserved for other things
  static constexpr uint8_t  GENERATION_INCR    = 1 << GENERATION_NONBITS; // increment the generation field, preserving the nongen bits
  static constexpr uint8_t  GENERATION_NONMASK = GENERATION_INCR - 1;     // retrieve nongeneration bits
  static constexpr uint8_t  GENERATION_MASK    = ~GENERATION_NONMASK;     // retrieve generation bits
  // To account for overflow, where the numeric value of `TTE.genBound8` is greater
  // than `TT.generation8`, we add an extra bit "above" `TT.generation8` (more
  // than 8 bits!) for the subtraction to borrow from and give a positive result.
  // If `generation8` was larger, then the extra bit remains but we can just mask
  // it away. Said another way, we do the subtraction modulo 32.
  // We must also account for the nongen bits of `genBound8` in the subtraction.
  // One could simply subtract `genBound8 & GEN_MASK` as above, but we can save
  // that extra runtime operation by instead setting the minuend nongen bits to
  // 1 at compiletime. That guarantees the subtraction won't borrow from the gen
  // bits, and we already mask out these low bits anyways as part of the modulo.
  static constexpr uint16_t GENERATION_MODULUS = (uint16_t(GENERATION_MASK) + GENERATION_INCR) | GENERATION_NONMASK;
  inline uint8_t GENERATION_AGE_BY8(uint8_t TTEgenBound8) const
  { // Note that we leave the numeric age in the upper bits, (age) << (GENERATION_NONBITS),
    // so that the numeric result is 8*age, which is key to the replacement algorithm.
    // It is also key, per above, that the sum happens before the difference.
    return ((GENERATION_MODULUS + generation8) - TTEgenBound8) & GENERATION_MASK;
  } // Example: TT.gen is 3, tte.gen is 31, then modulus+gen8 is 0b 1 00011 111, and
    // genbound8 is 0b 11111 xxx, and the difference is 0b 0 00100 yyy --> age is 4.

public:
 ~TranspositionTable() { aligned_large_pages_free(table); }
  void new_search() { generation8 += GENERATION_INCR; } // Preserve the lower bits as 0
  TTEntry* probe(const Key key, bool& found) const;
  int hashfull() const;
  void resize(size_t mbSize);
  void clear();

  TTEntry* first_entry(const Key key) const {
    return &table[mul_hi64(key, clusterCount)].entry[0];
  }

private:
  friend struct TTEntry;

  // `sizeof(generation8) == sizeof(TTEntry.genBound8)` must hold.
  uint8_t generation8 = 0; // We rely on the lowest GENERATION_NONBITS always being 0.

  size_t clusterCount;
  Cluster* table;
};

extern TranspositionTable TT;

} // namespace Stockfish

#endif // #ifndef TT_H_INCLUDED
