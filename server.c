#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <unistd.h>
#include <sys/stat.h>

#define PORT 2222
#define BLOCK_SIZE 4096
#define DEFAULT_DIR "uploads"

// Function to ensure the destination directory exists
void ensure_directory_exists(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == -1)
    {
        if (mkdir(dir, 0755) < 0)
        {
            perror("Failed to create directory");
            exit(EXIT_FAILURE);
        }
    }
    else if (!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "Error: %s exists but is not a directory.\n", dir);
        exit(EXIT_FAILURE);
    }
}

// Function to check if the file exists on the server
int file_exists(const char *file_path)
{
    struct stat st;
    return stat(file_path, &st) == 0;
}

// Function to send error message to the client
void send_error(int client_socket, const char *message)
{
    size_t message_length = strlen(message) + 1; // Include null terminator
    send(client_socket, &message_length, sizeof(message_length), 0);
    send(client_socket, message, message_length, 0);
}

// Fonction pour recevoir et décompresser le fichier
void receive_file(int client_socket, const char *dest_dir)
{
    char filename[256];
    size_t filename_length;

    // Recevoir la longueur du nom de fichier
    if (recv(client_socket, &filename_length, sizeof(filename_length), 0) <= 0)
    {
        perror("Failed to receive filename length");
        close(client_socket);
        return;
    }

    // Recevoir le nom du fichier
    if (recv(client_socket, filename, filename_length, 0) <= 0)
    {
        perror("Failed to receive filename");
        close(client_socket);
        return;
    }
    filename[filename_length - 1] = '\0'; // Assurer la terminaison nulle

    // Construire le chemin complet du fichier
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", dest_dir, filename);

    // Vérifier si le fichier existe déjà
    if (file_exists(full_path))
    {
        // Envoyer un message d'erreur au client si le fichier existe déjà
        send_error(client_socket, "File already exists.");
        close(client_socket);
        return;
    }

    // Ouvrir le fichier pour l'écriture
    FILE *file = fopen(full_path, "wb");
    if (!file)
    {
        perror("Failed to open file for writing");
        close(client_socket);
        return;
    }

    printf("Receiving file: %s\n", full_path);

    int block_size;
    char compressed_block[BLOCK_SIZE * 2];
    char decompressed_block[BLOCK_SIZE];
    size_t total_bytes_received = 0;

    // Initialiser le flux de décompression
    z_stream stream = {0};
    if (inflateInit(&stream) != Z_OK)
    {
        perror("Failed to initialize zlib stream");
        fclose(file);
        close(client_socket);
        return;
    }

    while (1)
    {
        // Recevoir la taille du bloc
        ssize_t bytes_received = recv(client_socket, &block_size, sizeof(block_size), 0);
        if (bytes_received <= 0)
        {
            perror("Failed to receive block size or connection closed");
            break;
        }

        // Signal de fin de fichier (taille de bloc 0)
        if (block_size == 0)
            break;

        // Recevoir le bloc compressé
        bytes_received = recv(client_socket, compressed_block, block_size, 0);
        if (bytes_received != block_size)
        {
            perror("Failed to receive complete block");
            break;
        }
        printf("Received block of size: %d\n", block_size);

        // Décompresser le bloc
        stream.next_in = (unsigned char *)compressed_block;
        stream.avail_in = block_size;
        stream.next_out = (unsigned char *)decompressed_block;
        stream.avail_out = BLOCK_SIZE;

        int ret = inflate(&stream, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END)
        {
            fprintf(stderr, "Decompression error: %d\n", ret);
            break;
        }

        size_t decompressed_size = BLOCK_SIZE - stream.avail_out;
        fwrite(decompressed_block, 1, decompressed_size, file);
        total_bytes_received += decompressed_size;

        // Si c'est la fin du flux de données
        if (ret == Z_STREAM_END)
            break;
    }

    // Nettoyer
    inflateEnd(&stream);

    fclose(file);
    close(client_socket);
    printf("File received successfully: %s (%zu bytes)\n", full_path, total_bytes_received);
}

int main(int argc, char *argv[])
{
    const char *dest_dir = DEFAULT_DIR;

    if (argc == 2)
    {
        dest_dir = argv[1];
    }

    ensure_directory_exists(dest_dir);

    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Création du socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Permettre la réutilisation immédiate de l'adresse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Initialisation de l'adresse serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Liaison du socket à l'adresse et au port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Écoute des connexions entrantes
    if (listen(server_fd, 1) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);
    printf("Files will be saved to: %s\n", dest_dir);

    client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_socket < 0)
    {
        perror("Accept failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Client connected.\n");

    receive_file(client_socket, dest_dir);

    // Fermer proprement la connexion avant de fermer le socket
    shutdown(client_socket, SHUT_RDWR); // SHUT_RDWR indique que la connexion est terminée pour lecture et écriture
    close(client_socket);               // Puis fermer le socket proprement
    return 0;
}
