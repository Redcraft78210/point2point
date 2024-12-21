#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <unistd.h>

#define BLOCK_SIZE 4096

void print_progress_bar(size_t current, size_t total) {
    int bar_width = 50; // Width of the progress bar
    float progress = (float)current / total;
    int position = bar_width * progress;

    printf("\r[");
    for (int i = 0; i < bar_width; i++) {
        if (i < position)
            printf("=");
        else if (i == position)
            printf(">");
        else
            printf(" ");
    }
    printf("] %3.0f%%", progress * 100);
    fflush(stdout);
}

void send_error(int socket_fd, const char *message) {
    size_t message_length = strlen(message) + 1; // Include null terminator
    send(socket_fd, &message_length, sizeof(message_length), 0);
    send(socket_fd, message, message_length, 0);
}

void send_file(int socket_fd, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        close(socket_fd);
        return;
    }

    // Get file size for progress bar
    fseek(file, 0, SEEK_END);
    size_t total_size = ftell(file);
    rewind(file);

    // Send file name to server
    size_t filename_length = strlen(filename) + 1; // Include null terminator
    send(socket_fd, &filename_length, sizeof(filename_length), 0);
    send(socket_fd, filename, filename_length, 0);

    char buffer[BLOCK_SIZE];
    char compressed_buffer[BLOCK_SIZE * 2];
    size_t bytes_sent = 0;
    int read_size;

    printf("Sending file: %s (%zu bytes)\n", filename, total_size);

    z_stream stream = {0};
    deflateInit(&stream, Z_BEST_SPEED);

    while ((read_size = fread(buffer, 1, BLOCK_SIZE, file)) > 0) {
        // Compress block
        stream.next_in = (unsigned char *)buffer;
        stream.avail_in = read_size;
        stream.next_out = (unsigned char *)compressed_buffer;
        stream.avail_out = sizeof(compressed_buffer);

        // Compress the data
        deflate(&stream, Z_NO_FLUSH);

        int compressed_size = stream.total_out;

        // Send block size
        send(socket_fd, &compressed_size, sizeof(compressed_size), 0);

        // Send compressed block
        send(socket_fd, compressed_buffer, compressed_size, 0);

        bytes_sent += read_size;
        print_progress_bar(bytes_sent, total_size);

        // Reset the stream after each block
        deflateReset(&stream);
    }

    // Finish compression
    deflate(&stream, Z_FINISH);
    int compressed_size = stream.total_out;
    send(socket_fd, &compressed_size, sizeof(compressed_size), 0);
    send(socket_fd, compressed_buffer, compressed_size, 0);

    // Receive error message (if any)
    size_t message_length;
    if (recv(socket_fd, &message_length, sizeof(message_length), 0) > 0) {
        char message[256];
        if (recv(socket_fd, message, message_length, 0) > 0) {
            fprintf(stderr, "Error from server: %s\n", message);
            fclose(file);
            close(socket_fd);
            return;
        }
    }

    // Send end of file signal
    int end_signal = 0;
    send(socket_fd, &end_signal, sizeof(end_signal), 0);

    fclose(file);
    close(socket_fd);
    printf("\nFile sent successfully.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <IP_ADDRESS> <PORT> <FILE_TO_SEND>\n", argv[0]);
        return 1;
    }

    const char *ip_address = argv[1];
    int port = atoi(argv[2]);
    const char *filename = argv[3];

    int socket_fd;
    struct sockaddr_in server_addr;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(socket_fd);
        return 1;
    }

    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(socket_fd);
        return 1;
    }

    printf("Connected to server at %s:%d\n", ip_address, port);
    send_file(socket_fd, filename);

    return 0;
}
