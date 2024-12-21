#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <unistd.h>
#include <fcntl.h>

#define PORT 2222
#define BLOCK_SIZE 4096

// Function to receive an error message from the server
void receive_error_message(int server_socket) {
    size_t message_length;
    
    // Receive message length
    if (recv(server_socket, &message_length, sizeof(message_length), 0) <= 0) {
        perror("Failed to receive message length");
        return;
    }

    // Allocate memory for the error message
    char *error_message = (char *)malloc(message_length);
    if (!error_message) {
        perror("Memory allocation failed");
        return;
    }

    // Receive the error message
    if (recv(server_socket, error_message, message_length, 0) <= 0) {
        perror("Failed to receive error message");
        free(error_message);
        return;
    }

    // Print the error message
    printf("Server error: %s\n", error_message);
    free(error_message);
}

// Function to send the file to the server
void send_file(int server_socket, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    // Send filename
    size_t filename_length = strlen(filename) + 1;
    send(server_socket, &filename_length, sizeof(filename_length), 0);
    send(server_socket, filename, filename_length, 0);

    // Receive and handle any error message from the server
    receive_error_message(server_socket);

    // If the server has an error message, don't continue
    printf("File not sent.\n");
    fclose(file);
    return;

    char block[BLOCK_SIZE];
    int bytes_read;
    size_t total_bytes_sent = 0;

    // Compress and send the file in blocks
    z_stream stream = {0};
    deflateInit(&stream, Z_DEFAULT_COMPRESSION);

    while ((bytes_read = fread(block, 1, BLOCK_SIZE, file)) > 0) {
        stream.next_in = (unsigned char *)block;
        stream.avail_in = bytes_read;

        // Compress the block
        char compressed_block[BLOCK_SIZE * 2];
        stream.next_out = (unsigned char *)compressed_block;
        stream.avail_out = sizeof(compressed_block);

        if (deflate(&stream, Z_NO_FLUSH) != Z_OK) {
            perror("Compression error");
            break;
        }

        size_t compressed_size = sizeof(compressed_block) - stream.avail_out;

        // Send the size of the compressed block
        send(server_socket, &compressed_size, sizeof(compressed_size), 0);

        // Send the compressed block
        send(server_socket, compressed_block, compressed_size, 0);

        total_bytes_sent += compressed_size;
    }

    // Send the last block with block size 0 to indicate the end of the file
    int block_size = 0;
    send(server_socket, &block_size, sizeof(block_size), 0);

    deflateEnd(&stream);
    fclose(file);

    printf("File sent successfully: %s (%zu bytes)\n", filename, total_bytes_sent);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <file_to_send>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    const char *filename = argv[2];

    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convert IP address to binary form
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s on port %d...\n", server_ip, PORT);

    send_file(server_socket, filename);

    // Close the socket after sending the file
    close(server_socket);

    return 0;
}
