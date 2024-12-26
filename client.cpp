#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <chrono>
#include <thread>

#define UDP_PORT 12345
#define TCP_PORT 12346
#define SERVER_ADDR "192.168.1.240"
#define BUFFER_SIZE 4096
#define END_SIGNAL -1 // Signal de fin
#define MAX_RETRIES 20 // Nombre de tentatives de ré-essai pour chaque paquet


template <typename T>
T my_min(T a, T b)
{
    return (a < b) ? a : b;
}

void showUsage()
{
    std::cout << "Usage: file_sender [options]\n";
    std::cout << "  -h, --help            Affiche l'aide\n";
    std::cout << "  -f, --file <file>      Fichier à envoyer\n";
    std::cout << "  -p, --port <port>      Port du serveur (défaut: 12345)\n";
    std::cout << "  -a, --address <ip>     Adresse IP du serveur (défaut: 127.0.0.1)\n";
    std::cout << "  -c, --compress         Active la compression\n";
    std::cout << "  -v, --verbose          Affiche des informations détaillées\n";
}

void showProgress(size_t bytesSent, size_t totalBytes, double elapsedTime)
{
    int progress = static_cast<int>((bytesSent * 100) / totalBytes);
    double transferRate = (bytesSent / 1024.0) / elapsedTime; // Ko/s

    // Convertir le taux de transfert en fonction de sa taille
    std::string rateUnit = "KB/s";
    if (transferRate >= 1024)
    {
        transferRate /= 1024; // Convertir en Mo/s
        rateUnit = "MB/s";
    }
    if (transferRate >= 1024)
    {
        transferRate /= 1024; // Convertir en Go/s
        rateUnit = "GB/s";
    }

    // Affichage du progrès et du taux de transfert
    std::cout << "\rProgress: [";
    for (int i = 0; i < 50; i++)
    {
        if (i < progress / 2)
            std::cout << "#";
        else
            std::cout << " ";
    }
    std::cout << "] " << progress << "%  Rate: "
              << std::fixed << std::setprecision(2) // Limite à 2 décimales
              << transferRate << " " << rateUnit;

    std::flush(std::cout);
}

// Fonction pour envoyer un fichier par UDP avec confirmation via TCP
void send_file_udp(int udp_socket, sockaddr_in &server_addr, const char *file_path, int tcp_socket)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file)
    {
        std::cerr << "Erreur d'ouverture du fichier" << std::endl;
        return;
    }

    char buffer[BUFFER_SIZE];
    socklen_t server_len = sizeof(server_addr);
    int packet_number = 0;

    // Transmettre le nom du fichier en premier paquet
    std::string file_name = std::string(file_path).substr(std::string(file_path).find_last_of("/\\") + 1);
    size_t name_length = file_name.size();

    if (name_length >= BUFFER_SIZE - sizeof(int))
    {
        std::cerr << "Nom de fichier trop long pour être transmis" << std::endl;
        return;
    }

    *((int *)buffer) = 0; // Paquet spécial pour le nom de fichier
    std::memcpy(buffer + sizeof(int), file_name.c_str(), name_length);

    ssize_t bytes_sent = sendto(udp_socket, buffer, sizeof(int) + name_length, 0, (struct sockaddr *)&server_addr, server_len);
    if (bytes_sent < 0)
    {
        std::cerr << "Erreur d'envoi du nom de fichier" << std::endl;
        return;
    }

    // Attendre la confirmation pour le nom de fichier
    int ack_seq_num;
    ssize_t n;
    int retries = 0;
    while (retries < MAX_RETRIES)
    {
        n = recv(tcp_socket, &ack_seq_num, sizeof(ack_seq_num), 0);
        if (n > 0 && ack_seq_num == 0)
    {
            break;
        }
        std::cerr << "Erreur de confirmation pour le nom de fichier, tentative " << retries + 1 << std::endl;
        retries++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Attendre un peu avant de réessayer
    }
    if (retries == MAX_RETRIES)
    {
        std::cerr << "Échec de la confirmation pour le nom de fichier après " << MAX_RETRIES << " tentatives" << std::endl;
        return;
    }

    while (file.read(buffer + sizeof(int), sizeof(buffer) - sizeof(int)) || file.gcount() > 0)
    {
        packet_number++;
        *((int *)buffer) = packet_number; // Ajouter le numéro de séquence au début du paquet

        size_t bytes_to_send = file.gcount() + sizeof(int);
        retries = 0;
        while (retries < MAX_RETRIES)
        {
        ssize_t bytes_sent = sendto(udp_socket, buffer, bytes_to_send, 0, (struct sockaddr *)&server_addr, server_len);
        if (bytes_sent < 0)
        {
                std::cerr << "Erreur d'envoi du paquet UDP #" << packet_number << ", tentative " << retries + 1 << std::endl;
                retries++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Attendre un peu avant de réessayer
                continue;
        }

        // Attente de la confirmation via TCP (ACK)
        int ack_seq_num;
        ssize_t n = recv(tcp_socket, &ack_seq_num, sizeof(ack_seq_num), 0);
            if (n > 0 && ack_seq_num == packet_number)
            {
                break; // ACK correct reçu
            }
            else
        {
                std::cerr << "Confirmation incorrecte ou échec de réception pour le paquet #" << packet_number << ", tentative " << retries + 1 << std::endl;
                retries++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Attendre un peu avant de réessayer
            }
        }

        if (retries == MAX_RETRIES)
        {
            std::cerr << "Échec de la réception de la confirmation pour le paquet #" << packet_number << " après " << MAX_RETRIES << " tentatives" << std::endl;
            break;
        }
    }

    // Envoyer un paquet de fin (signal de fin)
    *((int *)buffer) = END_SIGNAL;
    bytes_sent = sendto(udp_socket, buffer, sizeof(int), 0, (struct sockaddr *)&server_addr, server_len);
    if (bytes_sent < 0)
    {
        std::cerr << "Erreur d'envoi du signal de fin" << std::endl;
    }

    std::cout << "Envoi du fichier terminé." << std::endl;

    file.close();
    close(udp_socket);
    close(tcp_socket);
}

int main()
{
    // Création de socket UDP
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
    {
        std::cerr << "Erreur de création socket UDP" << std::endl;
        return -1;
    }

    // Création de socket TCP
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0)
    {
        std::cerr << "Erreur de création socket TCP" << std::endl;
        return -1;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr);

    // Connexion TCP
    sockaddr_in tcp_server_addr;
    memset(&tcp_server_addr, 0, sizeof(tcp_server_addr));
    tcp_server_addr.sin_family = AF_INET;
    tcp_server_addr.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, SERVER_ADDR, &tcp_server_addr.sin_addr);

    if (connect(tcp_socket, (struct sockaddr *)&tcp_server_addr, sizeof(tcp_server_addr)) < 0)
    {
        std::cerr << "Erreur de connexion TCP" << std::endl;
        return -1;
    }

    // Envoi du fichier par UDP avec confirmations TCP
    send_file_udp(udp_socket, server_addr, "fichier_binaire_1G.bin", tcp_socket);

    // Fermer les sockets après l'envoi
    close(udp_socket);
    close(tcp_socket);
    return 0;
}
