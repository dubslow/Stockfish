/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#include <cstddef>
#include <cstdint>

#include "memory.h"
#include "misc.h"
#include "types.h"

namespace Stockfish {

class ThreadPool;
struct TTEntry;
struct Cluster;

// The Stockfish TranspositionTable is a classic hash table, and some care is taken under the hood to support large hash
// sizes and efficient storage packing. There is only one global hash table for the engine and all its threads.
// For chess in particular, we even allow racy updates between threads to and from the TT, as taking the time to
// synchronize access would cost thinking time and thus elo. As a hash table, collisions are possible and may cause
// chess playing issues (bizarre blunders, faulty mate reports, etc). However the risk of such decreases with size.
//
// The public interface of the TT is minimal.
//
// The maintenance methods are `resize`, `clear` and `hashfull`. The first two do what they say, while the latter
// reports roughly how many times we've stored new search results during the current search.
//
// The functional methods are `new_search`, `generation`, `probe`, and `first_entry`. Any time we start a fresh search,
// alert the TT with `new_search`, and any time we store data, the user must pass in the current `generation()`.
// `first_entry` is only needed to prefetch entries from memory, and should otherwise be considered an implementation
// detail.
// `probe` is the primary method: given a board position, we lookup its entry in the table, and return a tuple of:
//   1) whether or not we found existing data in the entry
//   2) a copy of the data (if any) already stored in the entry
//   3) the means by which to write new data into this entry (if desirable).
//
//  The reason to split 2) and 3) into separate objects is to maintain clear separation between local, threadsafe
//  datastructures and global, racy datastructures. 2) is the former, 3) is the latter, and ought not be confused.
//
// These are the `probe` return types:
//
// A copy of the data already in the entry. It is read from the entry together, just once. In principle, this read
// should be considered racy, but in practice it's plenty fast enough to avoid problems. After the copy is made, the
// result can be freely used by the using thread without any further worry of races.
struct TTData {
    Move  move;
    Value value, eval;
    Depth depth;
    Bound bound;
    bool  is_pv;
};
//
// This is to be considered the racy object, used to make racy writes to the global TT.
struct TTWriter {
   public:
    void write(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t generation8);
   private:
    friend class TranspositionTable;
    TTEntry* entry; // This is no concern of the user
    TTWriter(TTEntry* tte);
};


class TranspositionTable {

   public:
    ~TranspositionTable() { aligned_large_pages_free(table); }

    void resize(size_t mbSize, ThreadPool& threads); // Threads must be ready before resizing the table.
    void clear(ThreadPool& threads); // Delete all present data, prepare for a new game.
    int  hashfull() const; // Roughly the count of writes-to-TT per TT-size in the current search, permille per UCI spec.

    void new_search(); // The user must call this for each new search in the current game.
    uint8_t generation() const; // This return value is what gets passed to the TTWriter.
    std::tuple<bool, TTData, TTWriter> probe(const Key key) const;
    TTEntry* first_entry(const Key key) const; // Only to be used for prefetching from memory

   private:
    friend struct TTEntry;

    size_t   clusterCount;
    Cluster* table       = nullptr;

    uint8_t  generation8 = 0;  // Size must be not bigger than TTEntry::genBound8
};

}  // namespace Stockfish

#endif  // #ifndef TT_H_INCLUDED
