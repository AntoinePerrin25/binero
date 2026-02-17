# Binero/Takuzu Solver

Binero, also known as Takuzu, is a logic puzzle game where the objective is to fill a grid with 0s and 1s while following specific rules. The rules are as follows:
1. Each row and each column must contain an equal number of 0s and 1s.
2. No more than two of the same number can be adjacent to each other in any row or column.
3. Each row and each column must be unique.

This is my implementation of a Binero/Takuzu TUI and solver in C made in approximately 10 hours.

## The TUI

The TUI (Text User Interface) allows users to interact with the game through the terminal. It provides a visual representation of the grid and allows users to input their moves. The TUI is designed to be user-friendly and intuitive, making it easy for players to enjoy the game.
It enables Raw mode, which allows for real-time input without the need for pressing Enter. The interface displays the grid and provides instructions for navigation and actions.
Arrows: nav|'a'/'e'->'0'/'1'|'r'emove | 'c'ommit | 'x'port | 'q'uit

## The Solver

### Evident Solver

The evident solver is a simple algorithm that applies the rules of the game to fill in cells that can be determined with certainty.

#### AdjacentPairRule

TwoEqualsThree: 00_ -> 001, _00 -> 100
FillTheHole:    0_0 -> 010, 1_1 -> 101

#### QuotaExhaustedRule

If a row or column has already reached the maximum number of 0s or 1s, the remaining cells must be filled with the opposite number.

### Backtracking Solver

One of the easiest backtracking to be implemented here, only two possibilities.
Clone the board, fill the first empty cell with 0, if it leads to a solution, return it, else use EvidentSolver to fill the board as much as possible, if it leads to a solution, return it, else use recursion to fill next empty cell with 0. If the 0 path does not lead to a solution, fill the first empty cell with 1 and repeat the process.
