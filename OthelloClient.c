// OthelloClient.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 1500
#define MAX_MSG 1024

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[MAX_MSG];
    char name[50];

    // สร้าง socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // ตั้งค่าที่อยู่เซิร์ฟเวอร์
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // เชื่อมต่อเซิร์ฟเวอร์
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        return 1;
    }

    // ส่งชื่อ
    printf("Enter your name: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = '\0';
    send(sockfd, name, strlen(name), 0);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) break;

        buffer[bytes_received] = '\0';
        printf("%s", buffer);

        // ถ้ามีข้อความให้พิมพ์ตอบ
        if (strstr(buffer, "ready") || strstr(buffer, "Enter x y")) {
            printf("> ");
            memset(buffer, 0, sizeof(buffer));
            fgets(buffer, sizeof(buffer), stdin);
            send(sockfd, buffer, strlen(buffer), 0);
        }

        // ตรวจจบเกม
        if (strstr(buffer, "WIN") || strstr(buffer, "DRAW") || strstr(buffer, "END")) {
            break;
        }
    }

    close(sockfd);
    return 0;
}
