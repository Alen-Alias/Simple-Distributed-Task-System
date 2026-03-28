#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define PORT 3005
#define MAX 100

struct Task {
    int id;
    char command[200];
    int client_sock;
};

struct Task queue[MAX];
int front = -1, rear = -1;
int task_id = 1;
int dispatch_enabled = 0;
int shutdown_requested = 0;
int server_fd_global = -1;

pthread_mutex_t lock;

void send_text(int sock, const char *text) {
    write(sock, text, strlen(text));
}

// FIFO Queue
void enqueue(struct Task t) {
    pthread_mutex_lock(&lock);

    if (rear == MAX - 1) {
        pthread_mutex_unlock(&lock);
        return;
    }

    if (front == -1) front = 0;
    queue[++rear] = t;

    printf("Task %d added: %s\n", t.id, t.command);

    pthread_mutex_unlock(&lock);
}

struct Task dequeue() {
    pthread_mutex_lock(&lock);

    struct Task t;
    t.id = -1;

    if (front != -1 && front <= rear)
        t = queue[front++];

    if (front > rear)
        front = rear = -1;

    pthread_mutex_unlock(&lock);
    return t;
}

void *handle_client(void *arg) {
    int sock = *(int *)arg;
    free(arg);

    char buffer[1024] = {0};
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(sock);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    // CLIENT BATCH START
    if (strncmp(buffer, "SHUTDOWN", 8) == 0) {
        pthread_mutex_lock(&lock);
        shutdown_requested = 1;
        dispatch_enabled = 1;
        pthread_mutex_unlock(&lock);

        send_text(sock, "SHUTDOWN_OK");

        if (server_fd_global >= 0) {
            shutdown(server_fd_global, SHUT_RDWR);
            close(server_fd_global);
            server_fd_global = -1;
        }
    }

    // CLIENT BATCH START
    else if (strncmp(buffer, "BATCH_START", 11) == 0) {
        int expected = 0;
        sscanf(buffer, "BATCH_START %d", &expected);

        pthread_mutex_lock(&lock);
        if (shutdown_requested) {
            pthread_mutex_unlock(&lock);
            send_text(sock, "SERVER_DOWN");
            close(sock);
            return NULL;
        }
        dispatch_enabled = 0;
        pthread_mutex_unlock(&lock);

        printf("Batch start: expecting %d task(s).\n", expected);
        send_text(sock, "BATCH_OK");
    }

    // CLIENT BATCH DONE
    else if (strncmp(buffer, "BATCH_DONE", 10) == 0) {
        pthread_mutex_lock(&lock);
        if (shutdown_requested) {
            pthread_mutex_unlock(&lock);
            send_text(sock, "SERVER_DOWN");
            close(sock);
            return NULL;
        }
        dispatch_enabled = 1;
        pthread_mutex_unlock(&lock);

        printf("Batch done: assignment enabled.\n");
        send_text(sock, "ASSIGN_OK");
    }

    // CLIENT TASK
    else if (strncmp(buffer, "TASK", 4) == 0) {
        struct Task t;

        pthread_mutex_lock(&lock);
        if (shutdown_requested) {
            pthread_mutex_unlock(&lock);
            send_text(sock, "SERVER_DOWN");
            close(sock);
            return NULL;
        }
        t.id = task_id++;
        pthread_mutex_unlock(&lock);

        snprintf(t.command, sizeof(t.command), "%s", buffer + 5);
        t.client_sock = sock;

        enqueue(t);
        send_text(sock, "Task queued\n");
        return NULL; // keep socket open
    }

    // WORKER GET TASK
    else if (strncmp(buffer, "GET_TASK", 8) == 0) {
        int ready;
        int stop_now;

        pthread_mutex_lock(&lock);
        ready = dispatch_enabled;
        stop_now = shutdown_requested;
        pthread_mutex_unlock(&lock);

        if (stop_now) {
            send_text(sock, "SHUTDOWN");
            close(sock);
            return NULL;
        }

        if (!ready) {
            send_text(sock, "WAIT_BATCH");
            close(sock);
            return NULL;
        }

        struct Task t = dequeue();

        if (t.id == -1) {
            pthread_mutex_lock(&lock);
            dispatch_enabled = 0;
            pthread_mutex_unlock(&lock);

            send_text(sock, "NO_TASK");
        } else {
            char msg[300];
            snprintf(msg, sizeof(msg), "%d %d %s", t.id, t.client_sock, t.command);
            send_text(sock, msg);
            printf("Assigned Task %d\n", t.id);
        }
    }

    // WORKER RESULT
    else if (strncmp(buffer, "RESULT", 6) == 0) {
        int id, client_sock;
        char result[200], worker[50];

        if (sscanf(buffer, "RESULT %d %d %49s %199[^\n]", &id, &client_sock, worker, result) != 4) {
            close(sock);
            return NULL;
        }

        printf("Task %d done by %s -> %s\n", id, worker, result);

        char msg[300];
        snprintf(msg, sizeof(msg), "Task %d Result = %s (by %s)\n", id, result, worker);

        send_text(client_sock, msg);
        close(client_sock);
    }

    close(sock);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    pthread_mutex_init(&lock, NULL);

    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    server_fd_global = server_fd;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Server running...\n");

    while (1) {
        pthread_mutex_lock(&lock);
        if (shutdown_requested) {
            pthread_mutex_unlock(&lock);
            break;
        }
        pthread_mutex_unlock(&lock);

        int *new_sock = malloc(sizeof(int));
        if (new_sock == NULL) {
            perror("malloc");
            continue;
        }

        *new_sock = accept(server_fd, NULL, NULL);
        if (*new_sock < 0) {
            if (errno != EINTR && errno != EBADF)
                perror("accept");
            free(new_sock);

            pthread_mutex_lock(&lock);
            if (shutdown_requested) {
                pthread_mutex_unlock(&lock);
                break;
            }
            pthread_mutex_unlock(&lock);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, new_sock) != 0) {
            perror("pthread_create");
            close(*new_sock);
            free(new_sock);
            continue;
        }
        pthread_detach(tid);
    }

    printf("Server shutting down.\n");
    if (server_fd >= 0 && server_fd_global == server_fd)
        close(server_fd);
    return 0;
}