#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"     // IP ของเซิร์ฟเวอร์
#define PORT 15000                // หมายเลขพอร์ตที่เชื่อมต่อ
#define MAX_MSG 1024              // ขนาดสูงสุดของข้อความที่ส่ง/รับ

// ฟังก์ชันเชื่อมต่อเซิร์ฟเวอร์
// รับค่า socket descriptor กลับมา (ถ้าล้มเหลว return -1)
int connect_to_server() {
    int sockfd;
    struct sockaddr_in server_addr;

    // สร้าง socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // ตั้งค่า server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // เชื่อมต่อ
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// ฟังก์ชันส่งชื่อผู้เล่น
void send_player_name(int sockfd) {
    char name[50];

    printf("Enter your name: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = '\0';  // ลบ '\n' ออก

    if (send(sockfd, name, strlen(name), 0) < 0) {
        perror("Send name failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
}

// ตรวจสอบว่าเซิร์ฟเวอร์ส่งข้อความจบเกมหรือไม่
int is_game_over(const char* msg) {
    return strstr(msg, "WIN") || strstr(msg, "DRAW") || strstr(msg, "END");
}

// รับ input จากผู้เล่นและส่งกลับเซิร์ฟเวอร์
void handle_user_input(int sockfd) {
    char input[MAX_MSG];
    printf("> ");
    fgets(input, sizeof(input), stdin);
    send(sockfd, input, strlen(input), 0);
}

// วนลูปรับข้อความและตอบกลับเซิร์ฟเวอร์
void game_loop(int sockfd) {
    char buffer[MAX_MSG];
    int bytes_received;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);

        // ถ้าเซิร์ฟเวอร์ปิดการเชื่อมต่อ
        if (bytes_received <= 0) {
            printf("Disconnected from server.\n");
            break;
        }

        buffer[bytes_received] = '\0';
        printf("%s", buffer);  // แสดงข้อความจากเซิร์ฟเวอร์

        // ถ้ามีคำถามหรือถึงรอบให้เดิน
        if (strstr(buffer, "ready") || strstr(buffer, "Enter your move")) {
            handle_user_input(sockfd);
        }

        // ถ้าเกมจบ
        if (is_game_over(buffer)) {
            memset(buffer, 0, sizeof(buffer));
            recv(sockfd, buffer, sizeof(buffer), 0);  // รับคะแนนสุดท้าย
            printf("%s", buffer);
            break;
        }
    }
}

int main() {
    int sockfd = connect_to_server();  // สร้างและเชื่อมต่อ socket
    if (sockfd < 0) exit(EXIT_FAILURE);  // ถ้าเชื่อมไม่สำเร็จให้ออกโปรแกรม

    send_player_name(sockfd);  // ส่งชื่อผู้เล่นให้เซิร์ฟเวอร์
    game_loop(sockfd);         // วนรับข้อความ และโต้ตอบกับเกม

    close(sockfd);  // ปิดการเชื่อมต่อหลังจบเกม
    return 0;
}