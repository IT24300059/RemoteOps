#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <dirent.h>

#define PORT 9430
#define SID_TAG "SID:9500"
#define AUTH_TOKEN "OPS-0059"
#define LOG_FILE "remoteops_IT24300059.log"
#define STORAGE_DIR "./agentfiles/IT24300059/"
#define BUFFER_SIZE 4096
#define MAX_FILE_SIZE (5 * 1024 * 1024)

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    int authenticated;
    int monitor_active;
    int udp_port;
    pthread_t monitor_thread;
} session_t;

/* ---------- Logging ---------- */

void log_event(const char *message)
{
    pthread_mutex_lock(&log_mutex);

    FILE *fp = fopen(LOG_FILE, "a");
    if (fp != NULL) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);

        fprintf(fp,
                "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
                t->tm_year + 1900,
                t->tm_mon + 1,
                t->tm_mday,
                t->tm_hour,
                t->tm_min,
                t->tm_sec,
                message);

        fclose(fp);
    }

    pthread_mutex_unlock(&log_mutex);
}

/* ---------- Send all bytes ---------- */

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

/* ---------- Receive exact number of bytes ---------- */

int recv_all(int fd, void *buffer, size_t length)
{
    size_t received = 0;

    while (received < length) {
        ssize_t n = recv(fd,
                         (char *)buffer + received,
                         length - received,
                         0);

        if (n <= 0)
            return -1;

        received += n;
    }

    return 0;
}

/* ---------- Receive one line ---------- */

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

/* ---------- Send protocol response ---------- */

void send_response(int fd, const char *response)
{
    char buffer[BUFFER_SIZE];

    snprintf(buffer,
             sizeof(buffer),
             "%s %s\n",
             response,
             SID_TAG);

    send_all(fd, buffer, strlen(buffer));
}

/* ---------- System information ---------- */

void get_sysinfo(char *output, size_t size)
{
    double load = 0.0;
    long mem_used = 0;
    long mem_total = 0;
    double uptime = 0.0;

    FILE *fp;

    /* CPU/load */
    fp = fopen("/proc/loadavg", "r");
    if (fp != NULL) {
        fscanf(fp, "%lf", &load);
        fclose(fp);
    }

    /* Memory */
    fp = fopen("/proc/meminfo", "r");
    if (fp != NULL) {
        char line[256];

        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1)
                continue;

            if (sscanf(line, "MemAvailable: %ld kB", &mem_used) == 1)
                break;
        }

        if (mem_total > 0)
            mem_used = mem_total - mem_used;

        mem_used /= 1024;

        fclose(fp);
    }

    /* Uptime */
    fp = fopen("/proc/uptime", "r");
    if (fp != NULL) {
        fscanf(fp, "%lf", &uptime);
        fclose(fp);
    }

    snprintf(output,
             size,
             "OK SYSINFO %.2f %ld %.0f",
             load,
             mem_used,
             uptime);
}

/* ---------- Process listing ---------- */

void get_processes(char *output, size_t size)
{
    FILE *fp;
    char line[256];

    snprintf(output, size, "OK PROCS ");

    fp = popen("ps -eo pid=,comm= --sort=pid | head -40", "r");

    if (fp == NULL) {
        strncat(output, "PROCESS_LIST_ERROR", size - strlen(output) - 1);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);

        while (len > 0 &&
               (line[len - 1] == '\n' ||
                line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (strlen(output) + len + 2 >= size)
            break;

        strncat(output, line, size - strlen(output) - 1);
        strncat(output, ",", size - strlen(output) - 1);
    }

    pclose(fp);
}

/* ---------- EXEC whitelist ---------- */

int allowed_command(const char *command)
{
    return strcmp(command, "DATE") == 0 ||
           strcmp(command, "UPTIME") == 0 ||
           strcmp(command, "DISKFREE") == 0 ||
           strcmp(command, "HOSTNAME") == 0 ||
           strcmp(command, "WHOAMI") == 0;
}

void execute_allowed(const char *command,
                     char *output,
                     size_t size)
{
    const char *shell_command = NULL;

    if (strcmp(command, "DATE") == 0)
        shell_command = "date";

    else if (strcmp(command, "UPTIME") == 0)
        shell_command = "uptime";

    else if (strcmp(command, "DISKFREE") == 0)
        shell_command = "df -h /";

    else if (strcmp(command, "HOSTNAME") == 0)
        shell_command = "hostname";

    else if (strcmp(command, "WHOAMI") == 0)
        shell_command = "whoami";

    FILE *fp = popen(shell_command, "r");

    if (fp == NULL) {
        snprintf(output, size, "EXECUTION_ERROR");
        return;
    }

    if (fgets(output, size, fp) == NULL)
        snprintf(output, size, "NO_OUTPUT");

    pclose(fp);

    output[strcspn(output, "\r\n")] = '\0';
}

/* ---------- Filename safety ---------- */

int valid_filename(const char *filename)
{
    if (filename == NULL || strlen(filename) == 0)
        return 0;

    if (strstr(filename, "..") != NULL)
        return 0;

    if (strchr(filename, '/') != NULL)
        return 0;

    if (strchr(filename, '\\') != NULL)
        return 0;

    return 1;
}

/* ---------- UDP monitoring ---------- */

void *monitor_thread_function(void *arg)
{
    session_t *session = (session_t *)arg;

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_fd < 0) {
        log_event("UDP socket creation failed");
        return NULL;
    }

    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(session->udp_port);
    udp_addr.sin_addr = session->client_addr.sin_addr;

    while (session->monitor_active) {

        char info[512];
        get_sysinfo(info, sizeof(info));

        char message[600];

        snprintf(message,
                 sizeof(message),
                 "SYSINFO %s\n",
                 info + 11);

        sendto(udp_fd,
               message,
               strlen(message),
               0,
               (struct sockaddr *)&udp_addr,
               sizeof(udp_addr));

        sleep(2);
    }

    close(udp_fd);

    log_event("UDP monitoring stopped");

    return NULL;
}

