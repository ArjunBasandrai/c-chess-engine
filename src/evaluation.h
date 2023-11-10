#pragma once

#include <stdio.h>

#include "board_constants.h"
#include "bit_manipulation.h"
#include "pre_calculated_tables.h"
#include "movegen.h"
#include "board.h"
#include "masks.h"

#ifndef U64
#define U64 unsigned long long
#endif

extern const int double_pawn_penalty[2][8];
extern const int unsupported_pawn_penalty[2];
extern const int isolated_pawn_penalty[2][8];
extern const int connected_pawn_bonus[2][64];
extern const int backward_pawn_penalty[2][8];
extern const int pawn_defects[2][8];
extern const int passed_pawn[8];
extern const int passed_pawn_base[2];
extern const int passed_pawn_free_advance;
extern const int passed_pawn_partial_advance;
extern const int passed_pawn_defended;
extern const int passed_pawn_partial_defended;
extern const int candidate_passed_pawn_bonus[2][8];

extern const int rook_semi_open_file_score;
extern const int rook_open_file_score[2];

extern const int king_open_file_penalty[8];
extern const int king_semi_open_file_penalty[8];

extern const int bishop_outpost[2][64];
extern const int bishop_pawns_on_color[2];
extern const int bishop_pair[2];

static const int bishop_unit = 4;
static const int queen_unit = 9;

static const int bishop_mobility[2] = {5, 6};
static const int queen_mobility[2] = {1, 2};

extern const int king_shield_bonus;

static inline int get_game_phase_score(s_board *pos) {
    int white_piece_scores=0, black_piece_scores=0;

    for (int piece = N; piece <= Q; piece++) {
        white_piece_scores += count_bits(pos->bitboards[piece]) * material_score[opening][piece];
    }

    for (int piece = n; piece <= q; piece++) {
        black_piece_scores += count_bits(pos->bitboards[piece]) * -material_score[opening][piece];
    }

    return white_piece_scores + black_piece_scores;
}

