#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <signal.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#define UDP_PORT 12345
#define TCP_PORT 12346
#define BUFFER_SIZE 4096
#define OUTPUT_FILE "received_file.txt"
#define END_SIGNAL -1  // Signal de fin
#define MAX_RETRIES 20 // Nombre de tentatives pour chaque confirmation TCP

namespace
{
    volatile sig_atomic_t quitok = false;
    void handle_break(int a)
    {
        if (a == SIGINT)
            quitok = true;
    }
}

// Fonction pour gérer la réception des paquets UDP et écrire dans le fichier
void handle_udp(int udp_socket, int tcp_socket, bool &udp_is_closed)
{
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    std::string file_name;

    // Set pour suivre les numéros de séquence des paquets reçus
    std::set<int> received_sequence_numbers;

    FILE *output_file = nullptr;
    while (true)
    {
        int n = recvfrom(udp_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (n > 0)
        {
            // Extraire le numéro de séquence du paquet
            int seq_num = *((int *)buffer);

            // Si le signal de fin est reçu, terminer l'envoi
            if (seq_num == END_SIGNAL)
            {
                std::cout << "Serveur UDP : fin de l'envoi des paquets" << std::endl;
                udp_is_closed = true;
                break;
            }

            // Si c'est le premier paquet (nom du fichier)
            if (seq_num == 0)
            {
                file_name = std::string(buffer + sizeof(int), n - sizeof(int));
                std::cout << "Nom du fichier reçu : " << file_name << std::endl;

                // Ouvrir le fichier pour écrire les données
                output_file = fopen(file_name.c_str(), "wb");
                if (!output_file)
                {
                    std::cerr << "Erreur d'ouverture du fichier de sortie : " << file_name << std::endl;
                    return;
                }

                // Envoyer une confirmation pour le nom du fichier
                send(tcp_socket, &seq_num, sizeof(seq_num), 0);
                continue;
            }

            // Vérifier si le fichier est correctement ouvert
            if (!output_file)
            {
                std::cerr << "Erreur : fichier non ouvert pour l'écriture." << std::endl;
                return;
            }

            // Si ce paquet n'a pas encore été reçu, l'ajouter et écrire dans le fichier
            if (received_sequence_numbers.find(seq_num) == received_sequence_numbers.end())
            {
                received_sequence_numbers.insert(seq_num);
                fwrite(buffer + sizeof(int), 1, n - sizeof(int), output_file);
                std::cout << "Paquet #" << seq_num << " reçu et écrit dans le fichier." << std::endl;

                // Envoyer la confirmation via TCP avec ré-essai
                int retries = 0;
                bool ack_sent = false;
                while (retries < MAX_RETRIES && !ack_sent)
                {
                    ssize_t bytes_sent = send(tcp_socket, &seq_num, sizeof(seq_num), 0);
                    if (bytes_sent > 0)
                    {
                        ack_sent = true;
                        std::cout << "Confirmation TCP envoyée pour le paquet #" << seq_num << std::endl;
                    }
                    else
                    {
                        retries++;
                        std::cerr << "Échec d'envoi de la confirmation TCP pour le paquet #" << seq_num << ", tentative " << retries << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Attendre avant de réessayer
                    }
                }

                if (retries == MAX_RETRIES)
                {
                    std::cerr << "Impossible d'envoyer la confirmation TCP pour le paquet #" << seq_num << " après " << MAX_RETRIES << " tentatives." << std::endl;
                }
            }
            else
            {
                std::cout << "Paquet déjà reçu, ignoré : #" << seq_num << std::endl;
            }
        }
    }

    // Fermeture du fichier après l'écriture
    fclose(output_file);
}

// Fonction pour gérer la connexion TCP pour recevoir les confirmations
void handle_tcp(int tcp_socket, bool &udp_is_closed)
{
    char buffer[BUFFER_SIZE];
    while (!udp_is_closed)
    {
        int n = recv(tcp_socket, buffer, sizeof(buffer), 0);
        if (n > 0)
        {
            int seq_num = *((int *)buffer);
            std::cout << "Confirmation TCP reçue pour le paquet #" << seq_num << std::endl;
        }
    }
}

int main()
{
    // Création de la socket UDP
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
    {
        std::cerr << "Erreur de création socket UDP" << std::endl;
        return -1;
    }

    // Création de la socket TCP
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0)
    {
        std::cerr << "Erreur de création socket TCP" << std::endl;
        return -1;
    }

    sockaddr_in udp_addr, tcp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_PORT);

    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(TCP_PORT);

    // Liaison des sockets
    if (bind(udp_socket, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0)
    {
        std::cerr << "Erreur de liaison UDP" << std::endl;
        return -1;
    }

    if (bind(tcp_socket, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0)
    {
        std::cerr << "Erreur de liaison TCP" << std::endl;
        return -1;
    }

    // Écoute sur le socket TCP
    if (listen(tcp_socket, 5) < 0)
    {
        std::cerr << "Erreur de listen TCP" << std::endl;
        return -1;
    }

    std::cout << "Serveur en attente de connexions..." << std::endl;

    // Attente de la connexion du client via TCP
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(tcp_socket, (struct sockaddr *)&client_addr, &client_len);
    if (client_sock < 0)
    {
        std::cerr << "Erreur de connexion TCP" << std::endl;
        return -1;
    }

    bool udp_is_closed = false;
    // Lancer les threads pour gérer UDP et TCP
    std::thread udp_thread(handle_udp, udp_socket, client_sock, std::ref(udp_is_closed));
    std::thread tcp_thread(handle_tcp, client_sock, std::ref(udp_is_closed));

    struct sigaction sigbreak;
    sigbreak.sa_handler = &handle_break;
    sigemptyset(&sigbreak.sa_mask);
    sigbreak.sa_flags = 0;
    if (sigaction(SIGINT, &sigbreak, NULL) != 0)
    {
        std::perror("sigClose");
        close(udp_socket);
        close(tcp_socket);
        exit(23);
    }

    udp_thread.join();
    tcp_thread.join();

    close(udp_socket);
    close(tcp_socket);
    return 0;
}