/* ---------- Client handler ---------- */

void *client_handler(void *arg)
{
    session_t *session = (session_t *)arg;

    char buffer[BUFFER_SIZE];

    char address[INET_ADDRSTRLEN];

    inet_ntop(AF_INET,
              &session->client_addr.sin_addr,
              address,
              sizeof(address));

    char logmsg[512];

    snprintf(logmsg,
             sizeof(logmsg),
             "Client connected from %s:%d",
             address,
             ntohs(session->client_addr.sin_port));

    log_event(logmsg);

    while (1) {

        int result = recv_line(session->client_fd,
                               buffer,
                               sizeof(buffer));

        if (result <= 0) {
            log_event("Client disconnected");
            break;
        }

        snprintf(logmsg,
                 sizeof(logmsg),
                 "Received command: %s",
                 buffer);

        log_event(logmsg);

        /* AUTH */

        if (strncmp(buffer, "AUTH ", 5) == 0) {

            char token[128];

            sscanf(buffer + 5, "%127s", token);

            if (strcmp(token, AUTH_TOKEN) == 0) {

                session->authenticated = 1;

                send_response(session->client_fd,
                              "OK AUTHENTICATED");

                log_event("Authentication successful");

            } else {

                send_response(session->client_fd,
                              "ERR 001 AUTH_FAILED");

                log_event("Authentication failed");
            }

            continue;
        }

        /* Commands below require authentication */

        if (!session->authenticated) {

            send_response(session->client_fd,
                          "ERR 001 AUTH_FAILED");

            continue;
        }

        /* SYSINFO */

        if (strcmp(buffer, "SYSINFO") == 0) {

            char info[512];

            get_sysinfo(info, sizeof(info));

            send_response(session->client_fd,
                          info + 3);

        }

        /* LISTPROC */

        else if (strcmp(buffer, "LISTPROC") == 0) {

            char processes[BUFFER_SIZE * 4];

            memset(processes, 0, sizeof(processes));

            get_processes(processes,
                          sizeof(processes));

            send_response(session->client_fd,
                          processes);

        }

        /* EXEC */

        else if (strncmp(buffer, "EXEC ", 5) == 0) {

            char command[64];

            sscanf(buffer + 5, "%63s", command);

            if (!allowed_command(command)) {

                send_response(session->client_fd,
                              "ERR 002 COMMAND_NOT_ALLOWED");

                log_event("EXEC command rejected");

            } else {

                char output[1024];
                char response[1200];

                execute_allowed(command,
                                output,
                                sizeof(output));

                snprintf(response,
                         sizeof(response),
                         "OK EXEC_RESULT %s",
                         output);

                send_response(session->client_fd,
                              response);
            }
        }

        /* PUT */

        else if (strncmp(buffer, "PUT ", 4) == 0) {

            char filename[256];
            long filesize;

            if (sscanf(buffer + 4,
                       "%255s %ld",
                       filename,
                       &filesize) != 2) {

                send_response(session->client_fd,
                              "ERR 003 BAD_PUT");

                continue;
            }

            if (!valid_filename(filename) ||
                filesize < 0 ||
                filesize > MAX_FILE_SIZE) {

                send_response(session->client_fd,
                              "ERR 004 FILE_TOO_LARGE");

                continue;
            }

            char path[512];

            snprintf(path,
                     sizeof(path),
                     STORAGE_DIR "%s",
                     filename);

            FILE *fp = fopen(path, "wb");

            if (fp == NULL) {

                send_response(session->client_fd,
                              "ERR 004 FILE_TOO_LARGE");

                continue;
            }

            char data[BUFFER_SIZE];
            long remaining = filesize;

            int error = 0;

            while (remaining > 0) {

                size_t chunk =
                    remaining > BUFFER_SIZE
                    ? BUFFER_SIZE
                    : (size_t)remaining;

                if (recv_all(session->client_fd,
                             data,
                             chunk) < 0) {

                    error = 1;
                    break;
                }

                fwrite(data, 1, chunk, fp);

                remaining -= chunk;
            }

            fclose(fp);

            if (error) {

                remove(path);
                log_event("PUT failed");

            } else {

                char response[512];

                snprintf(response,
                         sizeof(response),
                         "OK FILE_RECEIVED %s",
                         filename);

                send_response(session->client_fd,
                              response);

                log_event("File upload completed");
            }
        }

        /* GET */

        else if (strncmp(buffer, "GET ", 4) == 0) {

            char filename[256];

            sscanf(buffer + 4,
                   "%255s",
                   filename);

            if (!valid_filename(filename)) {

                send_response(session->client_fd,
                              "ERR 005 FILE_NOT_FOUND");

                continue;
            }

            char path[512];

            snprintf(path,
                     sizeof(path),
                     STORAGE_DIR "%s",
                     filename);

            FILE *fp = fopen(path, "rb");

            if (fp == NULL) {

                send_response(session->client_fd,
                              "ERR 005 FILE_NOT_FOUND");

                continue;
            }

            fseek(fp, 0, SEEK_END);

            long filesize = ftell(fp);

            fseek(fp, 0, SEEK_SET);

            char header[512];

            snprintf(header,
                     sizeof(header),
                     "OK FILE_SEND %s %ld %s\n",
                     filename,
                     filesize,
                     SID_TAG);

            send_all(session->client_fd,
                     header,
                     strlen(header));

            char data[BUFFER_SIZE];

            long remaining = filesize;

            while (remaining > 0) {

                size_t chunk =
                    remaining > BUFFER_SIZE
                    ? BUFFER_SIZE
                    : (size_t)remaining;

                size_t n = fread(data,
                                 1,
                                 chunk,
                                 fp);

                if (n == 0)
                    break;

                send_all(session->client_fd,
                         data,
                         n);

                remaining -= n;
            }

            fclose(fp);

            log_event("File download completed");
        }

        /* MONITOR START */

        else if (strncmp(buffer, "MONITOR START ", 14) == 0) {

            int udp_port;

            sscanf(buffer + 14,
                   "%d",
                   &udp_port);

            if (session->monitor_active) {

                send_response(session->client_fd,
                              "OK MONITOR_STARTED");

                continue;
            }

            session->udp_port = udp_port;
            session->monitor_active = 1;

            if (pthread_create(&session->monitor_thread,
                               NULL,
                               monitor_thread_function,
                               session) != 0) {

                session->monitor_active = 0;

                send_response(session->client_fd,
                              "ERR 003 MONITOR_FAILED");

            } else {

                send_response(session->client_fd,
                              "OK MONITOR_STARTED");

                log_event("UDP monitoring started");
            }
        }

        /* MONITOR STOP */

        else if (strcmp(buffer, "MONITOR STOP") == 0) {

            if (session->monitor_active) {

                session->monitor_active = 0;

                pthread_join(session->monitor_thread,
                             NULL);
            }

            send_response(session->client_fd,
                          "OK MONITOR_STOPPED");

            log_event("UDP monitoring stopped");
        }

        /* QUIT */

        else if (strcmp(buffer, "QUIT") == 0) {

            if (session->monitor_active) {

                session->monitor_active = 0;

                pthread_join(session->monitor_thread,
                             NULL);
            }

            send_response(session->client_fd,
                          "OK BYE");

            log_event("Client session closed");

            break;
        }

        /* UNKNOWN */

        else {

            send_response(session->client_fd,
                          "ERR 003 UNKNOWN_COMMAND");
        }
    }

    if (session->monitor_active) {

        session->monitor_active = 0;

        pthread_join(session->monitor_thread,
                     NULL);
    }

    close(session->client_fd);

    free(session);

    return NULL;
}

