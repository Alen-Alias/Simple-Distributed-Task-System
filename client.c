#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define PORT 3005
#define MAX 100

int connect_to_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;

    if (sock < 0)
        return -1;

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

int send_control_message(const char *message) {
    int sock;
    char response[64] = {0};

    sock = connect_to_server();
    if (sock < 0)
        return 0;

    if (write(sock, message, strlen(message)) <= 0) {
        close(sock);
        return 0;
    }

    ssize_t bytes = read(sock, response, sizeof(response) - 1);
    if (bytes > 0) {
        response[bytes] = '\0';
        printf("Server: %s\n", response);
    }

    close(sock);
    return 1;
}

int main() {
    while (1) {
        int n;
        printf("\nEnter number of tasks (0 to exit): ");
        if (scanf("%d", &n) != 1) {
            printf("Invalid number input.\n");
            while (getchar() != '\n');
            continue;
        }

        // clear input buffer
        while (getchar() != '\n');

        if (n == 0) {
            send_control_message("SHUTDOWN");
            break;
        }

        if (n < 0 || n > MAX) {
            printf("Please enter a number between 0 and %d.\n", MAX);
            continue;
        }

        int sockets[MAX];
        int submitted = 0;
        char control_msg[64];

        snprintf(control_msg, sizeof(control_msg), "BATCH_START %d", n);
        if (!send_control_message(control_msg)) {
            printf("Could not start batch on server.\n");
            continue;
        }

        // 🔹 PHASE 1: SEND ALL TASKS
        for (int i = 0; i < n; i++) {
            int sock;
            char task[200], msg[300];

            // proper input
            while (1) {
                printf("TASK %d: ", i + 1);

                if (fgets(task, sizeof(task), stdin) == NULL)
                    continue;

                task[strcspn(task, "\n")] = 0;

                if (strlen(task) == 0) {
                    printf("Empty input! Try again.\n");
                    continue;
                }
                break;
            }

            sock = connect_to_server();
            if (sock < 0)
                continue;

            snprintf(msg, sizeof(msg), "TASK %s", task);
            if (write(sock, msg, strlen(msg)) <= 0) {
                close(sock);
                continue;
            }

            struct timeval timeout;
            timeout.tv_sec = 30;
            timeout.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            // Keep the socket open to receive async result later.
            sockets[submitted++] = sock;
            printf("Sent TASK %d\n", i + 1);
        }

        if (submitted == 0) {
            printf("No tasks were submitted successfully.\n");
            continue;
        }

        if (submitted < n)
            printf("Only %d/%d tasks were submitted. Waiting for submitted tasks only.\n", submitted, n);

        if (!send_control_message("BATCH_DONE")) {
            printf("Could not send BATCH_DONE. Closing submitted sockets.\n");
            for (int i = 0; i < submitted; i++)
                close(sockets[i]);
            continue;
        }

        // PHASE 2: RECEIVE RESULTS
        printf("\n--- RESULTS ---\n");

        for (int i = 0; i < submitted; i++) {
            char buffer[2048] = {0};
            int total = 0;

            while (total < (int)sizeof(buffer) - 1) {
                ssize_t bytes = read(sockets[i], buffer + total, sizeof(buffer) - 1 - total);
                if (bytes <= 0)
                    break;

                total += (int)bytes;
                buffer[total] = '\0';

                char *result_pos = strstr(buffer, " Result = ");
                if (result_pos != NULL && strchr(result_pos, '\n') != NULL)
                    break;
            }

            if (total > 0) {
                char *result_line = strstr(buffer, " Result = ");

                if (result_line != NULL) {
                    while (result_line > buffer && *(result_line - 1) != '\n')
                        result_line--;

                    printf("%s", result_line);
                    if (strchr(result_line, '\n') == NULL)
                        printf("\n");
                } else {
                    printf("%s", buffer);
                    if (buffer[total - 1] != '\n')
                        printf("\n");
                }
            } else {
                printf("Task %d: no response received.\n", i + 1);
            }

            close(sockets[i]);
        }
    }

    printf("Exiting... shutdown signal sent.\n");
    return 0;
}