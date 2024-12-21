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
#define MAX_FILENAME_LENGTH 255

// Fonction pour s'assurer que le répertoire de destination existe
void ensure_directory_exists(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == -1)
    {
        if (mkdir(dir, 0755) < 0)
        {
            perror("Échec de la création du répertoire");
            exit(EXIT_FAILURE);
        }
    }
    else if (!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "Erreur : %s existe mais n'est pas un répertoire.\n", dir);
        exit(EXIT_FAILURE);
    }
}

// Fonction pour vérifier si le fichier existe déjà sur le serveur
int file_exists(const char *file_path)
{
    struct stat st;
    return stat(file_path, &st) == 0;
}

// Fonction pour envoyer un message d'erreur au client
void send_error(int client_socket, const char *message)
{
    size_t message_length = strlen(message) + 1; // Inclure le terminator nul
    send(client_socket, &message_length, sizeof(message_length), 0);
    send(client_socket, message, message_length, 0);
}

// Fonction pour recevoir et décompresser le fichier
void receive_file(int client_socket, const char *dest_dir)
{
    char filename[MAX_FILENAME_LENGTH + 1]; // Permet l'espace pour le terminator nul
    size_t filename_length;

    // Recevoir la longueur du nom de fichier
    if (recv(client_socket, &filename_length, sizeof(filename_length), 0) <= 0)
    {
        perror("Échec de la réception de la longueur du nom du fichier");
        close(client_socket);
        return;
    }

    // Vérifier la longueur du nom de fichier
    if (filename_length > MAX_FILENAME_LENGTH)
    {
        send_error(client_socket, "Le nom du fichier est trop long.");
        close(client_socket);
        return;
    }

    // Recevoir le nom du fichier
    if (recv(client_socket, filename, filename_length, 0) <= 0)
    {
        perror("Échec de la réception du nom du fichier");
        close(client_socket);
        return;
    }
    filename[filename_length - 1] = '\0'; // S'assurer de la terminaison nulle

    // Vérification de la traversée de répertoire dans le nom du fichier
    if (strstr(filename, "..") != NULL)
    {
        send_error(client_socket, "Nom de fichier invalide.");
        close(client_socket);
        return;
    }

    // Construire le chemin complet du fichier
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", dest_dir, filename);

    // Vérifier si le fichier existe déjà
    if (file_exists(full_path))
    {
        send_error(client_socket, "Le fichier existe déjà.");
        close(client_socket);
        return;
    }

    // Ouvrir le fichier pour l'écriture
    FILE *file = fopen(full_path, "wb");
    if (!file)
    {
        perror("Échec de l'ouverture du fichier pour l'écriture");
        close(client_socket);
        return;
    }

    printf("Réception du fichier : %s\n", full_path);

    int block_size;
    char compressed_block[BLOCK_SIZE * 2];
    char decompressed_block[BLOCK_SIZE];
    size_t total_bytes_received = 0;

    // Initialiser le flux de décompression
    z_stream stream = {0};
    if (inflateInit(&stream) != Z_OK)
    {
        perror("Échec de l'initialisation du flux zlib");
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
            perror("Échec de la réception de la taille du bloc ou connexion fermée");
            break;
        }

        // Signal de fin de fichier (taille du bloc 0)
        if (block_size == 0)
            break;

        // Recevoir le bloc compressé
        bytes_received = recv(client_socket, compressed_block, block_size, 0);
        if (bytes_received != block_size)
        {
            perror("Échec de la réception complète du bloc");
            break;
        }
        printf("Bloc reçu de taille : %d\n", block_size);

        // Décompresser le bloc
        stream.next_in = (unsigned char *)compressed_block;
        stream.avail_in = block_size;
        stream.next_out = (unsigned char *)decompressed_block;
        stream.avail_out = BLOCK_SIZE;

        int ret = inflate(&stream, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END)
        {
            fprintf(stderr, "Erreur de décompression : %d\n", ret);
            break;
        }

        size_t decompressed_size = BLOCK_SIZE - stream.avail_out;
        if (fwrite(decompressed_block, 1, decompressed_size, file) != decompressed_size)
        {
            perror("Échec de l'écriture dans le fichier");
            break;
        }
        total_bytes_received += decompressed_size;

        // Si c'est la fin du flux
        if (ret == Z_STREAM_END)
            break;
    }

    // Nettoyer
    inflateEnd(&stream);

    fclose(file);
    close(client_socket);
    printf("Fichier reçu avec succès : %s (%zu octets)\n", full_path, total_bytes_received);
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

    // Créer la socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("Échec de la création de la socket");
        exit(EXIT_FAILURE);
    }

    // Permettre la réutilisation immédiate de l'adresse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Échec de setsockopt(SO_REUSEADDR)");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Initialiser l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Lier la socket à l'adresse et au port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Échec de bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Écouter les connexions entrantes
    if (listen(server_fd, 1) < 0)
    {
        perror("Échec de listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Serveur en écoute sur le port %d...\n", PORT);
    printf("Les fichiers seront enregistrés dans : %s\n", dest_dir);

    client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_socket < 0)
    {
        perror("Échec de accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Client connecté.\n");

    receive_file(client_socket, dest_dir);

    // Fermer la connexion et la socket serveur
    shutdown(client_socket, SHUT_RDWR); 
    close(client_socket);  // Fermer la socket du client
    close(server_fd);      // Fermer la socket serveur
    return 0;
}
