#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 3005

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

void sort_numbers(int arr[], int n, int asc) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if ((asc && arr[j] > arr[j + 1]) ||
                (!asc && arr[j] < arr[j + 1])) {
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

void execute(char *cmd, char *result_str) {
    int a, b;

    if (sscanf(cmd, "ADD %d %d", &a, &b) == 2) {
        sprintf(result_str, "%d", a + b); return;
    }
    if (sscanf(cmd, "SUB %d %d", &a, &b) == 2) {
        sprintf(result_str, "%d", a - b); return;
    }
    if (sscanf(cmd, "MUL %d %d", &a, &b) == 2) {
        sprintf(result_str, "%d", a * b); return;
    }
    if (sscanf(cmd, "DIV %d %d", &a, &b) == 2) {
        if (b == 0) sprintf(result_str, "Error");
        else sprintf(result_str, "%d", a / b);
        return;
    }

    if (strncmp(cmd, "SORT", 4) == 0) {
        int arr[50], n = 0;
        char order[10] = "ASC";
        char temp[200];
        strcpy(temp, cmd);

        char *token = strtok(temp, " ");
        token = strtok(NULL, " ");

        while (token != NULL) {
            if (!strcmp(token, "ASC") || !strcmp(token, "DESC")) {
                strcpy(order, token);
                break;
            }
            if (n < 50)
                arr[n++] = atoi(token);
            token = strtok(NULL, " ");
        }

        if (n == 0) {
            sprintf(result_str, "Invalid");
            return;
        }

        int asc = (strcmp(order, "ASC") == 0);
        sort_numbers(arr, n, asc);

        result_str[0] = '\0';
        for (int i = 0; i < n; i++) {
            char num[20];
            sprintf(num, "%d ", arr[i]);
            strcat(result_str, num);
        }
        return;
    }

    sprintf(result_str, "Invalid");
}

int main() {
    char worker_id[20];
    printf("Enter Worker ID: ");
    if (scanf("%19s", worker_id) != 1) {
        fprintf(stderr, "Failed to read worker ID.\n");
        return 1;
    }

    printf("Worker %s started (poll interval: 5s).\n", worker_id);

    while (1) {
        int sock;
        char buffer[1024] = {0};

        sock = connect_to_server();
        if (sock < 0) {
            printf("Worker %s exiting: server not reachable.\n", worker_id);
            break;
        }

        if (write(sock, "GET_TASK", 8) <= 0) {
            close(sock);
            sleep(5);
            continue;
        }

        ssize_t bytes = read(sock, buffer, sizeof(buffer) - 1);
        if (bytes <= 0) {
            close(sock);
            sleep(5);
            continue;
        }
        buffer[bytes] = '\0';

        if (strncmp(buffer, "SHUTDOWN", 8) == 0) {
            close(sock);
            printf("Worker %s exiting on server shutdown.\n", worker_id);
            break;
        }

        if (strncmp(buffer, "NO_TASK", 7) == 0 || strncmp(buffer, "WAIT_BATCH", 10) == 0) {
            close(sock);
            sleep(5);
            continue;
        }

        int id, client_sock;
        char cmd[200], result[200];

        if (sscanf(buffer, "%d %d %199[^\n]", &id, &client_sock, cmd) != 3) {
            close(sock);
            sleep(5);
            continue;
        }

        printf("Worker %s processing Task %d: %s\n", worker_id, id, cmd);

        execute(cmd, result);

        close(sock);

        sock = connect_to_server();
        if (sock < 0) {
            sleep(5);
            continue;
        }

        char msg[300];
        snprintf(msg, sizeof(msg), "RESULT %d %d %s %s", id, client_sock, worker_id, result);

        if (write(sock, msg, strlen(msg)) > 0)
            printf("Worker %s completed Task %d -> %s\n", worker_id, id, result);
        close(sock);

        sleep(5);
    }

    return 0;
}