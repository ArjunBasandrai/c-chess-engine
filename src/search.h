#pragma once

#include "bit_manipulation.h"
#include "board.h"
#include "moves_list.h"
#include "movegen.h"
#include "evaluation.h"
#include "transposition_table.h"
#include "perft.h"
#include "time.h"

#define max_ply 64

int killer_moves[2][max_ply];
int history_moves[12][max_ply];

int pv_length[max_ply];
int pv_table[max_ply][max_ply];

int follow_pv, score_pv;

extern int quit;
extern int movestogo;
extern int movetime;
extern int time;
extern int inc;
extern int starttime;
extern int stoptime;
extern int timeset;
extern int stopped;

int input_waiting();
void read_input();
static void communicate();


// a bridge function to interact between search and GUI input
static void communicate() {
	// if time is up break here
    if (timeset == 1 && get_time_ms() > stoptime) {
		// tell engine to stop calculating
		stopped = 1;
	}
	
    // read GUI input
	read_input();
}


static inline void enable_pv_scoring(moves *move_list) {
    follow_pv = 0;
    for (int count = 0; count < move_list->count; count++) {
        if (pv_table[0][ply] == move_list->moves[count]) {
            score_pv = 1;
            follow_pv = 1;
        }
    }
}

static inline int score_move(int move) {

    if (score_pv) {
        if (pv_table[0][ply] == move) {
            score_pv = 0;
            
            return 20000;
        }
    }

    if (get_move_capture(move)){
        /* init target_piece to P because in case of enpassant, there will be no piece at target_square*/
        int target_piece = P;

        int start_piece, end_piece;
        if (side == white) { start_piece = p; end_piece = k;} 
        
        else { start_piece = P; end_piece = K; }

        for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++) {
            if (get_bit(bitboards[bb_piece], get_move_target(move))) {
                target_piece = bb_piece;
                break;
            }
        }

        return mvv_lva[get_move_piece(move)][target_piece] + 10000; // + 10000 is added so that captures rank higher than killer moves
    } 
    
    else {
        // score 1st killer move

        if (killer_moves[0][ply] == move) {
            return 9000;
        }

        // score 2nd killer move

        else if (killer_moves[1][ply] == move) {
            return 8000;
        }

        // score history move

        else {
            return history_moves[get_move_piece(move)][get_move_target(move)];
        }
    }

    return 0;
}

static inline int sort_moves(moves *move_list) {
    int move_scores[move_list->count];
    for (int count = 0; count < move_list->count; count++) {
        move_scores[count] = score_move(move_list->moves[count]);
    }

    for (int current_move = 0; current_move < move_list->count; current_move++) {
        for (int next_move = current_move + 1; next_move < move_list->count; next_move++) {
            if (move_scores[current_move] < move_scores[next_move]) {
                int temp_score = move_scores[current_move];
                move_scores[current_move] = move_scores[next_move];
                move_scores[next_move] = temp_score;

                int temp_move = move_list->moves[current_move];
                move_list->moves[current_move] = move_list->moves[next_move];
                move_list->moves[next_move] = temp_move;
            }
        }
    }
}
void print_move_scores(moves *move_list);

static inline int is_repetition() {

    for (int index = 0; index < repetition_index; index++) {
        if (repetitions_table[index] == hash_key) {
            return 1;
        }
    }

    return 0;
}

static inline int quiescence(int alpha, int beta) {

    if((nodes & 2047) == 0)
        // "listen" to the GUI/user input
		communicate();

    nodes++;

    if (ply > max_ply - 1) {
        return evaluate();
    }

    int evaluation = evaluate();

    // fail-hard beta cutoff
    if (evaluation >= beta) {
        // node fails high
        return beta;
    }

    // found a better move
    if (evaluation > alpha) {
        alpha = evaluation;
    }

    moves move_list[1];
    generate_moves(move_list);
    sort_moves(move_list);

    for (int count = 0; count < move_list->count; count++) {
        copy_board();
        ply++;

        repetition_index++;
        repetitions_table[repetition_index] = hash_key;

        if (make_move(move_list->moves[count], only_captures) == 0) {
            ply--;
            repetition_index--;
            continue;
        }

        int score = -quiescence(-beta, -alpha);
        ply--;
        repetition_index--;
        take_back();

        if(stopped == 1) return 0;

        // found a better move
        if (score > alpha) {
            alpha = score;

            // fail-hard beta cutoff
            if (score >= beta) {
                // node fails high
                return beta;
            }
        }
    }

    return alpha;
}