static inline int evaluate(s_board *pos) {

    int game_phase_score = get_game_phase_score(pos);
    int game_phase = -1;

    if (game_phase_score > opening_phase_score) game_phase = opening;
    else if (game_phase_score < endgame_phase_score) game_phase = endgame;
    else game_phase = middlegame;

    int score = 0, score_opening = 0, score_endgame = 0;
    int w_passer[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int b_passer[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    U64 bitboard;
    U64 pawn_files[2];
    int piece, square;

    int black_bishops = count_bits(pos->bitboards[b]);
    int white_bishops = count_bits(pos->bitboards[B]);

    for (int bb_piece = P; bb_piece <= k; bb_piece++) {
        bitboard = pos->bitboards[bb_piece];

        while (bitboard) {
            int doubled = 0, isolated = 0, passed = 0, connected = 0, unsupported = 0, most_adv = 0;
            U64 backward = 0;
            U64 attackers, defenders;
            int  outpost, b_same_color_pawns;

            piece = bb_piece;
            square = get_lsb_index(bitboard);

            score_opening += material_score[opening][piece];
            score_endgame += material_score[endgame][piece];
            switch (piece) {
                case P: 
                    pawn_files[white] |= get_file(square);

                    // positional score
                    score_opening += positional_score[opening][PAWN][square];
                    score_endgame += positional_score[endgame][PAWN][square];
                    
                    // double pawn penalty
                    doubled = pos->bitboards[P] & file_ahead_mask[square];
                    if (doubled) {
                        score_opening -= double_pawn_penalty[opening][get_file(square)] / rank_distance(square, most_advanced(doubled, white));
                        score_endgame -= double_pawn_penalty[endgame][get_file(square)] / rank_distance(square, most_advanced(doubled, white));
                    }
                    
                    // isolated pawn penalty
                    isolated = !(pos->bitboards[P] & isolated_mask[square]);
                    if (isolated) {
                        score_opening -= isolated_pawn_penalty[opening][get_file(square)];
                        score_endgame -= isolated_pawn_penalty[endgame][get_file(square)];
                    }

                    // unsupported pawn penalty
                    unsupported = !(pos->bitboards[P] & mask_pawn_attacks(square, black));
                    if (unsupported && !isolated) {
                        score_opening -= unsupported_pawn_penalty[opening];
                        score_endgame -= unsupported_pawn_penalty[endgame];
                    }

                    // passed pawn bonus
                    most_adv = most_advanced(pos->bitboards[P] & file_mask[square], white);
                    passed = !(white_passed_mask[square] & pos->bitboards[p]);
                    if (most_adv && !(w_passer[get_file(square)]) && passed) {
                        int score = 0, next_sq = 0, bonus = 0, rank = get_rank[square];
                        U64 behind, fwd, bwd, attacked, defended;
                        U64 fsliders = pos->bitboards[Q] | pos->bitboards[R];
                        U64 esliders = pos->bitboards[q] | pos->bitboards[r];

                        w_passer[get_file(square)] = 1;
                        score = passed_pawn[rank];

                        if (score) {
                            score_opening += score * passed_pawn_base[opening];
                            score_endgame += score * passed_pawn_base[endgame];
                            next_sq = square - 8;
                            /*
                                For endgames, adding a bonus based on how close 
                                white king is to this pawn and a penalty based on how     
                                close the black king is. 
                            */
                            int fk = get_lsb_index(pos->bitboards[K]), ek = get_lsb_index(pos->bitboards[k]);
                            score_endgame += distance(ek, next_sq) * 2 * score - distance(fk, next_sq) * score;
                            /*
                                Keeping the white king ahead of the pawn, so it can escort it to promotion square
                            */
                            if (rank < 6) {
                                score_endgame -= distance(fk, next_sq - 8) * score / 2;
                            }

                            if (!(pos->occupancies[both] & (1ULL << next_sq))) {
                                bonus = 0;
                                if (pos->bitboards[P] & mask_pawn_attacks(next_sq, black)) {
                                    bonus = passed_pawn_free_advance;
                                } else {
                                    attacked = 0ULL;
                                    fwd = file_ahead_mask[square];
                                    bwd = file_behind_mask[square];
                                    if ((behind = bwd & esliders) && 
                                        (rook_attacks_on_the_fly(square, pos->occupancies[both]) & file_mask[square] & behind)) {
                                        attacked = fwd;
                                    } else {
                                        U64 bb = fwd;
                                        while (bb) {
                                            int sq = get_lsb_index(bb);
                                            if (is_square_attacked(sq, black, pos)) {
                                                attacked |= (1ULL << sq);
                                            }
                                            pop_bit(bb, sq);
                                        }
                                    }

                                    if (!attacked) {
                                        bonus = passed_pawn_free_advance;
                                    } else if (!(attacked & (1ULL << next_sq))) {
                                        bonus = passed_pawn_partial_advance;
                                    }

                                    defended = 0ULL;
                                    if ((behind = bwd & fsliders) && 
                                        (rook_attacks_on_the_fly(square, pos->occupancies[both]) & file_mask[square] & behind)) {
                                        defended = fwd;
                                    } else {
                                        U64 bb = fwd;
                                        while (bb) {
                                            int sq = get_lsb_index(bb);
                                            if (is_square_attacked(sq, white, pos)) {
                                                defended |= (1ULL << sq);
                                            }
                                            pop_bit(bb, sq);
                                        }
                                    }
                                    if (defended == fwd) {
                                        bonus += passed_pawn_defended;
                                    } else if (defended & (1ULL << next_sq)) {
                                        bonus += passed_pawn_partial_defended;
                                    }
                                }
                                score_opening += bonus * score;
                                score_endgame += bonus * score;
                            }
                        } else {
                            score_endgame += 4;
                        }
                    }

                    // connected pawn bonus
                    connected = (pos->bitboards[P] & connected_mask[white][square]);
                    if (connected) {
                        score_opening += connected_pawn_bonus[opening][square];
                        score_endgame += connected_pawn_bonus[endgame][square];
                    }

                    // backward pawn penalty
                    backward = 0ULL;
                    if (!(isolated | passed | connected | 
                        (pos->bitboards[p] & mask_pawn_attacks(square, white)))) {
                        backward = (pos->bitboards[p] & mask_pawn_attacks(square - 8, white));
                    }
                    if (backward) {
                        score_opening -= backward_pawn_penalty[opening][get_file(square)];
                        score_endgame -= backward_pawn_penalty[endgame][get_file(square)];
                    }

                    // Candidate Passed Pawn Bonus
                    if (!(passed | backward | isolated) &&
                        !(pos->bitboards[p] & file_ahead_mask[square])) {
                        defenders = pos->bitboards[P] & mask_pawn_attacks(square - 8, white);
                        attackers = pos->bitboards[p] & mask_pawn_attacks(square, black);
                        if (count_bits(defenders) >= count_bits(attackers)) {
                            score_opening += candidate_passed_pawn_bonus[opening][get_rank[square]];
                            score_endgame += candidate_passed_pawn_bonus[endgame][get_rank[square]];
                        }
                    }

                    break;

                case N:
                    // positional score
                    score_opening += positional_score[opening][KNIGHT][square];
                    score_endgame += positional_score[endgame][KNIGHT][square]; 

                    break;
                case B: 
                    // positional score
                    score_opening += positional_score[opening][BISHOP][square];
                    score_endgame += positional_score[endgame][BISHOP][square]; 

                    // outpost evaluation
                    outpost = bishop_outpost[white][square];
                    if (outpost) {
                        if (!(pos->bitboards[p] & mask_pawn_attacks(square, white))) {
                            if (pos->bitboards[P] & mask_pawn_attacks(square, black)) {
                                outpost += outpost / 2;
                                if (!(pos->bitboards[n]) && !(color(square) & pos->bitboards[b])) {
                                    outpost += bishop_outpost[white][square];
                                }
                            } 
                            score_opening += outpost;
                            score_endgame += outpost;
                        }
                    }


                    // same color pawn penalty
                    if (white_bishops == 1) {
                        if (light_squares & (1ULL << square)) {
                            b_same_color_pawns = count_bits(pos->bitboards[P] & light_squares);
                        } else {
                            b_same_color_pawns = count_bits(pos->bitboards[P] & ~light_squares);
                        }
                        score_opening -= b_same_color_pawns * bishop_pawns_on_color[opening];
                        score_endgame -= b_same_color_pawns * bishop_pawns_on_color[endgame];
                    }

                    // mobility score
                    score_opening += (count_bits(get_bishop_attacks(square, pos->occupancies[both])) - bishop_unit) * bishop_mobility[opening];
                    score_endgame += (count_bits(get_bishop_attacks(square, pos->occupancies[both])) - bishop_unit) * bishop_mobility[endgame]; 

                    break;
                case R: 
                    // positional score
                    score_opening += positional_score[opening][ROOK][square];
                    score_endgame += positional_score[endgame][ROOK][square]; 

                    if (!(pos->bitboards[P] & file_mask[square])) {
                        // open file bonus
                        if (!((pos->bitboards[p]) & file_mask[square])) {
                            score_opening += rook_open_file_score[opening];
                            score_endgame += rook_open_file_score[endgame];
                        } 
                        // add semi open file bonus
                        else {
                            score_opening += rook_semi_open_file_score;
                            score_endgame += rook_semi_open_file_score;
                        }
                    }

                    break;
                
                case Q:
                    // positional score
                    score_opening += positional_score[opening][QUEEN][square];
                    score_endgame += positional_score[endgame][QUEEN][square]; 

                    // mobility score
                    score_opening += (count_bits(get_queen_attacks(square, pos->occupancies[both])) - queen_unit) * queen_mobility[opening];
                    score_endgame += (count_bits(get_queen_attacks(square, pos->occupancies[both])) - queen_unit) * queen_mobility[endgame]; 

                    break;

                case K: 
                    // positional score
                    score_opening += positional_score[opening][KING][square];
                    score_endgame += positional_score[endgame][KING][square]; 
                    if (!((pos->bitboards[P] | pos->bitboards[p]) & file_mask[square])) {
                        score_opening -= king_open_file_penalty[get_file(square)];
                        score_endgame -= king_open_file_penalty[get_file(square)];
                    } else {
                        if (!(pos->bitboards[p] & file_mask[square])) {
                            score_opening -= king_semi_open_file_penalty[get_file(square)] / 2;
                            score_endgame -= king_semi_open_file_penalty[get_file(square)] / 2;
                        } else if (pos->bitboards[p] & get_file(square)){
                            score_opening -= pawn_defects[white][get_rank[get_msb_index(pos->bitboards[p] & get_file(square))]];
                            score_endgame -= pawn_defects[white][get_rank[get_msb_index(pos->bitboards[p] & get_file(square))]];
                        }
                    }

                    // king safety bonus
                    score_opening += count_bits(king_attacks[square] & pos->occupancies[black]) * king_shield_bonus;
                    score_endgame += count_bits(king_attacks[square] & pos->occupancies[black]) * king_shield_bonus;

                    break;

                case p: 
                    pawn_files[black] |= get_file(square);
                
                    // positional score
                    score_opening -= positional_score[opening][PAWN][mirror_score[square]];
                    score_endgame -= positional_score[endgame][PAWN][mirror_score[square]]; 

                    // double pawn penalty
                    doubled = pos->bitboards[p] & file_behind_mask[square];
                    if (doubled) {
                        score_opening += double_pawn_penalty[opening][get_file(square)] / rank_distance(square, most_advanced(doubled, black));
                        score_endgame += double_pawn_penalty[endgame][get_file(square)] / rank_distance(square, most_advanced(doubled, black));
                    }

                    // isolated pawn penalty
                    isolated = !(pos->bitboards[p] & isolated_mask[square]);
                    if (isolated) {
                        score_opening += isolated_pawn_penalty[opening][get_file(mirror_score[square])];
                        score_endgame += isolated_pawn_penalty[endgame][get_file(mirror_score[square])];
                    }

                    // unsupported pawn penalty
                    unsupported = !(pos->bitboards[p] & mask_pawn_attacks(square, white));
                    if (unsupported && !isolated) {
                        score_opening += unsupported_pawn_penalty[opening];
                        score_endgame += unsupported_pawn_penalty[endgame];
                    }
                    
                    // passed pawn bonus
                    most_adv = most_advanced(pos->bitboards[p] & file_mask[square], black);
                    passed = !(black_passed_mask[square] & pos->bitboards[P]);
                    if (most_adv && !(b_passer[get_file(square)]) && passed) {
                        int score = 0, next_sq = 0, bonus = 0, rank = get_rank[mirror_score[square]];
                        U64 behind, fwd, bwd, attacked, defended;
                        U64 fsliders = pos->bitboards[q] | pos->bitboards[r];
                        U64 esliders = pos->bitboards[Q] | pos->bitboards[R];

                        b_passer[get_file(square)] = 1;
                        score = passed_pawn[rank];

                        if (score) {
                            score_opening -= score * passed_pawn_base[opening];
                            score_endgame -= score * passed_pawn_base[endgame];
                            next_sq = square + 8;
                            /*
                                For endgames, adding a bonus based on how close 
                                white king is to this pawn and a penalty based on how     
                                close the black king is. 
                            */
                            int fk = get_lsb_index(pos->bitboards[k]), ek = get_lsb_index(pos->bitboards[K]);
                            score_endgame -= distance(ek, next_sq) * 2 * score - distance(fk, next_sq) * score;
                            /*
                                Keeping the white king ahead of the pawn, so it can escort it to promotion square
                            */
                            if (rank < 6) {
                                score_endgame += distance(fk, next_sq + 8) * score / 2;
                            }

                            if (!(pos->occupancies[both] & (1ULL << next_sq))) {
                                bonus = 0;
                                if (pos->bitboards[p] & mask_pawn_attacks(next_sq, white)) {
                                    bonus = passed_pawn_free_advance;
                                } else {
                                    attacked = 0ULL;
                                    fwd = file_behind_mask[square];
                                    bwd = file_ahead_mask[square];
                                    if ((behind = bwd & esliders) && 
                                        (rook_attacks_on_the_fly(square, pos->occupancies[both]) & file_mask[square] & behind)) {
                                        attacked = fwd;
                                    } else {
                                        U64 bb = fwd;
                                        while (bb) {
                                            int sq = get_lsb_index(bb);
                                            if (is_square_attacked(sq, white, pos)) {
                                                attacked |= (1ULL << sq);
                                            }
                                            pop_bit(bb, sq);
                                        }
                                    }

                                    if (!attacked) {
                                        bonus = passed_pawn_free_advance;
                                    } else if (!(attacked & (1ULL << next_sq))) {
                                        bonus = passed_pawn_partial_advance;
                                    }

                                    defended = 0ULL;
                                    if ((behind = bwd & fsliders) && 
                                        (rook_attacks_on_the_fly(square, pos->occupancies[both]) & file_mask[square] & behind)) {
                                        defended = fwd;
                                    } else {
                                        U64 bb = fwd;
                                        while (bb) {
                                            int sq = get_lsb_index(bb);
                                            if (is_square_attacked(sq, black, pos)) {
                                                defended |= (1ULL << sq);
                                            }
                                            pop_bit(bb, sq);
                                        }
                                    }
                                    if (defended == fwd) {
                                        bonus += passed_pawn_defended;
                                    } else if (defended & (1ULL << next_sq)) {
                                        bonus += passed_pawn_partial_defended;
                                    }
                                }
                                score_opening -= bonus * score;
                                score_endgame -= bonus * score;
                            }
                        } else {
                            score_endgame -= 4;
                        }
                    }

                    // connected pawn bonus
                    connected = (pos->bitboards[p] & connected_mask[black][square]);
                    if (connected) {
                        score_opening -= connected_pawn_bonus[opening][mirror_score[square]];
                        score_endgame -= connected_pawn_bonus[endgame][mirror_score[square]];
                    }

                    // backward pawn penalty
                    backward = 0ULL;
                    if (!(isolated | passed | connected | 
                        (pos->bitboards[P] & mask_pawn_attacks(square, black)))) {
                        backward = (pos->bitboards[P] & mask_pawn_attacks(square + 8, black));
                    }
                    if (backward) {
                        score_opening += backward_pawn_penalty[opening][get_file(square)];
                        score_endgame += backward_pawn_penalty[endgame][get_file(square)];
                    }

                    // Candidate Passed Pawn Bonus
                    if (!(passed | backward | isolated) &&
                        !(pos->bitboards[P] & file_behind_mask[square])) {
                        defenders = pos->bitboards[p] & mask_pawn_attacks(square + 8, black);
                        attackers = pos->bitboards[P] & mask_pawn_attacks(square, white);
                        if (count_bits(defenders) >= count_bits(attackers)) {
                            score_opening -= candidate_passed_pawn_bonus[opening][get_rank[mirror_score[square]]];
                            score_endgame -= candidate_passed_pawn_bonus[endgame][get_rank[mirror_score[square]]];
                        }
                    }

                    break;
                
                case n: 
                    // positional score
                    score_opening -= positional_score[opening][KNIGHT][mirror_score[square]];
                    score_endgame -= positional_score[endgame][KNIGHT][mirror_score[square]];

                    break;
                
                case b: 
                    // positional score
                    score_opening -= positional_score[opening][BISHOP][mirror_score[square]];
                    score_endgame -= positional_score[endgame][BISHOP][mirror_score[square]];

                    // outpost evaluation
                    outpost = bishop_outpost[black][square];
                    if (outpost) {
                        if (!(pos->bitboards[P] & mask_pawn_attacks(square, black))) {
                            if (pos->bitboards[p] & mask_pawn_attacks(square, white)) {
                                outpost += outpost / 2;
                                if (!(pos->bitboards[N]) && !(color(square) & pos->bitboards[P])) {
                                    outpost += bishop_outpost[black][square];
                                }
                            } 
                            score_opening -= outpost;
                            score_endgame -= outpost;
                        }
                    }

                    // Same color pawn penalty
                    if (black_bishops == 1) {
                        if (light_squares & (1ULL << square)) {
                            b_same_color_pawns = count_bits(pos->bitboards[p] & light_squares);
                        } else {
                            b_same_color_pawns = count_bits(pos->bitboards[p] & ~light_squares);
                        }
                        score_opening += b_same_color_pawns * bishop_pawns_on_color[opening];
                        score_endgame += b_same_color_pawns * bishop_pawns_on_color[endgame];
                    }

                    // mobility score
                    score_opening -= (count_bits(get_bishop_attacks(square, pos->occupancies[both])) - bishop_unit) * bishop_mobility[opening];
                    score_endgame -= (count_bits(get_bishop_attacks(square, pos->occupancies[both])) - bishop_unit) * bishop_mobility[endgame];

                    break;
                case r: 
                    // positional score
                    score_opening -= positional_score[opening][ROOK][mirror_score[square]];
                    score_endgame -= positional_score[endgame][ROOK][mirror_score[square]];

                    if (!(pos->bitboards[p] & file_mask[square])) {
                        // open file bonus
                        if (!((pos->bitboards[P]) & file_mask[square])) {
                            score_opening -= rook_open_file_score[opening];
                            score_endgame -= rook_open_file_score[endgame];
                        } 
                        // add semi open file bonus
                        else {
                            score_opening -= rook_semi_open_file_score;
                            score_endgame -= rook_semi_open_file_score;
                        }
                    }

                    break;
                
                case q:
                    // positional score
                    score_opening -= positional_score[opening][QUEEN][mirror_score[square]];
                    score_endgame -= positional_score[endgame][QUEEN][mirror_score[square]];

                    // mobility score
                    score_opening -= (count_bits(get_queen_attacks(square, pos->occupancies[both])) - queen_unit) * queen_mobility[opening];
                    score_endgame -= (count_bits(get_queen_attacks(square, pos->occupancies[both])) - queen_unit) * queen_mobility[endgame]; 

                    break;

                case k: 
                    // positional score
                    score_opening -= positional_score[opening][KING][mirror_score[square]];
                    score_endgame -= positional_score[endgame][KING][mirror_score[square]];

                    if (!((pos->bitboards[P] | pos->bitboards[p]) & file_mask[square])) {
                        score_opening += king_open_file_penalty[get_file(square)];
                        score_endgame += king_open_file_penalty[get_file(square)];
                    } else {
                        if (!(pos->bitboards[P] & file_mask[square])) {
                            score_opening += king_semi_open_file_penalty[get_file(square)] / 2;
                            score_endgame += king_semi_open_file_penalty[get_file(square)] / 2;
                        } else if (pos->bitboards[P] & get_file(square)) {
                            score_opening += pawn_defects[black][get_rank[get_lsb_index(pos->bitboards[P] & get_file(square))]];
                            score_endgame += pawn_defects[black][get_rank[get_lsb_index(pos->bitboards[P] & get_file(square))]];
                        }
                    }

                    // king safety bonus
                    score_opening -= count_bits(king_attacks[square] & pos->occupancies[black]) * king_shield_bonus;
                    score_endgame -= count_bits(king_attacks[square] & pos->occupancies[black]) * king_shield_bonus;
                    
                    break;
            }
        
            pop_bit(bitboard, square);
        }
    }

    // bishop pair bonus
    if (black_bishops > 1) {
        score_opening -= bishop_pair[opening];
        score_endgame -= bishop_pair[endgame];
    }
    if (white_bishops > 1) {
        score_opening += bishop_pair[opening];
        score_endgame += bishop_pair[endgame];
    }

    score_endgame += 8 * distance(get_msb_index(pawn_files[white]), get_lsb_index(pawn_files[white]));
    score_endgame -= 8 * distance(get_msb_index(pawn_files[black]), get_lsb_index(pawn_files[black]));

    if (game_phase == middlegame) {
        score = (
            score_opening * game_phase_score + 
            score_endgame * (opening_phase_score - game_phase_score)
        ) / opening_phase_score;
    } else if (game_phase == opening) {
        score = score_opening;
    } else if (game_phase == endgame) {
        score = score_endgame;
    }

    score = score * (100 - pos->fifty) / 100;

    return (pos->side == white) ? score : -score;
}