/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#include "evaluate.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

#include "nnue/network.h"
#include "nnue/nnue_misc.h"
#include "position.h"
#include "types.h"
#include "uci.h"
#include "nnue/nnue_accumulator.h"

namespace Stockfish {

unsigned king_mobility(const Position& pos, Color c = COLOR_NB);

inline int material_eval(const Position& pos) {
    Color us = pos.side_to_move();
    return PawnValue * (pos.count<PAWN>(us) - pos.count<PAWN>(~us))
          + pos.non_pawn_material(us) - pos.non_pawn_material(~us);
}

constexpr Value TwoKings = RookValue + BishopValue; // king = 4 points, i.e. the average of a rook and bishop
inline bool use_mating_eval(const Position& pos, int abs_material_ev) {
    constexpr float advantageKRK = float(RookValue - 1) / (TwoKings + RookValue); // R-1 to ensure the following float equality holds
    return float(abs_material_ev) / (TwoKings + pos.non_pawn_material()) >= advantageKRK;
}

static int mating_eval(const Position& pos, int material_ev) {
    assert(material_ev != 0);

    Color us = pos.side_to_move();
    Color loser = (material_ev < 0) ? us : ~us;

    int bonus = (8 - king_mobility(pos, loser)) * PawnValue / 2;
    return material_ev + (loser == us ? -bonus : +bonus);
}

// Evaluate is the evaluator for the outer world. It returns a static evaluation
// of the position from the point of view of the side to move.
Value Eval::evaluate(const Eval::NNUE::Network&     network,
                     const Position&                pos,
                     Eval::NNUE::AccumulatorStack&  accumulators,
                     Eval::NNUE::AccumulatorCaches& caches,
                     int                            optimism) {

    assert(!pos.checkers());

    int material_ev = material_eval(pos);
    if (use_mating_eval(pos, std::abs(material_ev)))
        return mating_eval(pos, material_ev);

    auto [psqt, positional] = network.evaluate(pos, accumulators, caches);

    Value nnue = (125 * psqt + 131 * positional) / 128;

    // Blend optimism and eval with nnue complexity
    int nnueComplexity = std::abs(psqt - positional);
    optimism += optimism * nnueComplexity / 476;
    nnue -= nnue * nnueComplexity / 18236;

    int material = 534 * pos.count<PAWN>() + pos.non_pawn_material();
    int v        = (nnue * (77871 + material) + optimism * (7191 + material)) / 77871;

    // Damp down the evaluation linearly when shuffling
    v -= v * pos.rule50_count() / 199;

    // Guarantee evaluation does not hit the tablebase range
    v = std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);

    return v;
}

// Like evaluate(), but instead of returning a value, it returns
// a string (suitable for outputting to stdout) that contains the detailed
// descriptions and values of each evaluation term. Useful for debugging.
// Trace scores are from white's point of view
std::string Eval::trace(Position& pos, const Eval::NNUE::Network& network) {

    if (pos.checkers())
        return "Final evaluation: none (in check)";

    auto accumulators = std::make_unique<Eval::NNUE::AccumulatorStack>();
    auto caches       = std::make_unique<Eval::NNUE::AccumulatorCaches>(network);

    std::stringstream ss;
    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    ss << '\n' << NNUE::trace(pos, network, *caches) << '\n';

    ss << std::showpoint << std::showpos << std::fixed << std::setprecision(2) << std::setw(15);

    auto [psqt, positional] = network.evaluate(pos, *accumulators, *caches);
    Value v                 = psqt + positional;
    ss << "NNUE evaluation          " << v << " (side to move, internal units)\n";
    v = pos.side_to_move() == WHITE ? v : -v;
    ss << "NNUE evaluation        " << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)\n";

    v = evaluate(network, pos, *accumulators, *caches, VALUE_ZERO);
    v = pos.side_to_move() == WHITE ? v : -v;

    ss << "Final evaluation      ";
    ss << 0.01 * UCIEngine::to_cp(v, pos) << " (white side)";
    ss << " [with scaled NNUE, ...]\n";

    return ss.str();
}

constexpr unsigned king_mob_count(const Position& pos, Color c, Square ksq, Bitboard candidates) {
    // Among the candidate squares, how many could our king teleport to?
    unsigned count = 0;
    while (candidates)
    {
        Square to = pop_lsb(candidates);
        // Can't capture friendlies, can't go into check
        count += (!(pos.pieces(c) & to) && !(pos.attackers_to_exist(to, pos.pieces() ^ ksq, ~c)));
    }
    return count;
}

// Doesn't count "in check" so may return 0
unsigned king_mobility(const Position& pos, Color c) {
    Square ksq = pos.square<KING>(c);
    Bitboard ring = Attacks::attacks_bb<KING>(ksq);
    return king_mob_count(pos, c, ksq, ring);
}

}  // namespace Stockfish
