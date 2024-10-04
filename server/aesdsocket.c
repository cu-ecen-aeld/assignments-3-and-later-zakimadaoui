#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <netdb.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>

#define USE_AESD_CHAR_DEVICE 1 

#if USE_AESD_CHAR_DEVICE == 1
    #define OUT_FILE "/dev/aesd_char"
#else
    #define OUT_FILE "/var/tmp/aesdsocketdata"
#endif




int run = 1;
int sockfd = -1;
static pthread_mutex_t out_file_sync;

typedef struct {
    int fd;
    char *client_ip;
} connection_info;

struct thread_entry {
    pthread_t thread_id;
    TAILQ_ENTRY(thread_entry) entries;
};
typedef TAILQ_HEAD(thread_queue, thread_entry) threads_queue_head_t;

void *run_client_request(void *info);

void handle_signal(int signal) {
    syslog(LOG_DEBUG, "Caught signal. exiting");
    run = 0;
}

void timer_handler(union sigval arg) {
    FILE *file;
    char buffer[80]; // Buffer to hold the formatted time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    // Format the current time according to RFC 2822 format
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", tm_info);

    // Open the file in append mode
    file = fopen(OUT_FILE, "a");

    // Write the timestamp to the file
    pthread_mutex_lock(&out_file_sync);
    fprintf(file, "timestamp: %s\n", buffer);
    fclose(file); // Close the file
    pthread_mutex_unlock(&out_file_sync);
}

timer_t timer_id;
void setup_timer() {
    struct sigevent sev;
    struct itimerspec its;

    // Configure the timer to call 'timer_handler' when it expires
    sev.sigev_notify = SIGEV_THREAD;       // Notify using a separate thread
    sev.sigev_value.sival_ptr = &timer_id; // Pass the timer_id to the handler
    sev.sigev_notify_function = timer_handler;
    sev.sigev_notify_attributes = NULL;

    // Create the timer
    if (timer_create(CLOCK_REALTIME, &sev, &timer_id) == -1) {
        perror("timer_create failed");
        exit(EXIT_FAILURE);
    }

    // Configure the timer to trigger every 10 seconds
    its.it_value.tv_sec = 10; // Initial expiration in seconds
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 10; // Interval for periodic timer
    its.it_interval.tv_nsec = 0;

    // Start the timer
    if (timer_settime(timer_id, 0, &its, NULL) == -1) {
        perror("timer_settime failed");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv) {
    // ----------------------------------------------------------------------------
    openlog("aesdsocket", 0, LOG_USER);
    char server_ip[INET_ADDRSTRLEN];
#if USE_AESD_CHAR_DEVICE != 1
    remove(OUT_FILE);
#endif
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    // ----------------------------------------------------------------------------
    int deamon = 0;
    if (argc > 1 && !strcmp(argv[1], "-d")) {
        deamon = 1;
    };

    // ----------------------------------------------------------------------------
    // open tcp socket on port 9000, return -1 on failture
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // use IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo("0.0.0.0", "9000", &hints, &res);
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1) {
        freeaddrinfo(res);
        printf("Failed create socket: %s\n", strerror(errno));
        return -1;
    }
    inet_ntop(AF_INET, &(res->ai_addr), server_ip, INET_ADDRSTRLEN);

    // Set SO_REUSEADDR option
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "Failed to set SO_REUSEADDR: %s", strerror(errno));
        freeaddrinfo(res);
        close(sockfd);
        return -1;
    }

    // bind socket to port 9000
    if (bind(sockfd, res->ai_addr, res->ai_addrlen)) {
        syslog(LOG_ERR, "Failed to bind socket to port 9000: %s",
               strerror(errno));
        printf("Failed to bind socket to port 9000: %s\n", strerror(errno));
        freeaddrinfo(res);
        close(sockfd);
        return -1;
    }

    // make socket non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    freeaddrinfo(res);
    // ----------------------------------------------------------------------------

    if (deamon) {
        printf("running as deamon...\n");
        if (fork())
            exit(0);
    }

    // Declare the queue head
    struct thread_queue queue;
    // Initialize the head before use
    TAILQ_INIT(&queue);
    pthread_mutex_init(&out_file_sync, NULL);
#if USE_AESD_CHAR_DEVICE != 1
    setup_timer();
#endif

    while (run) {
        // Listen for and accept a connection
        if (listen(sockfd, 10)) {
            syslog(LOG_ERR, "Failed to listen on %s:9000 : %s", server_ip,
                   strerror(errno));
            break;
        }

        struct sockaddr client;
        socklen_t size = sizeof(client);

        int fd = -1;
    retry:
        fd = accept(sockfd, (struct sockaddr *)&client, &size);
        if (fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!run)
                break;
            // No incoming connections, continue loop
            goto retry;
        }

        char *client_ip;
        if (fd == -1) {
            syslog(LOG_ERR, "Failed to connect to client: %s", strerror(errno));
            break;
        } else {
            client_ip = malloc(INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &client, client_ip, INET_ADDRSTRLEN);
            syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);
        }

        // start the request in a new thread
        connection_info *info = malloc(sizeof(connection_info));
        info->fd = fd;
        info->client_ip = client_ip;
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, run_client_request, (void *)info);
        struct thread_entry *queue_entry = malloc(sizeof(struct thread_entry));
        queue_entry->thread_id = thread_id;
        TAILQ_INSERT_TAIL(&queue, queue_entry, entries);
    }

    // Join all threads by dequeuing their IDs
    struct thread_entry *t;
    while (!TAILQ_EMPTY(&queue)) {
        t = TAILQ_FIRST(&queue);          // Get the first element
        pthread_join(t->thread_id, NULL); // Join the thread
        TAILQ_REMOVE(&queue, t,
                     entries); // Remove the element from the queue
                               // Free the memory allocated for the element
        free(t);
    }

    close(sockfd);
#if USE_AESD_CHAR_DEVICE != 1
    remove(OUT_FILE);
#endif
    closelog();
    return 0;
}

void *run_client_request(void *info) {
    int fd = ((connection_info *)info)->fd;
    char *client_ip = ((connection_info *)info)->client_ip;

    uint8_t buffer[BUFSIZ];

    // read from client until a new line is received
    FILE *outf = fopen(OUT_FILE, "ab");
    while (1) {
        ssize_t bytes = recv(fd, buffer, BUFSIZ, 0);
        if (bytes <= 0) { // error or connection closed
            exit(-1);
        } else {
            int nl_found = 0;
            for (int i = 0; i < bytes; i++) {
                if (buffer[i] == '\n') {
                    bytes = i + 1;
                    nl_found = 1;
                    break;
                }
            }

            // write all received data or untill the new line character.
            pthread_mutex_lock(&out_file_sync);
            fwrite(buffer, 1, bytes, outf);
            fflush(outf);
            pthread_mutex_unlock(&out_file_sync);
            if (nl_found)
                break;
        }
    }
    fclose(outf);

    // write OUT_FILE contents back to client
    pthread_mutex_lock(&out_file_sync); // write access shouldn't be allowed
                                        // while we are reading
    outf = fopen(OUT_FILE, "rb");
    while (!feof(outf)) {
        size_t bytes = fread(buffer, 1, BUFSIZ, outf);
        send(fd, (void *)buffer, bytes, 0);
    }
    pthread_mutex_unlock(&out_file_sync); // write access shouldn't be allowed
                                          // while we are reading
    fclose(outf);

    close(fd); // close connection
    syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
    free(client_ip);
    free(info);
    return NULL;
}