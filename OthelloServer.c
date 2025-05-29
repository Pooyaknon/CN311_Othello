#include <stdio.h>              // สำหรับฟังก์ชัน I/O เช่น printf, sprintf
#include <stdlib.h>             // สำหรับฟังก์ชันพื้นฐาน เช่น memset
#include <string.h>             // สำหรับจัดการข้อความ เช่น strcpy, strlen
#include <unistd.h>             // สำหรับ close() และ read(), write()
#include <pthread.h>            // สำหรับสร้าง thread เพื่อรองรับผู้เล่นหลายคน
#include <arpa/inet.h>          // สำหรับจัดการ network เช่น sockaddr_in

#define PORT 15000              // พอร์ตที่ server จะฟัง
#define MAX_CLIENTS 2           // จำนวนผู้เล่นสูงสุด
#define BOARD_SIZE 8            // ขนาดกระดาน Othello
#define MAX_MSG 1024            // ขนาด buffer สำหรับข้อความที่รับ/ส่ง

// โครงสร้างเก็บคะแนนของทั้งสองฝั่ง
typedef struct {
    int black;
    int white;
} Score;

// ตัวแปร global สำหรับเก็บสถานะต่าง ๆ ของเกม
int client_sockets[MAX_CLIENTS];        // เก็บ socket ของผู้เล่นแต่ละคน
char client_names[MAX_CLIENTS][50];     // ชื่อของผู้เล่นแต่ละคน
int ready_flags[MAX_CLIENTS] = {0, 0};  // ธงบอกว่าแต่ละคนกด "พร้อม" แล้วหรือยัง
int current_turn = 0;                   // ผู้เล่นคนที่กำลังเล่นอยู่ (0 หรือ 1)
int turn_count = 1;                     // นับจำนวนรอบที่เล่นไปแล้ว
char board[BOARD_SIZE][BOARD_SIZE];     // กระดานเกม Othello
int dx[8] = {-1, -1, -1, 0, 1, 1, 1, 0}; // ใช้เดินทางใน 8 ทิศทาง (แนว x)
int dy[8] = {-1, 0, 1, 1, 1, 0, -1, -1}; // ใช้เดินทางใน 8 ทิศทาง (แนว y)

// ฟังก์ชันประกาศล่วงหน้า
void send_to_client(int client, const char* msg);
void broadcast(const char* msg);
void render_board(char* buffer);
int is_legal_move(int x, int y, char color);
int has_legal_move(char color);
void flip_disks(int x, int y, char color);
int board_full();
Score count_score();
void declare_winner();
void* client_handler(void* id_ptr);
void start_game();

// ส่งข้อความไปยัง client คนเดียว
void send_to_client(int client, const char* msg) {
    send(client_sockets[client], msg, strlen(msg), 0);
}

// ส่งข้อความไปยัง client ทั้งหมด
void broadcast(const char* msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        send_to_client(i, msg);
    }
}

// แสดงกระดาน Othello ลงใน buffer แบบเป็นข้อความ
void render_board(char* buffer) {
    strcpy(buffer, "\n   ");  // เว้นให้ตรงกับเลขแถว
    for (int col = 0; col < BOARD_SIZE; col++) {
        char col_label[5];
        snprintf(col_label, sizeof(col_label), "%d ", col);
        strcat(buffer, col_label);
    }
    strcat(buffer, "\n");

    // เพิ่มเนื้อหากระดานทีละแถว
    for (int row = 0; row < BOARD_SIZE; row++) {
        char line[BOARD_SIZE * 3 + 10];  // ขนาดสำหรับเลขและหมาก
        snprintf(line, sizeof(line), "%2d ", row);  // แสดงเลขแถวชิดขวาในช่อง

        for (int col = 0; col < BOARD_SIZE; col++) {
            char piece = board[row][col];
            snprintf(line + strlen(line), sizeof(line) - strlen(line), "%c ", piece);
        }

        strcat(buffer, line);
        strcat(buffer, "\n");
    }
}

// ตรวจสอบว่าการเดินตำแหน่ง (x,y) ถูกต้องหรือไม่
int is_legal_move(int x, int y, char color) {
    char opponent = (color == 'B') ? 'W' : 'B';
    if (board[y][x] != ' ') return 0; // ถ้ามีตัวแล้วเดินไม่ได้

    for (int d = 0; d < 8; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        int found_opponent = 0;

        while (nx >= 0 && ny >= 0 && nx < BOARD_SIZE && ny < BOARD_SIZE) {
            if (board[ny][nx] == opponent) {
                found_opponent = 1;
            } else if (board[ny][nx] == color && found_opponent) {
                return 1;  // ถูกต้อง ถ้ามีฝ่ายตรงข้ามขั้นกลาง แล้วเจอตัวเราอีกฝั่ง
            } else {
                break;
            }
            nx += dx[d];
            ny += dy[d];
        }
    }
    return 0;  // ไม่พบทิศที่ถูกต้อง
}