/* ---------- Main ---------- */

int main(void)
{
    mkdir("./agentfiles", 0755);
    mkdir(STORAGE_DIR, 0755);

    log_event("Agent server starting");

    int server_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {

        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {

        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("RemoteOps Agent started\n");
    printf("TCP Port : %d\n", PORT);
    printf("SID      : %s\n", SID_TAG);
    printf("Auth     : %s\n", AUTH_TOKEN);
    printf("Log      : %s\n", LOG_FILE);
    printf("Storage  : %s\n", STORAGE_DIR);
    printf("Waiting for Controllers...\n");

    log_event("Agent server listening on TCP port 9430");

    while (1) {

        session_t *session =
            malloc(sizeof(session_t));

        if (session == NULL)
            continue;

        socklen_t addr_len =
            sizeof(session->client_addr);

        session->client_fd =
            accept(server_fd,
                   (struct sockaddr *)&session->client_addr,
                   &addr_len);

        if (session->client_fd < 0) {

            free(session);
            continue;
        }

        session->authenticated = 0;
        session->monitor_active = 0;
        session->udp_port = 0;

        pthread_t thread;

        if (pthread_create(&thread,
                           NULL,
                           client_handler,
                           session) != 0) {

            close(session->client_fd);
            free(session);

        } else {

            pthread_detach(thread);
        }
    }

    close(server_fd);

    return 0;
}