static inline int negamax(int alpha, int beta, int depth) {

    int score;
    
    int hash_flag = hash_flag_alpha;

    if (ply && is_repetition()) {
        return draw_score;
    }

    int pv_node = (beta - alpha > 1);

    // read hash from transposition table if not root ply and not a pv node
    if (ply && (score = read_hash_entry(alpha, beta, depth)) != no_hash_entry && !pv_node) {
        // if move has already been searched and henc
        return score;
    }

    if((nodes & 2047 ) == 0)
        // "listen" to the GUI/user input
		communicate();
    
    pv_length[ply] = ply;

    if (depth == 0) {
        return quiescence(alpha, beta);
    }

    if (ply > max_ply - 1) return evaluate();

    nodes++;

    int in_check = is_square_attacked((side == white) ? get_lsb_index(bitboards[K]) : get_lsb_index(bitboards[k]),side ^ 1);

    if (in_check) depth++;

    int legal_moves=0;

    // Null Move Pruning
    if (depth >= 3 && !in_check && ply) {
        copy_board();

        ply++;

        repetition_index++;
        repetitions_table[repetition_index] = hash_key;

        if (enpassant != no_sq) hash_key ^= enpassant_keys[enpassant];

        enpassant = no_sq;
        side ^= 1;

        hash_key ^= side_key;

        score = -negamax(-beta, -beta + 1, depth - 1 - 2);

        ply--;
        repetition_index--;

        take_back();

        if (score >= beta) {
            return beta;
        }
    }

    moves move_list[1];
    generate_moves(move_list);

    if (follow_pv) enable_pv_scoring(move_list);

    sort_moves(move_list);

    int moves_searched = 0;

    for (int count = 0; count < move_list->count; count++) {
        copy_board();
        ply++;

        repetition_index++;
        repetitions_table[repetition_index] = hash_key;

        if (make_move(move_list->moves[count], all_moves) == 0) {
            ply--;
            repetition_index--;
            continue;
        }

        legal_moves++;

        if (moves_searched == 0) {
            score = -negamax(-beta, -alpha, depth - 1);
        }
        
        else {
            if (
                moves_searched >= full_depth_moves &&
                depth >= reduction_limit &&
                in_check == 0 && 
                get_move_capture(move_list->moves[count]) == 0 &&
                get_move_promoted(move_list->moves[count]) == 0
                ) {
                score = -negamax(-alpha - 1, -alpha, depth - 2);
                }
            
            else {score = alpha + 1;}
            
            // if found a better move during LMR
            if (score > alpha) {
                /* Once you've found a move with a score that is between alpha and beta,
                the rest of the moves are searched with the goal of proving that they are all bad.
                It's possible to do this a bit faster than a search that worries that one
                of the remaining moves might be good. */
                score = -negamax(-alpha - 1, -alpha, depth-1);
            
                /* If the algorithm finds out that it was wrong, and that one of the
                subsequent moves was better than the first PV move, it has to search again,
                in the normal alpha-beta manner.  This happens sometimes, and it's a waste of time,
                but generally not often enough to counteract the savings gained from doing the
                "bad move proof" search referred to earlier. */
                if((score > alpha) && (score < beta)) {
                    score = -negamax(-beta, -alpha, depth-1);
                }
            }
        }
        
        ply--;
        repetition_index--;
        take_back();

        if(stopped == 1) return 0;

        moves_searched++;

        // found a better move
        if (score > alpha) {

            hash_flag = hash_flag_exact;

            // store history moves
            if (get_move_capture(move_list->moves[count]) == 0) {
                history_moves[get_move_piece(move_list->moves[count])][get_move_target(move_list->moves[count])] += depth;
            }

            alpha = score;

            pv_table[ply][ply] = move_list->moves[count];

            for (int next_ply = ply + 1; next_ply < pv_length[ply + 1]; next_ply++) {
                pv_table[ply][next_ply] = pv_table[ply + 1][next_ply];
            }

            pv_length[ply] = pv_length[ply + 1];

            // fail-hard beta cutoff
            if (score >= beta) {

                write_hash_entry(beta, depth, hash_flag_beta);

                // processing killer moves

                if (get_move_capture(move_list->moves[count]) == 0) {
                    killer_moves[1][ply] = killer_moves[0][ply];
                    killer_moves[0][ply] = move_list->moves[count];
                }

                // node fails high
                return beta;
            }
        }
    }

    if (legal_moves == 0) {
        if (in_check) {
            return -mate_value + ply;
        } else {
            return draw_score;
        }
    }

    write_hash_entry(alpha, depth, hash_flag);

    // node fails high
    return alpha;
}

void search_position(int depth);