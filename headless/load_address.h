// SPDX-License-Identifier: GPL-3.0-or-later
// Where a game's code is loaded. Eden offsets it by a random number of pages at every start;
// a game given a seed (Library > Game settings > System > RNG seed) is offset by what the seed
// gives instead, the same at every start. Mods that tell an emulator from a console by the
// address the game is loaded at need that (they ask for seed 0, which is no offset at all).
//
// The seed is the port's own and not Eden's RNG seed setting: that one also seeds the random
// numbers a game is given (csrng, the process's entropy), and a game with it on would be dealt
// the same ones at every start. Eden's setting stays off; main.cpp sets this before each game,
// and the game loader reads it (headless/CMakeLists.txt, deconstructed_rom_directory.cpp).
#pragma once
#include <atomic>

namespace Eden {
// The seed of the game about to start, or -1: a random offset.
inline std::atomic<long long> code_seed{-1};
} // namespace Eden
