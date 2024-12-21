#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <unistd.h>

#define PORT 2222
#define BLOCK_SIZE 4096

void receive_file(int client_socket, const char *output_filename) {
    FILE *file = fopen(output_filename, "wb");
    if (!file) {
        perror("Failed to open file");
        close(client_socket);
        return;
    }

    int block_size;
    char compressed_block[BLOCK_SIZE * 2];
    char decompressed_block[BLOCK_SIZE];

    while (1) {
        // Receive block size
        if (recv(client_socket, &block_size, sizeof(block_size), 0) <= 0) break;

        // End of file signal
        if (block_size == 0) break;

        // Receive compressed block
        recv(client_socket, compressed_block, block_size, 0);

        // Decompress block
        z_stream stream = {0};
        inflateInit(&stream);
        stream.next_in = (unsigned char *)compressed_block;
        stream.avail_in = block_size;
        stream.next_out = (unsigned char *)decompressed_block;
        stream.avail_out = BLOCK_SIZE;

        if (inflate(&stream, Z_FINISH) == Z_STREAM_END) {
            fwrite(decompressed_block, 1, stream.total_out, file);
        }
        inflateEnd(&stream);
    }

    fclose(file);
    close(client_socket);
    printf("File received and written to %s\n", output_filename);
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 1);

    printf("Server listening on port %d...\n", PORT);

    client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    printf("Client connected.\n");

    receive_file(client_socket, "output_file");
    close(server_fd);
    return 0;
}
