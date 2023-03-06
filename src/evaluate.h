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

#ifndef EVALUATE_H_INCLUDED
#define EVALUATE_H_INCLUDED

#include <string>
#include <optional>
#include <utility>

#include "types.h"

namespace Stockfish {

class Position;

namespace Eval {

  // This is a helper function, shouldn't be used outside the namespace.
  std::pair<Value, int> cook_nnue(const Position& pos); // returns {nnCooked, nnComplexity}
  
  // This is the evaluator for the oustide world. It returns a static
  // evaluation of the position from the point of view of the side to move.
  std::pair<Value, int> evaluate(const Position& pos); // returns {value, complexity}
  
  // Used to generate ASCII art depictions of the static eval (e.g. for debug)
  std::string trace(Position& pos);

  extern bool useNNUE;
  extern std::string currentEvalFileName;

  // The default net name MUST follow the format nn-[SHA256 first 12 digits].nnue
  // for the build process (profile-build and fishtest) to work. Do not change the
  // name of the macro, as it is used in the Makefile.
  #define EvalFileDefaultName   "nn-52471d67216a.nnue"

  namespace NNUE {

    std::string trace(Position& pos);
    std::pair<Value, Value> evaluate(const Position& pos); // returns {positional, psq}
    void hint_common_parent_position(const Position& pos);

    void init();
    void verify();

    bool load_eval(std::string name, std::istream& stream);
    bool save_eval(std::ostream& stream);
    bool save_eval(const std::optional<std::string>& filename);

  } // namespace NNUE

} // namespace Eval

} // namespace Stockfish

#endif // #ifndef EVALUATE_H_INCLUDED
