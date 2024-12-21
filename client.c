#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define PORT 2222
#define BLOCK_SIZE 4096

void send_file(int socket_fd, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        close(socket_fd);
        return;
    }

    char buffer[BLOCK_SIZE];
    char compressed_buffer[BLOCK_SIZE * 2];
    int read_size;

    while ((read_size = fread(buffer, 1, BLOCK_SIZE, file)) > 0) {
        // Compress block
        z_stream stream = {0};
        deflateInit(&stream, Z_BEST_SPEED);
        stream.next_in = (unsigned char *)buffer;
        stream.avail_in = read_size;
        stream.next_out = (unsigned char *)compressed_buffer;
        stream.avail_out = sizeof(compressed_buffer);

        deflate(&stream, Z_FINISH);
        deflateEnd(&stream);

        int compressed_size = stream.total_out;

        // Send block size
        send(socket_fd, &compressed_size, sizeof(compressed_size), 0);

        // Send compressed block
        send(socket_fd, compressed_buffer, compressed_size, 0);
    }

    // Send end of file signal
    int end_signal = 0;
    send(socket_fd, &end_signal, sizeof(end_signal), 0);

    fclose(file);
    close(socket_fd);
    printf("File sent successfully.\n");
}

int main() {
    int socket_fd;
    struct sockaddr_in server_addr;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        return 1;
    }

    printf("Connected to server.\n");
    send_file(socket_fd, "input_file");
    return 0;
}
