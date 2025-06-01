#include <time.h>
#include <math.h>

#define INF 1000000000
#define MAX_DEPTH 3
#define TOP_K_MOVES 6
#define TIME_LIMIT 2.9

static struct timespec start_time;

static double elapsed_time() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start_time.tv_sec) + (now.tv_nsec - start_time.tv_nsec) * 1e-9;
}

static int time_exceeded() {
    return elapsed_time() >= TIME_LIMIT;
}

static void copy_board(char dst[BOARD_SIZE][BOARD_SIZE], char src[BOARD_SIZE][BOARD_SIZE]) {
    memcpy(dst, src, sizeof(char) * BOARD_SIZE * BOARD_SIZE);
}

static void apply_move_sim(char bd[BOARD_SIZE][BOARD_SIZE],
                           int r1, int c1, int r2, int c2,
                           char me, int is_jump) {
    if (is_jump) bd[r1][c1] = '.';
    bd[r2][c2] = me;
    char opp = (me == 'R' ? 'B' : 'R');
    for (int d = 0; d < 8; ++d) {
        int nr = r2 + directions[d][0], nc = c2 + directions[d][1];
        if (0 <= nr && nr < BOARD_SIZE && 0 <= nc && nc < BOARD_SIZE && bd[nr][nc] == opp)
            bd[nr][nc] = me;
    }
}

static int has_moves(char bd[BOARD_SIZE][BOARD_SIZE], char player) {
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (bd[r][c] != player) continue;
            for (int d = 0; d < 8; ++d) {
                for (int step = 1; step <= 2; ++step) {
                    int nr = r + step * directions[d][0];
                    int nc = c + step * directions[d][1];
                    if (nr < 0 || nr >= BOARD_SIZE || nc < 0 || nc >= BOARD_SIZE) break;
                    if (bd[nr][nc] == '.') return 1;
                }
            }
        }
    }
    return 0;
}

static int mobility(char bd[BOARD_SIZE][BOARD_SIZE], char me) {
    int cnt = 0;
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (bd[r][c] != me) continue;
            for (int d = 0; d < 8; ++d) {
                for (int step = 1; step <= 2; ++step) {
                    int nr = r + step * directions[d][0];
                    int nc = c + step * directions[d][1];
                    if (nr < 0 || nr >= BOARD_SIZE || nc < 0 || nc >= BOARD_SIZE) break;
                    if (bd[nr][nc] == '.') {
                        cnt++;
                        goto NEXT;
                    }
                }
            }
            NEXT:;
        }
    }
    return cnt;
}

static int evaluate_board(char bd[BOARD_SIZE][BOARD_SIZE], char me) {
    char opp = (me == 'R') ? 'B' : 'R';
    int my_cnt = 0, opp_cnt = 0;
    for (int i = 0; i < BOARD_SIZE; ++i) {
        for (int j = 0; j < BOARD_SIZE; ++j) {
            if (bd[i][j] == me) my_cnt++;
            else if (bd[i][j] == opp) opp_cnt++;
        }
    }
    int piece_diff = my_cnt - opp_cnt;
    int mob_diff = mobility(bd, me) - mobility(bd, opp);
    return piece_diff * 100 + mob_diff * 10;
}

