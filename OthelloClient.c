#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // close()
#include <arpa/inet.h>  // socket

#define PORT 12345
#define BOARD_SIZE 8
#define BLACK 'B'
#define WHITE 'W'
#define EMPTY '.'

// แสดงกระดานที่ client รับมา
void print_board(char *board_str) {
    printf("  ");
    for(int i=0; i<BOARD_SIZE; i++) printf("%d ", i);
    printf("\n");
    for(int i=0; i<BOARD_SIZE; i++) {
        printf("%d ", i);
        for(int j=0; j<BOARD_SIZE; j++) {
            printf("%c ", board_str[i*BOARD_SIZE + j]);
        }
        printf("\n");
    }
}

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;

    // สร้าง socket
    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error\n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // แปลง IP จาก string เป็น network address
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Invalid address\n");
        return -1;
    }

    // เชื่อมต่อ server
    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection Failed\n");
        return -1;
    }

    // ส่งชื่อผู้เล่น
    char name[20];
    printf("Enter your name: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = '\0'; // ตัด \n ออก
    send(sock, name, strlen(name)+1, 0);

    // รับชื่อคู่ต่อสู้
    char opponent_name[20];
    int valread = recv(sock, opponent_name, sizeof(opponent_name), 0);
    opponent_name[valread] = '\0';
    printf("Your opponent is: %s\n", opponent_name);

    char board_str[BOARD_SIZE * BOARD_SIZE + 1];
    char buffer[100];

    while(1) {
        // รับกระดาน
        int valread = recv(sock, board_str, sizeof(board_str), 0);
        board_str[valread] = '\0';

        // รับสถานะ
        int len = recv(sock, buffer, sizeof(buffer), 0);
        buffer[len] = '\0';

        print_board(board_str);

        if(strcmp(buffer, "YOUR_MOVE") == 0) {
            printf("Your move (row col): ");
            fgets(buffer, sizeof(buffer), stdin);
            send(sock, buffer, strlen(buffer)+1, 0);
        } else if(strcmp(buffer, "WAIT") == 0) {
            printf("Waiting for opponent...\n");
        } else if(strncmp(buffer, "INVALID_MOVE", 12) == 0) {
            printf("Invalid move, try again.\n");
        } else if(strncmp(buffer, "GAME_OVER", 9) == 0) {
            printf("%s\n", buffer + 10);
            break;
        }
    }

    close(sock);
    return 0;
}
