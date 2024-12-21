#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <unistd.h>

#define PORT 2222
#define BLOCK_SIZE 4096

void receive_file(int client_socket) {
    char filename[256];
    size_t filename_length;

    // Receive filename length
    if (recv(client_socket, &filename_length, sizeof(filename_length), 0) <= 0) {
        perror("Failed to receive filename length");
        close(client_socket);
        return;
    }

    // Receive filename
    if (recv(client_socket, filename, filename_length, 0) <= 0) {
        perror("Failed to receive filename");
        close(client_socket);
        return;
    }
    filename[filename_length - 1] = '\0'; // Ensure null termination

    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Failed to open file for writing");
        close(client_socket);
        return;
    }

    printf("Receiving file: %s\n", filename);

    int block_size;
    char compressed_block[BLOCK_SIZE * 2];
    char decompressed_block[BLOCK_SIZE];
    size_t total_bytes_received = 0;

    while (1) {
        // Receive block size
        if (recv(client_socket, &block_size, sizeof(block_size), 0) <= 0) break;

        // End of file signal
        if (block_size == 0) break;

        // Receive compressed block
        ssize_t bytes_received = recv(client_socket, compressed_block, block_size, 0);
        if (bytes_received != block_size) {
            perror("Failed to receive complete block");
            break;
        }

        // Decompress block
        z_stream stream = {0};
        inflateInit(&stream);
        stream.next_in = (unsigned char *)compressed_block;
        stream.avail_in = block_size;
        stream.next_out = (unsigned char *)decompressed_block;
        stream.avail_out = BLOCK_SIZE;

        if (inflate(&stream, Z_FINISH) == Z_STREAM_END) {
            fwrite(decompressed_block, 1, stream.total_out, file);
            total_bytes_received += stream.total_out;
        } else {
            perror("Decompression error");
            inflateEnd(&stream);
            break;
        }
        inflateEnd(&stream);
    }

    fclose(file);
    close(client_socket);
    printf("File received successfully: %s (%zu bytes)\n", filename, total_bytes_received);
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_socket < 0) {
        perror("Accept failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Client connected.\n");

    receive_file(client_socket);

    close(server_fd);
    return 0;
}
