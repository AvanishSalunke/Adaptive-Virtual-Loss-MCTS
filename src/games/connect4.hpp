// connect4.hpp - Connect-4 (6 rows x 7 columns) on two 64-bit "bitboards".
//
// Bit layout: every column gets 7 bits (6 cells + 1 always-empty "sentinel"
// bit on top). Cell (col, row) is bit  col * 7 + row,  row 0 = bottom:
//
//      row 5 |  5 12 19 26 33 40 47      (bit numbers)
//      ...   |  ...
//      row 0 |  0  7 14 21 28 35 42
//              c0 c1 c2 c3 c4 c5 c6      sentinel bits: 6, 13, 20, ...
//
// The sentinel bit stops a line of four from "wrapping" from the top of one
// column into the bottom of the next one.
//
// State = { cur_  : stones of the player to move,
//           mask_ : stones of both players }.
// The opponent's stones are therefore  cur_ ^ mask_.
#pragma once
#include <cstdint>
#include <string>

class Connect4 {
public:
    using Move = int;  // a move is a column number, 0..6
    static constexpr int kCols = 7;
    static constexpr int kRows = 6;
    static constexpr int kMaxMoves = kCols;

    // 0 = first player (X), 1 = second player (O).
    int current_player() const { return moves_ & 1; }
    int moves_played() const { return moves_; }

    // True if the player who made the last move has four in a row.
    bool won() const { return won_; }

    bool is_over() const { return won_ || moves_ == kRows * kCols; }

    // A column is playable while its top cell (row 5) is empty.
    int legal_moves(Move* out) const {
        int n = 0;
        for (int c = 0; c < kCols; ++c)
            if ((mask_ & top_bit(c)) == 0) out[n++] = c;
        return n;
    }

    void play(Move col) {
        cur_ ^= mask_;                  // cur_ becomes the opponent's stones (next to move)
        mask_ |= mask_ + bottom_bit(col);  // drop a stone: see the carry trick below
        ++moves_;
        // The player who just moved owns every stone that is not in cur_.
        won_ = has_four(cur_ ^ mask_);
    }

    // 1.0 win, 0.5 draw, 0.0 loss for `player`. Only valid when is_over().
    double result(int player) const {
        if (!won_) return 0.5;
        const int last_mover = 1 - current_player();
        return player == last_mover ? 1.0 : 0.0;
    }

    // Text picture of the board (X = first player, O = second), for debugging.
    std::string to_string() const {
        const uint64_t p0 = (current_player() == 0) ? cur_ : (cur_ ^ mask_);
        const uint64_t p1 = mask_ ^ p0;
        std::string s;
        for (int r = kRows - 1; r >= 0; --r) {
            for (int c = 0; c < kCols; ++c) {
                const uint64_t bit = 1ULL << (c * (kRows + 1) + r);
                s += (p0 & bit) ? 'X' : (p1 & bit) ? 'O' : '.';
                s += ' ';
            }
            s += '\n';
        }
        s += "0 1 2 3 4 5 6\n";
        return s;
    }

private:
    static constexpr uint64_t bottom_bit(int col) { return 1ULL << (col * (kRows + 1)); }
    static constexpr uint64_t top_bit(int col) { return 1ULL << (col * (kRows + 1) + kRows - 1); }

    // Carry trick used in play(): the stones in a column are packed from the
    // bottom, e.g. 0b0111. Adding the column's bottom bit makes the carry
    // ripple up through the stones (0b0111 + 1 = 0b1000) and lands exactly on
    // the lowest empty cell. OR-ing with the old mask keeps the old stones.

    // True if `p` contains four stones in a line. Shifting the board by k bits
    // and AND-ing with itself keeps only the stones whose neighbour at
    // distance k is also set; doing it twice finds four in a row:
    //   shift 1 = up, 7 = right, 8 = up-right diagonal, 6 = down-right diagonal.
    static bool has_four(uint64_t p) {
        uint64_t m = p & (p >> (kRows + 1));  // horizontal pairs
        if (m & (m >> (2 * (kRows + 1)))) return true;
        m = p & (p >> kRows);  // diagonal, going down-right
        if (m & (m >> (2 * kRows))) return true;
        m = p & (p >> (kRows + 2));  // diagonal, going up-right
        if (m & (m >> (2 * (kRows + 2)))) return true;
        m = p & (p >> 1);  // vertical pairs
        return (m & (m >> 2)) != 0;
    }

    uint64_t cur_ = 0;
    uint64_t mask_ = 0;
    int moves_ = 0;
    bool won_ = false;
};
