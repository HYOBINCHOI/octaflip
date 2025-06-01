
/********************************************************************
 *  client2_strong.c ― Iterative Deepening + α‐β (강화 AI)
 *
 *  빌드:
 *      gcc client_strong.c cJSON.c -o client_strong -O2 -lm
 *
 *  실행 예:
 *      ./client_strong -ip 127.0.0.1 -port 12345 -username Bob
 *******************************************************************/

 #define _POSIX_C_SOURCE 200809L

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>         
 #include <arpa/inet.h>
 #include <sys/socket.h>
 #include <time.h>
 #include "cJSON.h"
 #include <math.h>
 
 #define SIZE     8
 #define BUF_SIZE 1024
 #define INF      1000000000
 
 /* ───── 인자 파싱 ─────────────────────────────────────────────────── */
 static void parse_args(int argc, char *argv[],
                        char *ip, int *port, char *username)
 {
     if (argc != 7) {
         fprintf(stderr,
                 "Usage: %s -ip <server_ip> -port <port> -username <name>\n",
                 argv[0]);
         exit(EXIT_FAILURE);
     }
     for (int i = 1; i < argc; i += 2) {
         if (strcmp(argv[i], "-ip") == 0) {
             strncpy(ip, argv[i+1], INET_ADDRSTRLEN - 1);
             ip[INET_ADDRSTRLEN - 1] = '\0';
         } else if (strcmp(argv[i], "-port") == 0) {
             *port = atoi(argv[i+1]);
             if (*port <= 0 || *port > 65535) {
                 fprintf(stderr, "[Client] Invalid port: %s\n", argv[i+1]);
                 exit(EXIT_FAILURE);
             }
         } else if (strcmp(argv[i], "-username") == 0) {
             strncpy(username, argv[i+1], 31);
             username[31] = '\0';
         } else {
             fprintf(stderr, "[Client] Unknown option: %s\n", argv[i]);
             exit(EXIT_FAILURE);
         }
     }
 }
 
 /* ───── robust recv_json ─────────────────────────────────────────── */
 static cJSON* recv_json(int sockfd)
 {
     _Thread_local static char   buf[BUF_SIZE];
     _Thread_local static size_t used = 0;
 
     for (;;) {
         char *nl = memchr(buf, '\n', used);
         if (nl) {
             size_t linelen = nl - buf;
             if (linelen == 0) {
                 size_t remain = used - 1;
                 memmove(buf, nl + 1, remain);
                 used = remain;
                 continue;
             }
             char json_txt[BUF_SIZE];
             memcpy(json_txt, buf, linelen);
             json_txt[linelen] = '\0';
 
             size_t remain = used - (linelen + 1);
             memmove(buf, nl + 1, remain);
             used = remain;
 
             cJSON *json = cJSON_Parse(json_txt);
             if (json) return json;
 
             fprintf(stderr, "[Client] cJSON_Parse error, ignored: %s\n", json_txt);
             continue;
         }
 
         ssize_t n = recv(sockfd, buf + used, BUF_SIZE - used - 1, 0);
         if (n <= 0) return NULL;
         used += n;
         buf[used] = '\0';
 
         if (used >= BUF_SIZE - 1) {
             fprintf(stderr, "[recv_json] buffer overflow\n");
             return NULL;
         }
     }
 }
 
 /* ───── send_json ───────────────────────────────────────────────── */
 static int send_json(int sockfd, cJSON *msg)
 {
     if (!msg) return -1;
     char *out = cJSON_PrintUnformatted(msg);
     if (!out) {
         fprintf(stderr, "[Client] cJSON_PrintUnformatted failed\n");
         return -1;
     }
     size_t len = strlen(out);
     if (send(sockfd, out, len, 0) < 0) {
         perror("[Client] send json failed");
         free(out);
         return -1;
     }
     if (send(sockfd, "\n", 1, 0) < 0) {
         perror("[Client] send newline failed");
         free(out);
         return -1;
     }
     free(out);
     return 0;
 }
 
 /* =================================================================
  *                  시간 제한용 유틸리티
  * =================================================================*/
 static struct timespec start_time;
 static const double TIME_LIMIT = 2.9;  // 2.9초 후 탐색 중단
 
 static double elapsed_time()
 {
     struct timespec now;
     clock_gettime(CLOCK_MONOTONIC, &now);
     return (now.tv_sec - start_time.tv_sec)
          + (now.tv_nsec - start_time.tv_nsec) * 1e-9;
 }
 
 static int time_exceeded()
 {
     return elapsed_time() >= TIME_LIMIT;
 }
 
 /* =================================================================
  *                        AI  (Iterative Deepening + α-β 가지치기)
  * =================================================================*/
 static const int DR[8] = { -1,-1,-1, 0, 0, 1, 1, 1 };
 static const int DC[8] = { -1, 0, 1,-1, 1,-1, 0, 1 };
 
 /* 보드 복사 */
 static inline void copy_board(char dst[SIZE][SIZE], char src[SIZE][SIZE])
 {
     memcpy(dst, src, SIZE * SIZE);
 }
 
 /* 주어진 이동을 보드에 적용 (점프인지 아닌지 구분) */
 static void apply_move_sim(char bd[SIZE][SIZE],
                            int r1,int c1,int r2,int c2,
                            char me,int is_jump)
 {
     if (is_jump) bd[r1][c1] = '.';
     bd[r2][c2] = me;
     char opp = (me == 'R' ? 'B' : 'R');
     for (int d = 0; d < 8; ++d) {
         int nr = r2 + DR[d], nc = c2 + DC[d];
         if (0 <= nr && nr < SIZE && 0 <= nc && nc < SIZE && bd[nr][nc] == opp)
             bd[nr][nc] = me;
     }
 }
 
 /* 보드 평가 함수: 말 수 차이 + 모빌리티 차이 (원본과 동일) */
 static int mobility(char bd[SIZE][SIZE], char me)
 {
     int cnt = 0;
     for (int r = 0; r < SIZE; ++r) {
         for (int c = 0; c < SIZE; ++c) {
             if (bd[r][c] == me) {
                 for (int d = 0; d < 8; ++d) {
                     int nr = r + DR[d], nc = c + DC[d];
                     for (int step = 1; step <= 2; ++step,
                          nr += DR[d], nc += DC[d]) {
                         if (0 <= nr && nr < SIZE && 0 <= nc && nc < SIZE
                             && bd[nr][nc] == '.') {
                             ++cnt; 
                             goto NEXT_CELL; 
                         }
                     }
                 }
             }
             NEXT_CELL: ;
         }
     }
     return cnt;
 }
 
 static int evaluate_board(char bd[SIZE][SIZE], char me)
 {
     char opp = (me == 'R' ? 'B' : 'R');
     int my_cnt = 0, opp_cnt = 0;
     for (int i = 0; i < SIZE; ++i) {
         for (int j = 0; j < SIZE; ++j) {
             if      (bd[i][j] == me)  ++my_cnt;
             else if (bd[i][j] == opp) ++opp_cnt;
         }
     }
     int piece_diff = my_cnt - opp_cnt;
     int my_mob = mobility(bd, me);
     int opp_mob = mobility(bd, opp);
     int mob_diff = my_mob - opp_mob;
     return piece_diff * 100 + mob_diff * 10;
 }
 
 /* 말이 아직 존재하는지 검사 (턴 건너뛰기 여부 체크) */
 static int has_moves(char bd[SIZE][SIZE], char player)
 {
     for (int r = 0; r < SIZE; ++r) {
         for (int c = 0; c < SIZE; ++c) {
             if (bd[r][c] == player) {
                 for (int d = 0; d < 8; ++d) {
                     int nr = r + DR[d], nc = c + DC[d];
                     for (int step = 1; step <= 2; ++step,
                          nr += DR[d], nc += DC[d]) {
                         if (nr < 0 || nr >= SIZE || nc < 0 || nc >= SIZE)
                             break;
                         if (bd[nr][nc] == '.') return 1;
                     }
                 }
             }
         }
     }
     return 0;
 }
 
 /* ====================
    α-β 가지치기 탐색 (negamax 형태)
    ==================== */
 static int alpha_beta(char bd[SIZE][SIZE],
                       char me, char player,
                       int depth, int alpha, int beta)
 {
     if (time_exceeded()) {
         return evaluate_board(bd, me);
     }
     if (depth == 0) {
         return evaluate_board(bd, me);
     }
 
     char opp = (player == 'R' ? 'B' : 'R');
 
     // 만약 현재 플레이어가 둘 수 없으면(턴 건너뛰기 검사)
     if (!has_moves(bd, player)) {
         if (!has_moves(bd, opp)) {
             // 양쪽 모두 못 두면 게임 종료, 터미널 스코어
             int my_cnt = 0, opp_cnt = 0;
             for (int i = 0; i < SIZE; ++i) {
                 for (int j = 0; j < SIZE; ++j) {
                     if      (bd[i][j] == me)  ++my_cnt;
                     else if (bd[i][j] == opp) ++opp_cnt;
                 }
             }
             if      (my_cnt > opp_cnt) return  INF/2;
             else if (my_cnt < opp_cnt) return -INF/2;
             else                        return  0;
         }
         // 상대만 수가 있으면, 내 차례 건너뛰기
         int val = -alpha_beta(bd, me, opp, depth, -beta, -alpha);
         return val;
     }
 
     int best_val = -INF;
     // 모든 합법 수들을 “정적 평가” 기준으로 정렬하여 순차 탐색
     //  (Move‐Ordering)
     typedef struct {
         int r1, c1, r2, c2;
         int static_score;
     } Move;
     Move all_moves[256];
     int move_cnt = 0;
 
     // (1) 가능한 모든 수 생성 & 정적 평가값(static_score) 계산
     for (int r = 0; r < SIZE; ++r) {
         for (int c = 0; c < SIZE; ++c) {
             if (bd[r][c] != player) continue;
             for (int d = 0; d < 8; ++d) {
                 int nr = r + DR[d], nc = c + DC[d];
                 for (int step = 1; step <= 2; ++step,
                      nr += DR[d], nc += DC[d]) {
                     if (nr < 0 || nr >= SIZE || nc < 0 || nc >= SIZE) break;
                     if (bd[nr][nc] != '.') continue;
 
                     // 임시 보드에 한 수 적용
                     char sim[SIZE][SIZE];
                     copy_board(sim, bd);
                     apply_move_sim(sim, r, c, nr, nc, player, step == 2);
                     // 정적 평가: evaluate_board(…, player) 사용
                     int st_score = evaluate_board(sim, player);
                     all_moves[move_cnt++] = (Move){r, c, nr, nc, st_score};
                 }
             }
         }
     }
 
     // (2) 정적 평가(static_score) 내림차순으로 정렬(버블 정렬로 간단 구현)
     for (int i = 0; i < move_cnt; ++i) {
         for (int j = i + 1; j < move_cnt; ++j) {
             if (all_moves[j].static_score > all_moves[i].static_score) {
                 Move tmp = all_moves[i];
                 all_moves[i] = all_moves[j];
                 all_moves[j] = tmp;
             }
         }
     }
 
     // (3) α‐β 탐색: ordering된 순서대로 탐색
     for (int i = 0; i < move_cnt; ++i) {
         int r1 = all_moves[i].r1, c1 = all_moves[i].c1;
         int r2 = all_moves[i].r2, c2 = all_moves[i].c2;
         char sim[SIZE][SIZE];
         copy_board(sim, bd);
         apply_move_sim(sim, r1, c1, r2, c2, player,
                        (abs(r1 - r2) > 1 || abs(c1 - c2) > 1));
 
         int val = -alpha_beta(sim, me, opp, depth - 1, -beta, -alpha);
         if (val > best_val) best_val = val;
         if (best_val > alpha) alpha = best_val;
         if (alpha >= beta) break;
         if (time_exceeded()) break;
     }
 
     return best_val;
 }
 
 /* ====================
    Iterative Deepening을 이용한 move_generate
    ==================== */
 static void move_generate(char board[SIZE][SIZE], char me,
                           int *out_r1, int *out_c1,
                           int *out_r2, int *out_c2)
 {
     clock_gettime(CLOCK_MONOTONIC, &start_time);
 
     char opp = (me == 'R' ? 'B' : 'R');
     int best_score_overall = -INF;
     int best_r1 = -1, best_c1 = -1, best_r2 = -1, best_c2 = -1;
 
     // 시간 내에 가능한 만큼 깊이(depth)를 1씩 늘리며 탐색
     for (int depth = 1; depth <= 8; ++depth) {
         if (time_exceeded()) break;
 
         int local_best_score = -INF;
         int local_r1 = -1, local_c1 = -1, local_r2 = -1, local_c2 = -1;
 
         // 현재 depth에서 가능한 모든 수를 한 번만 훑어보고 정적 점수로 정렬
         typedef struct {
             int r1, c1, r2, c2;
             int static_score;
         } Move;
         Move all_moves[256];
         int move_cnt = 0;
 
         // (1) 후보 생성 & 정적 평가
         for (int r = 0; r < SIZE; ++r) {
             for (int c = 0; c < SIZE; ++c) {
                 if (board[r][c] != me) continue;
                 for (int d = 0; d < 8; ++d) {
                     int nr = r + DR[d], nc = c + DC[d];
                     for (int step = 1; step <= 2; ++step,
                          nr += DR[d], nc += DC[d]) {
                         if (nr < 0 || nr >= SIZE || nc < 0 || nc >= SIZE) break;
                         if (board[nr][nc] != '.') continue;
 
                         char sim[SIZE][SIZE];
                         copy_board(sim, board);
                         apply_move_sim(sim, r, c, nr, nc, me,
                                        (abs(r - nr) > 1 || abs(c - nc) > 1));
                         int st_score = evaluate_board(sim, me);
                         all_moves[move_cnt++] = (Move){r, c, nr, nc, st_score};
                     }
                 }
             }
         }
 
         // (2) 정렬: 정적 점수 내림차순
         for (int i = 0; i < move_cnt; ++i) {
             for (int j = i + 1; j < move_cnt; ++j) {
                 if (all_moves[j].static_score > all_moves[i].static_score) {
                     Move tmp = all_moves[i];
                     all_moves[i] = all_moves[j];
                     all_moves[j] = tmp;
                 }
             }
         }
 
         // (3) α‐β 탐색 (logging 최적 수를 기록)
         for (int i = 0; i < move_cnt; ++i) {
             if (time_exceeded()) break;
 
             int r1 = all_moves[i].r1, c1 = all_moves[i].c1;
             int r2 = all_moves[i].r2, c2 = all_moves[i].c2;
             char sim[SIZE][SIZE];
             copy_board(sim, board);
             apply_move_sim(sim, r1, c1, r2, c2, me,
                           (abs(r1 - r2) > 1 || abs(c1 - c2) > 1));
 
             int score = -alpha_beta(sim, me, opp, depth - 1, -INF, +INF);
             if (score > local_best_score) {
                 local_best_score = score;
                 local_r1 = r1; local_c1 = c1;
                 local_r2 = r2; local_c2 = c2;
             }
         }
 
         // (4) 이 깊이 탐색을 완료했다면, 최고 결과를 “전체 최적”으로 갱신
         if (!time_exceeded() && local_r1 >= 0) {
             best_score_overall = local_best_score;
             best_r1 = local_r1; best_c1 = local_c1;
             best_r2 = local_r2; best_c2 = local_c2;
         } else {
             // 시간이 다 되었거나 후보가 없으면 종료
             break;
         }
     }
 
     // 만약 둘 수 없으면 패스(-1 신호)
     if (best_r1 < 0) {
         *out_r1 = *out_c1 = *out_r2 = *out_c2 = -1;
     } else {
         *out_r1 = best_r1; 
         *out_c1 = best_c1;
         *out_r2 = best_r2; 
         *out_c2 = best_c2;
     }
 }
 
 /* =================================================================
  *              이하 메인, 게임 루프, 프로토콜 처리 (원본과 동일)
  * =================================================================*/
 int main(int argc, char *argv[])
 {
     char server_ip[INET_ADDRSTRLEN] = {0};
     int  server_port = 0;
     char username[32] = {0};
 
     parse_args(argc, argv, server_ip, &server_port, username);
 
     int sockfd;
     struct sockaddr_in serv_addr;
     if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
         perror("[Client] socket creation failed");
         exit(EXIT_FAILURE);
     }
     memset(&serv_addr, 0, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_port   = htons(server_port);
     if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
         fprintf(stderr, "[Client] Invalid address: %s\n", server_ip);
         close(sockfd);
         exit(EXIT_FAILURE);
     }
     if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
         perror("[Client] connect failed");
         close(sockfd);
         exit(EXIT_FAILURE);
     }
     printf("[Client %s] Connected to %s:%d\n",
            username, server_ip, server_port);
 
     /* ---------- register ---------- */
     cJSON *reg = cJSON_CreateObject();
     cJSON_AddStringToObject(reg, "type", "register");
     cJSON_AddStringToObject(reg, "username", username);
     send_json(sockfd, reg); 
     cJSON_Delete(reg);
 
     cJSON *res = recv_json(sockfd);
     if (!res) { 
         fprintf(stderr,"[Client] No response to register.\n"); 
         close(sockfd);
         exit(EXIT_FAILURE);
     }
     cJSON *rtype = cJSON_GetObjectItemCaseSensitive(res,"type");
     if (!cJSON_IsString(rtype) || strcmp(rtype->valuestring,"register_ack")!=0) {
         fprintf(stderr,"[Client] register failed.\n");
         cJSON_Delete(res);
         close(sockfd);
         exit(EXIT_FAILURE);
     }
     cJSON_Delete(res);
 
     /* ---------- wait game_start ---------- */
     cJSON *gst = recv_json(sockfd);
     if (!gst) {
         fprintf(stderr,"[Client] No game_start received.\n");
         close(sockfd);
         exit(EXIT_FAILURE);
     }
     cJSON *fst = cJSON_GetObjectItemCaseSensitive(gst,"first_player");
     char my_color = (strcmp(fst->valuestring, username)==0 ? 'R':'B');
     cJSON_Delete(gst);
 
     /* ---------- main game loop ---------- */
     while (1) {
         cJSON *msg = recv_json(sockfd);
         if (!msg) { 
             fprintf(stderr,"[Client] Server closed.\n"); 
             break; 
         }
 
         cJSON *tp = cJSON_GetObjectItemCaseSensitive(msg,"type");
         if (!cJSON_IsString(tp)){
             cJSON_Delete(msg);
             continue;
         }
 
         /* === your_turn === */
         if (strcmp(tp->valuestring,"your_turn")==0) {
             cJSON *jboard = cJSON_GetObjectItemCaseSensitive(msg,"board");
             cJSON *jtimeout = cJSON_GetObjectItemCaseSensitive(msg,"timeout");
             if (!cJSON_IsArray(jboard) || !cJSON_IsNumber(jtimeout)) {
                 fprintf(stderr,"[Client] Invalid your_turn format\n");
                 cJSON_Delete(msg);
                 continue;
             }
             char bd[SIZE][SIZE];
             for (int i = 0; i < SIZE; ++i) {
                 cJSON *row = cJSON_GetArrayItem(jboard, i);
                 memcpy(bd[i], row->valuestring, SIZE);
             }
 
             int r1, c1, r2, c2;
             move_generate(bd, my_color, &r1, &c1, &r2, &c2);
 
             cJSON *mv = cJSON_CreateObject();
             cJSON_AddStringToObject(mv,"type","move");
             cJSON_AddStringToObject(mv,"username",username);
             if (r1 < 0) {  /* PASS */
                 cJSON_AddNumberToObject(mv,"sx",0);
                 cJSON_AddNumberToObject(mv,"sy",0);
                 cJSON_AddNumberToObject(mv,"tx",0);
                 cJSON_AddNumberToObject(mv,"ty",0);
             } else {
                 cJSON_AddNumberToObject(mv,"sx",r1+1);
                 cJSON_AddNumberToObject(mv,"sy",c1+1);
                 cJSON_AddNumberToObject(mv,"tx",r2+1);
                 cJSON_AddNumberToObject(mv,"ty",c2+1);
             }
             send_json(sockfd,mv);
             cJSON_Delete(mv);
             cJSON_Delete(msg);
             continue;
         }
 
         /* === game_over === */
         if (strcmp(tp->valuestring,"game_over")==0) {
             puts("[Client] Game over!");
             cJSON_Delete(msg);
             break;
         }
 
         /* 기타 메시지 => 무시 */
         cJSON_Delete(msg);
     }
 
     close(sockfd);
     return 0;
 }