// ตรวจสอบว่าผู้เล่นมีการเดินที่ถูกต้องอย่างน้อย 1 ช่องไหม
int has_legal_move(char color) {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (is_legal_move(x, y, color)) return 1;
        }
    }
    return 0;
}

// พลิกตัวหมากฝ่ายตรงข้ามตามกติกา Othello
void flip_disks(int x, int y, char color) {
    char opponent = (color == 'B') ? 'W' : 'B';

    for (int d = 0; d < 8; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        int path[BOARD_SIZE][2], path_len = 0;

        while (nx >= 0 && ny >= 0 && nx < BOARD_SIZE && ny < BOARD_SIZE) {
            if (board[ny][nx] == opponent) {
                path[path_len][0] = nx;
                path[path_len][1] = ny;
                path_len++;
            } else if (board[ny][nx] == color) {
                for (int i = 0; i < path_len; i++) {
                    board[path[i][1]][path[i][0]] = color;
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

// ตรวจสอบว่ากระดานเต็มหรือยัง
int board_full() {
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            if (board[i][j] == ' ') return 0;
    return 1;
}

// นับจำนวนหมากของแต่ละฝ่าย
Score count_score() {
    Score s;
    s.black = 0;
    s.white = 0;

    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (board[y][x] == 'B') s.black++;
            else if (board[y][x] == 'W') s.white++;
        }
    }
    return s;
}

// ประกาศผลแพ้ชนะเมื่อจบเกม
void declare_winner() {
    Score s = count_score(); // นับคะแนนดำ-ขาว
    char msg[MAX_MSG];

    // บอกเกมจบและคะแนน
    snprintf(msg, sizeof(msg), "\n---- GAME END ----\nTotal Score\n%s (Black): %d\n%s (White): %d\n", 
            client_names[0], s.black, client_names[1], s.white);
    broadcast(msg);

    // แล้วจึงประกาศผลลัพธ์
    if (s.black > s.white) {
        snprintf(msg, sizeof(msg), "%s WIN !!!\n", client_names[0]);
    } else if (s.white > s.black) {
        snprintf(msg, sizeof(msg), "%s WIN !!!\n", client_names[1]);
    } else {
        snprintf(msg, sizeof(msg), "DRAW !!!\n");
    }
    broadcast(msg);
}

// ฟังก์ชันสำหรับรับผู้เล่นแต่ละคน
void* client_handler(void* id_ptr) {
    int id = *((int*)id_ptr);
    char buffer[MAX_MSG];

    // รับชื่อผู้เล่น
    recv(client_sockets[id], client_names[id], sizeof(client_names[id]), 0);
    printf("Player %d connected: %s\n", id + 1, client_names[id]);

    // ส่งข้อความต้อนรับ
    sprintf(buffer, "Hi %s\n", client_names[id]);
    send_to_client(id, buffer);

    // แจ้งว่าคุณเล่นเป็นฝ่ายใด
    sprintf(buffer, "You are playing as %s (%c)\n\n", (id == 0 ? "Black" : "White"), (id == 0 ? 'B' : 'W'));
    send_to_client(id, buffer);

    // ถ้าเกินผู้เล่น 2 คน ให้ปฏิเสธ
    if (id >= MAX_CLIENTS) {
        send_to_client(id, "The players are full\n");
        close(client_sockets[id]);
        return NULL;
    }

    // ถามผู้เล่นว่า "พร้อมหรือยัง"
    while (!ready_flags[id]) {
        send_to_client(id, "Are you ready to play? [y/n]: ");
        memset(buffer, 0, MAX_MSG);
        recv(client_sockets[id], buffer, MAX_MSG, 0);
        if (buffer[0] == 'y' || buffer[0] == 'Y') {
            ready_flags[id] = 1;
        }
    }

    return NULL;
}

// เริ่มเกม
void start_game() {
    memset(board, ' ', sizeof(board));
    board[3][3] = board[4][4] = 'W';
    board[3][4] = board[4][3] = 'B';

    char buffer[MAX_MSG];
    int x, y;
    
    // ตัวแปรเก็บเทิร์นปัจจุบัน และนับรอบ
    // current_turn = 0 คือ black, 1 คือ white
    turn_count = 1;

    while (1) {
        memset(buffer, 0, MAX_MSG);
        render_board(buffer);

        snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "\n---- Turn %d ----\n", turn_count);
        broadcast(buffer);

        // กำหนดสีหมากของผู้เล่นปัจจุบันและฝ่ายตรงข้าม
        char current_color = (current_turn == 0) ? 'B' : 'W';
        char opponent_color = (current_turn == 0) ? 'W' : 'B';

        // ตรวจสอบว่าฝ่ายปัจจุบันและฝ่ายตรงข้ามมี legal move หรือไม่
        int current_can_move = has_legal_move(current_color);
        int opponent_can_move = has_legal_move(opponent_color);

        // ถ้าทั้งสองฝ่ายไม่มี legal move ถือว่าจบเกม
        if (!current_can_move && !opponent_can_move) {
            broadcast("No one can move\nEnd this game\n");
            declare_winner();
            break;
        }

        // ถ้าฝ่ายปัจจุบันไม่มี legal move จะทำการแจ้งข้ามเทิร์น
        if (!current_can_move) {
            char skip_msg[MAX_MSG];
            snprintf(skip_msg, sizeof(skip_msg),
                     "%s have no legal move to place. Skip to %s's turn.\n",
                     client_names[current_turn], client_names[1 - current_turn]);
            broadcast(skip_msg);

            // แสดงบน server terminal ด้วย
            printf("Turn %d: %s have no legal move to place. Skip to %s's turn.\n",
                   turn_count, client_names[current_turn], client_names[1 - current_turn]);

            current_turn = 1 - current_turn;  // สลับเทิร์น
            continue;  // ข้ามรอบนี้
        }

        // ส่งกระดานและคะแนนให้ผู้เล่น
        Score s = count_score();
        char score_msg[MAX_MSG];
        snprintf(score_msg, sizeof(score_msg), "Score \n%s: %d, %s: %d\n",
                 client_names[0], s.black, client_names[1], s.white);
        broadcast(score_msg);

        // บอกผู้เล่นปัจจุบันให้วางหมาก
        char prompt_msg[100];
        snprintf(prompt_msg, sizeof(prompt_msg), "Your turn (%s) \nEnter your move [row column]: ",
                 client_names[current_turn]);
        send_to_client(current_turn, prompt_msg);

        memset(buffer, 0, MAX_MSG);
        recv(client_sockets[current_turn], buffer, MAX_MSG, 0);
        sscanf(buffer, "%d %d", &y, &x);

        if (is_legal_move(x, y, current_color)) {
            board[y][x] = current_color;
            flip_disks(x, y, current_color);

            // แสดงที่ client ว่าผู้เล่นวางหมากที่ตำแหน่งไหน พร้อมบอก turn
            char move_msg[MAX_MSG];
            snprintf(move_msg, sizeof(move_msg), "Turn %d: %s placed at (%d, %d)\n",
                     turn_count, client_names[current_turn], y, x);
            broadcast(move_msg);
            // แสดงที่ server ว่าผู้เล่นวางหมากที่ตำแหน่งไหนและบอก turn
            printf("Turn %d: %s placed at (%d, %d)\n", turn_count, client_names[current_turn], y, x);

            turn_count++;
            current_turn = 1 - current_turn;  // สลับเทิร์น
        } else {
            send_to_client(current_turn, "Illegal move. Try again.\n");

            // แสดงบน server terminal ว่าเดินผิด
            printf("Turn %d: Illegal move attempt by %s at (%d, %d)\n", turn_count, client_names[current_turn], y, x);
        }

        // ตรวจสอบว่ากระดานเต็มหรือไม่ ถ้าเต็มจบเกม
        if (board_full()) {
            // แสดงกระดานก่อนจะบอกว่ากระดานเต็ม
            char final_board[MAX_MSG];
            memset(final_board, 0, sizeof(final_board));
            render_board(final_board);
            broadcast(final_board);
            
            broadcast("Board is full\n");
            declare_winner();
            break;
        }
    }
}

int main() {
    int server_fd, client_fd, addr_len;
    struct sockaddr_in server_addr, client_addr;
    pthread_t threads[MAX_CLIENTS];
    int client_ids[MAX_CLIENTS];

    // สร้าง socket สำหรับรอรับ client
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, MAX_CLIENTS);
    printf("Server listening on port %d\nWaiting for players...\n", PORT);

    // รอรับ client และสร้าง thread สำหรับแต่ละคน
    for (int i = 0; i < MAX_CLIENTS; i++) {
        addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&addr_len);
        client_sockets[i] = client_fd;
        client_ids[i] = i;
        pthread_create(&threads[i], NULL, client_handler, &client_ids[i]);
    }

    // รอให้ทั้งสอง thread เสร็จ
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_join(threads[i], NULL);
    }

    // เริ่มเกม
    start_game();

    // ปิด socket
    for (int i = 0; i < MAX_CLIENTS; i++) {
        close(client_sockets[i]);
    }
    close(server_fd);
    return 0;
}