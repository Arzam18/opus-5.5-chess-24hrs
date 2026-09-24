#pragma once
#include "types.h"
#include "bitboard.h"

const int MAX_STATES = 1024 + MAX_PLY + 16;

struct DirtyPiece {
    int n;
    int piece[3];
    int from[3]; // NO_SQ if added
    int to[3];   // NO_SQ if removed
};

struct StateInfo {
    U64 key;
    U64 pawnKey;
    int castle;
    int ep;
    int rule50;
    int pliesFromNull;
    int captured;
    U64 checkers;
    U64 pinned;
    Move move;
    DirtyPiece dirty;
    bool accComputed;
};

struct MoveList {
    Move moves[256];
    int scores[256];
    int n = 0;
    void add(Move m) { moves[n++] = m; }
};

enum GenType { GEN_CAPTURES, GEN_QUIETS, GEN_ALL };

namespace Zobrist {
extern U64 psq[12][64];
extern U64 castle[16];
extern U64 epFile[8];
extern U64 side;
void init();
}

class Position {
public:
    U64 byType[6];
    U64 byColor[2];
    int board[64];
    int stm;
    int sp; // index of current state
    int gamePly;
    StateInfo states[MAX_STATES];

    StateInfo &st() { return states[sp]; }
    const StateInfo &st() const { return states[sp]; }

    void set_fen(const std::string &fen);
    std::string fen() const;

    U64 occupied() const { return byColor[0] | byColor[1]; }
    U64 pieces(int c, int pt) const { return byColor[c] & byType[pt]; }
    U64 pieces(int pt) const { return byType[pt]; }
    int king_sq(int c) const { return lsb(pieces(c, KING)); }
    int piece_on(int s) const { return board[s]; }

    U64 attackers_to(int s, U64 occ) const;
    bool attacked_by(int s, int c, U64 occ) const { return attackers_to(s, occ) & byColor[c]; }
    bool in_check() const { return st().checkers != 0; }

    void make_move(Move m);
    void unmake_move(Move m);
    void make_null();
    void unmake_null();

    bool pseudo_legal(Move m) const;
    bool is_legal(Move m) const;
    bool is_capture(Move m) const { return (board[to_sq(m)] != NO_PIECE && move_type(m) != MT_CASTLE) || move_type(m) == MT_EP; }
    bool is_tactical(Move m) const { return is_capture(m) || (move_type(m) == MT_PROMO && promo_type(m) == QUEEN); }
    int captured_piece(Move m) const { return move_type(m) == MT_EP ? make_piece(!stm, PAWN) : board[to_sq(m)]; }

    bool see_ge(Move m, int threshold) const;
    bool is_draw(int ply) const;
    bool has_non_pawn(int c) const { return byColor[c] & ~byType[PAWN] & ~byType[KING]; }

    void generate(MoveList &list, GenType type) const;
    void generate_legal(MoveList &list);

    std::string move_to_uci(Move m) const;
    Move parse_uci(const std::string &s);

private:
    void put_piece(int p, int s);
    void remove_piece(int s);
    void move_piece(int from, int to);
    void compute_check_info();
    U64 compute_key() const;
    U64 compute_pawn_key() const;
};

extern const int CastleMask[64];
