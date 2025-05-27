// OthelloServer.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PORT 1500
#define MAX_CLIENTS 2
#define BOARD_SIZE 8
#define MAX_MSG 1024

int client_sockets[MAX_CLIENTS];
char client_names[MAX_CLIENTS][50];
int ready_flags[MAX_CLIENTS] = {0, 0};
int current_turn = 0;
char board[BOARD_SIZE][BOARD_SIZE];

// ตำแหน่งเคลื่อนที่ของแต่ละทิศ (8 ทิศ)
int dx[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
int dy[8] = {-1, 0, 1, 1, 1, 0, -1, -1};

// ส่งข้อความไปยัง client
void send_to_client(int client, const char* msg) {
    send(client_sockets[client], msg, strlen(msg), 0);
}

// ส่งข้อความถึง client ทั้ง 2
void broadcast(const char* msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        send_to_client(i, msg);
    }
}

// แสดงกระดาน
void render_board(char* buffer) {
    strcpy(buffer, "\n  0 1 2 3 4 5 6 7\n");
    for (int i = 0; i < BOARD_SIZE; i++) {
        char line[100];
        sprintf(line, "%d ", i);
        for (int j = 0; j < BOARD_SIZE; j++) {
            sprintf(line + strlen(line), "%c ", board[i][j]);
        }
        strcat(buffer, line);
        strcat(buffer, "\n");
    }
}

// ตรวจสอบตำแหน่งที่เดินได้ถูกต้อง
int is_legal_move(int x, int y, char color) {
    char opponent = (color == 'B') ? 'W' : 'B';
    if (board[x][y] != ' ') return 0;

    for (int d = 0; d < 8; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        int found_opponent = 0;

        while (nx >= 0 && ny >= 0 && nx < BOARD_SIZE && ny < BOARD_SIZE) {
            if (board[nx][ny] == opponent) {
                found_opponent = 1;
            } else if (board[nx][ny] == color && found_opponent) {
                return 1;
            } else {
                break;
            }
            nx += dx[d];
            ny += dy[d];
        }
    }
    return 0;
}

// กลับหมากตามกติกา
void flip_disks(int x, int y, char color) {
    char opponent = (color == 'B') ? 'W' : 'B';

    for (int d = 0; d < 8; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        int path[BOARD_SIZE][2], path_len = 0;

        while (nx >= 0 && ny >= 0 && nx < BOARD_SIZE && ny < BOARD_SIZE) {
            if (board[nx][ny] == opponent) {
                path[path_len][0] = nx;
                path[path_len][1] = ny;
                path_len++;
            } else if (board[nx][ny] == color) {
                if (path_len > 0) {  // ✅ เช็คว่ามี opponent คั่นก่อนจะ flip
                    for (int i = 0; i < path_len; i++) {
                        board[path[i][0]][path[i][1]] = color;
                    }
                }
                break;
            } else {
                break;
            }
            nx += dx[d];
            ny += dy[d];
        }
    }
}

// เช็คว่ากระดานเต็ม
int board_full() {
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            if (board[i][j] == ' ') return 0;
    return 1;
}

// ประกาศผู้ชนะ
void declare_winner() {
    int black = 0, white = 0;
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (board[i][j] == 'B') black++;
            else if (board[i][j] == 'W') white++;
        }

    char msg[256];
    sprintf(msg, "\nGAME END!\n");
    broadcast(msg);
    if (black > white) {
        sprintf(msg, "%s WIN !!!\n", client_names[0]);
    } else if (white > black) {
        sprintf(msg, "%s WIN !!!\n", client_names[1]);
    } else {
        sprintf(msg, "DRAW !!!\n");
    }
    broadcast(msg);
}

// รอ readiness จาก client
void* client_handler(void* arg) {
    int id = *(int*)arg;
    char buffer[MAX_MSG];
    recv(client_sockets[id], client_names[id], sizeof(client_names[id]), 0);

    char hi_msg[100];
    sprintf(hi_msg, "Hi: %s\n", client_names[id]);
    send_to_client(id, hi_msg);

    // ถ้าเกิน 2 คน
    if (id >= MAX_CLIENTS) {
        send_to_client(id, "ผู้เล่นเต็มแล้ว\n");
        close(client_sockets[id]);
        return NULL;
    }

    // ถามว่า ready
    while (!ready_flags[id]) {
        send_to_client(id, "Are you ready to play [y/n]: ");
        memset(buffer, 0, MAX_MSG);
        recv(client_sockets[id], buffer, MAX_MSG, 0);
        if (buffer[0] == 'y' || buffer[0] == 'Y') {
            ready_flags[id] = 1;
        }
    }

    return NULL;
}

// เริ่มเกมหลังจาก ready
void start_game() {
    memset(board, ' ', sizeof(board));
    board[3][3] = board[4][4] = 'W';
    board[3][4] = board[4][3] = 'B';

    char buffer[MAX_MSG];
    int x, y;
    while (!board_full()) {
        memset(buffer, 0, MAX_MSG);
        render_board(buffer);
        strcat(buffer, "\n");

        char turn_msg[100];
        sprintf(turn_msg, "Your turn (%s): Enter x y: ", client_names[current_turn]);
        strcat(buffer, turn_msg);

        send_to_client(current_turn, buffer);

        recv(client_sockets[current_turn], buffer, MAX_MSG, 0);
        sscanf(buffer, "%d %d", &x, &y);

        char color = (current_turn == 0) ? 'B' : 'W';

        if (is_legal_move(x, y, color)) {
            board[x][y] = color;
            flip_disks(x, y, color);
            current_turn = 1 - current_turn;
        } else {
            send_to_client(current_turn, "Illegal move. Try again.\n");
        }
    }

    declare_winner();
}

int main() {
    int server_fd, client_fd, addr_len;
    struct sockaddr_in server_addr, client_addr;
    pthread_t threads[MAX_CLIENTS];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, MAX_CLIENTS);
    printf("Waiting for players...\n");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&addr_len);
        client_sockets[i] = client_fd;

        int* arg = malloc(sizeof(*arg));
        *arg = i;
        pthread_create(&threads[i], NULL, client_handler, arg);
    }

    // รอ readiness
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_join(threads[i], NULL);
    }

    // เริ่มเกม
    start_game();

    // ปิดทุกอย่าง
    for (int i = 0; i < MAX_CLIENTS; i++) close(client_sockets[i]);
    close(server_fd);
    return 0;
}