static int alpha_beta(char bd[BOARD_SIZE][BOARD_SIZE], char me, char player,
                      int depth, int alpha, int beta) {
    if (time_exceeded()) return evaluate_board(bd, me);
    if (depth == 0) return evaluate_board(bd, me);

    char opp = (player == 'R') ? 'B' : 'R';
    if (!has_moves(bd, player)) {
        if (!has_moves(bd, opp)) {
            int my_cnt = 0, opp_cnt = 0;
            for (int i = 0; i < BOARD_SIZE; ++i)
                for (int j = 0; j < BOARD_SIZE; ++j) {
                    if (bd[i][j] == me) my_cnt++;
                    else if (bd[i][j] == opp) opp_cnt++;
                }
            if (my_cnt > opp_cnt) return INF / 2;
            else if (my_cnt < opp_cnt) return -INF / 2;
            else return 0;
        }
        return -alpha_beta(bd, me, opp, depth, -beta, -alpha);
    }

    int best = -INF;
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (bd[r][c] != player) continue;
            for (int d = 0; d < 8; ++d) {
                for (int step = 1; step <= 2; ++step) {
                    int nr = r + step * directions[d][0];
                    int nc = c + step * directions[d][1];
                    if (nr < 0 || nr >= BOARD_SIZE || nc < 0 || nc >= BOARD_SIZE) break;
                    if (bd[nr][nc] != '.') continue;

                    char sim[BOARD_SIZE][BOARD_SIZE];
                    copy_board(sim, bd);
                    apply_move_sim(sim, r, c, nr, nc, player, step == 2);
                    int val = -alpha_beta(sim, me, opp, depth - 1, -beta, -alpha);
                    if (val > best) best = val;
                    if (best > alpha) alpha = best;
                    if (alpha >= beta) return alpha;
                    if (time_exceeded()) return best;
                }
            }
        }
    }
    return best;
}

int generate_move(char board[BOARD_SIZE][BOARD_SIZE], char player_color,
                  int *out_r1, int *out_c1, int *out_r2, int *out_c2) {
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    update_led_matrix(board);

    char opp = (player_color == 'R') ? 'B' : 'R';
    int best_score = -INF;
    int best_r1 = -1, best_c1 = -1, best_r2 = -1, best_c2 = -1;

    for (int depth = 1; depth <= MAX_DEPTH; ++depth) {
        if (time_exceeded()) break;

        typedef struct {
            int r1, c1, r2, c2, score;
        } Move;
        Move moves[256];
        int count = 0;

        for (int r = 0; r < BOARD_SIZE; ++r) {
            for (int c = 0; c < BOARD_SIZE; ++c) {
                if (board[r][c] != player_color) continue;
                for (int d = 0; d < 8; ++d) {
                    for (int step = 1; step <= 2; ++step) {
                        int nr = r + step * directions[d][0];
                        int nc = c + step * directions[d][1];
                        if (nr < 0 || nr >= BOARD_SIZE || nc < 0 || nc >= BOARD_SIZE) break;
                        if (board[nr][nc] != '.') continue;

                        char sim[BOARD_SIZE][BOARD_SIZE];
                        copy_board(sim, board);
                        apply_move_sim(sim, r, c, nr, nc, player_color, step == 2);
                        int score = evaluate_board(sim, player_color);
                        moves[count++] = (Move){r, c, nr, nc, score};
                    }
                }
            }
        }

        for (int i = 0; i < count - 1; ++i) {
            for (int j = i + 1; j < count; ++j) {
                if (moves[j].score > moves[i].score) {
                    Move tmp = moves[i]; moves[i] = moves[j]; moves[j] = tmp;
                }
            }
        }

        int limit = (count < TOP_K_MOVES) ? count : TOP_K_MOVES;
        for (int i = 0; i < limit; ++i) {
            if (time_exceeded()) break;
            char sim[BOARD_SIZE][BOARD_SIZE];
            copy_board(sim, board);
            apply_move_sim(sim, moves[i].r1, moves[i].c1, moves[i].r2, moves[i].c2, player_color,
                           abs(moves[i].r1 - moves[i].r2) > 1 || abs(moves[i].c1 - moves[i].c2) > 1);
            int score = -alpha_beta(sim, player_color, opp, depth - 1, -INF, INF);
            if (score > best_score) {
                best_score = score;
                best_r1 = moves[i].r1; best_c1 = moves[i].c1;
                best_r2 = moves[i].r2; best_c2 = moves[i].c2;
            }
        }
    }

    if (best_r1 < 0) {
        *out_r1 = *out_c1 = *out_r2 = *out_c2 = 0;
        return 0;  // pass
    } else {
        *out_r1 = best_r1; *out_c1 = best_c1;
        *out_r2 = best_r2; *out_c2 = best_c2;
        return 1;  // valid move
    }
}