#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // close()
#include <arpa/inet.h>  // socket
#include <pthread.h>    // thread

#define PORT 12345
#define BOARD_SIZE 8

// ตัวแปรหมาก: B = ดำ, W = ขาว, . = ว่าง
#define BLACK 'B'
#define WHITE 'W'
#define EMPTY '.'

// กระดานเกม
char board[BOARD_SIZE][BOARD_SIZE];

// ชื่อผู้เล่น
char player1_name[20];
char player2_name[20];

// socket ของ client 2 ฝ่าย
int client_sockets[2];

// ตำแหน่งเดินปัจจุบันที่ส่งให้ client รู้
int current_player = 0; // 0 = player1, 1 = player2

// สร้างกระดานเริ่มต้น Othello
void init_board() {
    for(int i=0; i<BOARD_SIZE; i++)
        for(int j=0; j<BOARD_SIZE; j++)
            board[i][j] = EMPTY;
    
    // จุดเริ่มต้น 4 ช่องตรงกลางตามกติกา
    board[3][3] = WHITE;
    board[3][4] = BLACK;
    board[4][3] = BLACK;
    board[4][4] = WHITE;
}

// แสดงกระดานใน server (debug)
void print_board() {
    printf("  ");
    for(int i=0; i<BOARD_SIZE; i++) printf("%d ", i);
    printf("\n");
    for(int i=0; i<BOARD_SIZE; i++) {
        printf("%d ", i);
        for(int j=0; j<BOARD_SIZE; j++) {
            printf("%c ", board[i][j]);
        }
        printf("\n");
    }
}

// ส่งกระดานไปยัง client (ส่งเป็นข้อความ)
void send_board(int client) {
    char msg[BOARD_SIZE * BOARD_SIZE + 1];
    int k = 0;
    for(int i=0; i<BOARD_SIZE; i++)
        for(int j=0; j<BOARD_SIZE; j++)
            msg[k++] = board[i][j];
    msg[k] = '\0';

    send(client, msg, strlen(msg)+1, 0);
}

// เช็คตำแหน่ง (row,col) ว่าวางหมากได้ไหม
int valid_move(int row, int col, char color) {
    if(row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) return 0;
    if(board[row][col] != EMPTY) return 0;

    char opponent = (color == BLACK) ? WHITE : BLACK;

    // ตรวจสอบ 8 ทิศทางว่ามีหมากคู่ต่อสู้ถูกคั่นโดยหมากเราไหม
    int directions[8][2] = {
        {-1, -1}, {-1, 0}, {-1, 1},
        {0, -1},           {0, 1},
        {1, -1},  {1, 0},  {1, 1}
    };

    for(int d=0; d<8; d++) {
        int dx = directions[d][0];
        int dy = directions[d][1];
        int x = row + dx;
        int y = col + dy;
        int found_opponent = 0;

        while(x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE) {
            if(board[x][y] == opponent) {
                found_opponent = 1;
            } else if(board[x][y] == color && found_opponent) {
                return 1; // valid move
            } else {
                break;
            }
            x += dx;
            y += dy;
        }
    }
    return 0;
}

// กลับสีหมากตามกติกาหลังวางที่ (row,col)
void flip_discs(int row, int col, char color) {
    char opponent = (color == BLACK) ? WHITE : BLACK;
    int directions[8][2] = {
        {-1, -1}, {-1, 0}, {-1, 1},
        {0, -1},           {0, 1},
        {1, -1},  {1, 0},  {1, 1}
    };

    for(int d=0; d<8; d++) {
        int dx = directions[d][0];
        int dy = directions[d][1];
        int x = row + dx;
        int y = col + dy;
        int discs_to_flip[BOARD_SIZE][2];
        int count = 0;

        while(x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE) {
            if(board[x][y] == opponent) {
                discs_to_flip[count][0] = x;
                discs_to_flip[count][1] = y;
                count++;
            } else if(board[x][y] == color && count > 0) {
                // พบหมากตัวเองปิดท้าย ให้กลับสีทั้งหมด
                for(int i=0; i<count; i++) {
                    board[discs_to_flip[i][0]][discs_to_flip[i][1]] = color;
                }
                break;
            } else {
                break;
            }
            x += dx;
            y += dy;
        }
    }
}

