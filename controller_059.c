#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
volatile int udp_running = 0;

#define SERVER_IP "127.0.0.1"
#define TCP_PORT 9430
#define UDP_PORT 9501
#define BUFFER_SIZE 8192

void trim_newline(char *text)
{
    text[strcspn(text, "\r\n")] = '\0';
}

int recv_line(int fd, char *buffer, size_t size)
{
    size_t i = 0;

    while (i < size - 1) {
        char c;

        ssize_t n = recv(fd, &c, 1, 0);

        if (n == 0)
            return 0;

        if (n < 0)
            return -1;

        if (c == '\n') {
            buffer[i] = '\0';
            return 1;
        }

        buffer[i++] = c;
    }

    buffer[size - 1] = '\0';
    return 1;
}

int send_all(int fd, const void *data, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = send(fd,
                         (const char *)data + sent,
                         length - sent,
                         0);

        if (n <= 0)
            return -1;

        sent += n;
    }

    return 0;
}

void *udp_monitor(void *arg)
{
    (void)arg;

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_fd < 0) {
        perror("UDP socket");
        return NULL;
    }

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_fd,
             (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {

        perror("UDP bind");
        close(udp_fd);
        return NULL;
    }

    printf("\n[UDP MONITOR] Listening on UDP port %d...\n",
           UDP_PORT);

    while (udp_running) {

        char buffer[1024];

        ssize_t n = recvfrom(udp_fd,
                             buffer,
                             sizeof(buffer) - 1,
                             0,
                             NULL,
                             NULL);

        if (n <= 0)
            break;

        buffer[n] = '\0';

        printf("[UDP MONITOR] %s",
               buffer);

        fflush(stdout);
    }

    close(udp_fd);

    return NULL;
}

void print_response(int fd)
{
    char response[BUFFER_SIZE];

    int result = recv_line(fd,
                           response,
                           sizeof(response));

    if (result > 0)
        printf("%s\n", response);
    else
        printf("Connection closed or receive error.\n");
}

void send_command(int fd, const char *command)
{
    char buffer[BUFFER_SIZE];

    snprintf(buffer,
             sizeof(buffer),
             "%s\n",
             command);

    send_all(fd,
             buffer,
             strlen(buffer));

    print_response(fd);
}

void upload_file(int fd)
{
    char filename[256];

    printf("Enter filename to upload: ");
    scanf("%255s", filename);

    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        perror("File open");
        return;
    }

    fseek(fp, 0, SEEK_END);

    long filesize = ftell(fp);

    fseek(fp, 0, SEEK_SET);

    char command[512];

    snprintf(command,
             sizeof(command),
             "PUT %s %ld\n",
             filename,
             filesize);

    send_all(fd,
             command,
             strlen(command));

    char buffer[BUFFER_SIZE];

    long remaining = filesize;

    while (remaining > 0) {

        size_t chunk =
            remaining > BUFFER_SIZE
            ? BUFFER_SIZE
            : (size_t)remaining;

        size_t n = fread(buffer,
                         1,
                         chunk,
                         fp);

        if (n == 0)
            break;

        send_all(fd,
                 buffer,
                 n);

        remaining -= n;
    }

    fclose(fp);

    print_response(fd);
}

void download_file(int fd)
{
    char filename[256];

    printf("Enter filename to download: ");
    scanf("%255s", filename);

    char command[512];

    snprintf(command,
             sizeof(command),
             "GET %s\n",
             filename);

    send_all(fd,
             command,
             strlen(command));

    char header[BUFFER_SIZE];

    if (recv_line(fd,
                  header,
                  sizeof(header)) <= 0) {

        printf("Failed to receive response.\n");
        return;
    }

    printf("%s\n", header);

    if (strncmp(header,
                "OK FILE_SEND ",
                13) != 0) {

        return;
    }

    char remote_filename[256];
    long filesize;
    char sid[64];

    if (sscanf(header + 13,
               "%255s %ld %63s",
               remote_filename,
               &filesize,
               sid) != 3) {

        printf("Invalid GET response.\n");
        return;
    }

    FILE *fp = fopen(remote_filename, "wb");

    if (fp == NULL) {
        perror("File create");
        return;
    }

    char buffer[BUFFER_SIZE];

    long remaining = filesize;

    while (remaining > 0) {

        size_t chunk =
            remaining > BUFFER_SIZE
            ? BUFFER_SIZE
            : (size_t)remaining;

        ssize_t n = recv(fd,
                         buffer,
                         chunk,
                         0);

        if (n <= 0)
            break;

        fwrite(buffer,
               1,
               n,
               fp);

        remaining -= n;
    }

    fclose(fp);

    if (remaining == 0)
        printf("File downloaded successfully: %s\n",
               remote_filename);
    else
        printf("File download incomplete.\n");
}

int main(void)
{
    int sockfd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0) {

        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {

        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Connected to RemoteOps Agent.\n");
    printf("Server: %s:%d\n",
           SERVER_IP,
           TCP_PORT);

    char command[BUFFER_SIZE];

    while (1) {

        printf("\n");
        printf("========== RemoteOps Controller ==========\n");
        printf("1. AUTH\n");
        printf("2. SYSINFO\n");
        printf("3. LISTPROC\n");
        printf("4. EXEC\n");
        printf("5. PUT\n");
        printf("6. GET\n");
        printf("7. MONITOR START\n");
        printf("8. MONITOR STOP\n");
        printf("9. QUIT\n");
        printf("===========================================\n");

        printf("Enter choice: ");

        int choice;

        if (scanf("%d", &choice) != 1)
            break;

        switch (choice) {

            case 1:

                send_command(sockfd,
                             "AUTH OPS-0059");

                break;

            case 2:

                send_command(sockfd,
                             "SYSINFO");

                break;

            case 3:

                send_command(sockfd,
                             "LISTPROC");

                break;

            case 4: {

                char name[64];

                printf("Allowed commands:\n");
                printf("DATE\n");
                printf("UPTIME\n");
                printf("DISKFREE\n");
                printf("HOSTNAME\n");
                printf("WHOAMI\n");

                printf("Enter command: ");

                scanf("%63s", name);

                snprintf(command,
                         sizeof(command),
                         "EXEC %s",
                         name);

                send_command(sockfd,
                             command);

                break;
            }

            case 5:

                upload_file(sockfd);

                break;

            case 6:

                download_file(sockfd);

                break;

            case 7: {

                pthread_t thread;

                if (pthread_create(&thread,
                                   NULL,
                                   udp_monitor,
                                   NULL) != 0) {

                  printf("Failed to start UDP receiver.\n");
                  break;
                }
                sleep(1);

                snprintf (command,
                         sizeof(command),
                         "MONITOR START %d",
                         UDP_PORT);

                send_command(sockfd,
                              command);

                printf("UDP monitoring receiver started.\n");

                break;
            }

            case 8:

                send_command(sockfd,
                             "MONITOR STOP");

                break;

            case 9:

                send_command(sockfd,
                             "QUIT");

                close(sockfd);

                printf("Controller closed.\n");

                return 0;

            default:

                printf("Invalid choice.\n");
        }
    }

    close(sockfd);

    return 0;
}
