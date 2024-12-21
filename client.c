#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT 2222
#define BLOCK_SIZE 4096

// Fonction pour recevoir un message d'erreur du serveur
void receive_error_message(int server_socket) {
    size_t message_length;

    // Recevoir la longueur du message
    if (recv(server_socket, &message_length, sizeof(message_length), 0) <= 0) {
        perror("Failed to receive message length");
        return;
    }

    // Allouer de la mémoire pour le message d'erreur
    char *error_message = (char *)malloc(message_length);
    if (!error_message) {
        perror("Memory allocation failed");
        return;
    }

    // Recevoir le message d'erreur
    if (recv(server_socket, error_message, message_length, 0) <= 0) {
        perror("Failed to receive error message");
        free(error_message);
        return;
    }

    // Afficher le message d'erreur
    printf("Server error: %s\n", error_message);
    free(error_message);
}

// Fonction pour envoyer le fichier au serveur
void send_file(int server_socket, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    // Obtenir la taille totale du fichier
    fseek(file, 0, SEEK_END);
    long total_file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Envoyer le nom du fichier
    size_t filename_length = strlen(filename) + 1;
    send(server_socket, &filename_length, sizeof(filename_length), 0);
    send(server_socket, filename, filename_length, 0);

    // Recevoir et gérer un message d'erreur du serveur
    receive_error_message(server_socket);

    // Si le serveur a un message d'erreur, ne pas continuer
    printf("File not sent.\n");
    fclose(file);
    return;

    char block[BLOCK_SIZE];
    int bytes_read;
    size_t total_bytes_sent = 0;

    // Compression et envoi du fichier en blocs
    z_stream stream = {0};
    if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
        perror("Compression initialization failed");
        fclose(file);
        return;
    }

    while ((bytes_read = fread(block, 1, BLOCK_SIZE, file)) > 0) {
        stream.next_in = (unsigned char *)block;
        stream.avail_in = bytes_read;

        // Compresser le bloc
        char compressed_block[BLOCK_SIZE * 2];
        stream.next_out = (unsigned char *)compressed_block;
        stream.avail_out = sizeof(compressed_block);

        if (deflate(&stream, Z_NO_FLUSH) != Z_OK) {
            perror("Compression error");
            break;
        }

        size_t compressed_size = sizeof(compressed_block) - stream.avail_out;

        // Envoyer la taille du bloc compressé
        send(server_socket, &compressed_size, sizeof(compressed_size), 0);

        // Envoyer le bloc compressé
        send(server_socket, compressed_block, compressed_size, 0);

        total_bytes_sent += compressed_size;

        // Calculer le pourcentage d'envoi
        float progress = ((float)total_bytes_sent / total_file_size) * 100;

        // Afficher la barre de progression et le taux d'envoi
        printf("\rProgress: [");
        int progress_blocks = (int)(progress / 2); // Divisé par 2 pour une largeur plus raisonnable
        for (int i = 0; i < 50; i++) {
            if (i < progress_blocks)
                printf("#");
            else
                printf(" ");
        }
        printf("] %.2f%% | Sent: %.2f KB/s", progress, (total_bytes_sent / 1024.0));

        fflush(stdout); // Forcer l'affichage de la barre de progression
    }

    // Envoyer le dernier bloc avec la taille de 0 pour indiquer la fin du fichier
    int block_size = 0;
    send(server_socket, &block_size, sizeof(block_size), 0);

    deflateEnd(&stream);
    fclose(file);

    printf("\nFile sent successfully: %s (%zu bytes)\n", filename, total_bytes_sent);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <file_to_send>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    const char *filename = argv[2];

    // Créer la socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Adresse du serveur
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convertir l'adresse IP en forme binaire
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Se connecter au serveur
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s on port %d...\n", server_ip, PORT);

    send_file(server_socket, filename);

    // Fermer la socket après l'envoi du fichier
    close(server_socket);

    return 0;
}