// นับคะแนนหมากดำและขาว
void count_score(int *black_score, int *white_score) {
    *black_score = 0;
    *white_score = 0;
    for(int i=0; i<BOARD_SIZE; i++) {
        for(int j=0; j<BOARD_SIZE; j++) {
            if(board[i][j] == BLACK) (*black_score)++;
            else if(board[i][j] == WHITE) (*white_score)++;
        }
    }
}

// เช็คว่ามีตำแหน่งว่างและ valid move ของ color หรือไม่
int can_move(char color) {
    for(int i=0; i<BOARD_SIZE; i++)
        for(int j=0; j<BOARD_SIZE; j++)
            if(valid_move(i,j,color)) return 1;
    return 0;
}

// ส่งข้อความ string ไป client
void send_msg(int client, const char *msg) {
    send(client, msg, strlen(msg)+1, 0);
}

// รับข้อความ string จาก client
void recv_msg(int client, char *buffer, int size) {
    int len = recv(client, buffer, size, 0);
    if(len > 0) buffer[len] = '\0';
}

// ทำงานใน thread สำหรับ client แต่ละคน
void *client_handler(void *arg) {
    int idx = *(int*)arg;  // 0 หรือ 1
    int client = client_sockets[idx];

    // รับชื่อผู้เล่น
    char name[20];
    recv_msg(client, name, sizeof(name));
    if(idx == 0) strcpy(player1_name, name);
    else strcpy(player2_name, name);

    printf("Player %d connected: %s\n", idx+1, name);

    // รอให้ผู้เล่นอีกฝ่ายเชื่อมต่อ
    while(client_sockets[1 - idx] == 0) {
        sleep(1);
    }

    // แจ้งชื่อคู่ต่อสู้ให้ client รู้
    if(idx == 0) send_msg(client, player2_name);
    else send_msg(client, player1_name);

    // เริ่มเล่นเกม
    while(1) {
        if(current_player == idx) {
            // ส่งกระดานให้ client และแจ้งถึงตาของ client นี้
            send_board(client);
            send_msg(client, "YOUR_MOVE");

            char move[10];
            recv_msg(client, move, sizeof(move));

            // ตั้งการวางตำแหน่งเป็น row, col เช่น "3 4"
            int row, col;
            sscanf(move, "%d %d", &row, &col);

            if(valid_move(row, col, (idx == 0) ? BLACK : WHITE)) {
                // วางหมาก
                board[row][col] = (idx == 0) ? BLACK : WHITE;
                flip_discs(row, col, board[row][col]);

                print_board();

                // สลับตา
                current_player = 1 - current_player;
            } else {
                send_msg(client, "INVALID_MOVE");
                continue;
            }
        } else {
            // ตาของอีกฝ่าย รอข้อมูล
            send_board(client);
            send_msg(client, "WAIT");
            sleep(1);
        }

        // เช็คเกมจบหรือไม่ (ไม่มี valid move ทั้งสองฝ่าย)
        if(!can_move(BLACK) && !can_move(WHITE)) {
            // ส่งกระดานสุดท้ายและผลลัพธ์
            for(int i=0; i<2; i++) {
                send_board(client_sockets[i]);
                int bscore, wscore;
                count_score(&bscore, &wscore);
                char endmsg[100];
                if(bscore > wscore) {
                    sprintf(endmsg, "GAME_OVER: Black wins! %s %d - %s %d", player1_name, bscore, player2_name, wscore);
                } else if(wscore > bscore) {
                    sprintf(endmsg, "GAME_OVER: White wins! %s %d - %s %d", player2_name, wscore, player1_name, bscore);
                } else {
                    sprintf(endmsg, "GAME_OVER: Draw %d - %d", bscore, wscore);
                }
                send_msg(client_sockets[i], endmsg);
            }
            break;
        }
    }

    close(client);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // สร้าง socket server
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // ตั้งค่า address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // รับทุก IP
    address.sin_port = htons(PORT);

    // bind socket กับ port
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // เริ่ม listen socket
    if(listen(server_fd, 2) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    // init กระดานเกม
    init_board();

    pthread_t threads[2];
    int client_idx[2];

    // รอรับ client 2 ตัว
    for(int i=0; i<2; i++) {
        int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if(client_socket < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        client_sockets[i] = client_socket;
        client_idx[i] = i;

        // สร้าง thread เพื่อจัดการ client
        pthread_create(&threads[i], NULL, client_handler, &client_idx[i]);
    }

    // รอ thread ทั้งสองจบ
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    close(server_fd);

    return 0;
}
