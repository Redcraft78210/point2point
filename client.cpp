#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <ctype.h> // For isdigit
#include <cctype>
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <thread>
#include <sys/time.h> // Pour struct timeval
#include <unistd.h>
#include <vector>
#include <zlib.h>

#define UDP_PORT 12345
#define TCP_PORT 12346
#define SERVER_ADDR "127.0.0.1"
#define BUFFER_SIZE 8096
#define END_SIGNAL -1  // Signal de fin
#define MAX_RETRIES 20 // Nombre de tentatives de ré-essai pour chaque paquet
#define HEADER_SIZE 16

template <typename T>
T my_min(T a, T b)
{
    return (a < b) ? a : b;
}

template <typename T>
T my_max(T a, T b)
{
    return (a > b) ? a : b;
}

int udp_socket = -1;
int tcp_socket = -1;

#include <iostream>
#include <vector>
#include <iomanip> // For std::hex, std::setw, and std::setfill

void hexDump(const std::vector<char> &buffer)
{
    const size_t bytesPerLine = 16; // Nombre d'octets par ligne

    for (size_t i = 0; i < buffer.size(); i += bytesPerLine)
    {
        // Afficher l'offset
        std::cout << std::setw(8) << std::setfill('0') << std::hex << i << ": ";

        // Afficher les octets en hexadécimal
        for (size_t j = 0; j < bytesPerLine; ++j)
        {
            if (i + j < buffer.size())
            {
                unsigned char byte = static_cast<unsigned char>(buffer[i + j]);
                std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(byte) << " ";
            }
            else
            {
                std::cout << "   "; // Espace pour les octets manquants
            }
        }

        // Afficher les caractères ASCII
        std::cout << "| ";
        for (size_t j = 0; j < bytesPerLine; ++j)
        {
            if (i + j < buffer.size())
            {
                unsigned char byte = static_cast<unsigned char>(buffer[i + j]);
                if (std::isprint(byte))
                {
                    std::cout << static_cast<char>(byte);
                }
                else
                {
                    std::cout << '.';
                }
            }
            else
            {
                std::cout << ' '; // Espace pour les octets manquants
            }
        }
        std::cout << std::endl;
    }
}

void showUsage()
{
    std::cout << "Usage: file_sender \033[4mOPTIONS\033[0m\n";
    std::cout << "  -h, --help            Affiche l'aide\n";
    std::cout << "  -T, --test <file>      Numéro du test à effectuer\n";
    std::cout << "  -f, --file <file>      Fichier à envoyer\n";
    std::cout << "  -u, --udp_port <port>  Port UDP pour la transmission\n";
    std::cout << "  -t, --tcp_port <port>  Port TCP pour la transmission\n";
    std::cout << "  -a, --address <ip>     Adresse IP du serveur (défaut: 127.0.0.1)\n";
    std::cout << "  -c, --compress         Active la compression\n";
    std::cout << "  -v, --verbose          Affiche des informations détaillées\n";
}

// Signal handler function
void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        std::cout << "\nSignal caught! Closing socket and exiting...\n";

        // Close the socket if it's open
        if (udp_socket != -1)
        {
            close(udp_socket);
            std::cout << "UDP Socket closed successfully.\n";
        }

        if (tcp_socket != -1)
        {
            close(tcp_socket);
            std::cout << "TCP Socket closed successfully.\n";
        }

        // Exit the program
        exit(0);
    }
}

void test1(std::string &filePath, std::string &serverIP, bool &compressFlag)
{
    // Test d'un gros fichier binaire sans compression en local
    serverIP = "127.0.0.1";
    filePath = "data_to_send/fichier_binaire_1G.bin";
}
void test2(std::string &filePath, std::string &serverIP, bool &compressFlag)
{
    // Test d'un gros fichier binaire sans compression en distant
    filePath = "data_to_send/fichier_binaire_1G.bin";
    serverIP = "192.168.1.240";
}
void test3(std::string &filePath, std::string &serverIP, bool &compressFlag)
{
    // Test d'un gros fichier binaire avec compression en distant
    filePath = "data_to_send/fichier_binaire_1G.bin";
    serverIP = "172.20.10.4";
}
void test4(std::string &filePath, std::string &serverIP, bool &compressFlag) { printf("Called test4()\n"); }
void test5(std::string &filePath, std::string &serverIP, bool &compressFlag) { printf("Called test5()\n"); }

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

bool compressChunk(const std::vector<char> &input, std::vector<char> &output, bool verbose, size_t chunkSize)
{
    uLongf compressedSize = compressBound(input.size());
    output.resize(compressedSize);

    int result = compress2(reinterpret_cast<Bytef *>(output.data()), &compressedSize,
                           reinterpret_cast<const Bytef *>(input.data()), input.size(), Z_BEST_COMPRESSION);
    if (result != Z_OK)
    {
        return false;
    }

    output.resize(compressedSize);
    return true;
}

size_t calculateDynamicBufferSize(double transferRate)
{
    // Calcul de la taille du buffer dynamique basée sur la vitesse de transfert (en KB/s)
    size_t dynamicChunkSize = static_cast<size_t>(transferRate * 1024);      // Taille du buffer en bytes
    dynamicChunkSize = my_min(dynamicChunkSize, static_cast<size_t>(50000)); // Limite supérieure à 50000 bytes
        std::cout << "\nspeed is: " << transferRate << std::endl;

    // Si la taille calculée est inférieure à BUFFER_SIZE, on retourne BUFFER_SIZE comme taille minimale
    return my_max(dynamicChunkSize, static_cast<size_t>(BUFFER_SIZE));
}


// Fonction pour envoyer un fichier par UDP avec confirmation via TCP
void send_file_udp(int udp_socket, sockaddr_in &server_addr, const char *file_path, int tcp_socket)
{

    // Définir un délai d'attente de 15 secondes pour la réception
    struct timeval timeout;
    timeout.tv_sec = 15; // Délai d'attente de 15 secondes
    timeout.tv_usec = 0;

    if (setsockopt(tcp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        std::cerr << "Erreur de définition des options de socket." << std::endl;
        return;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file)
    {
        std::cerr << "Erreur d'ouverture du fichier" << std::endl;
        return;
    }

    std::vector<char> buffer(BUFFER_SIZE);
    socklen_t server_len = sizeof(server_addr);
    int packet_number = 0;
    size_t totalBytesSents = 0;

    file.seekg(0, std::ios::end);
    size_t totalBytestoSend = file.tellg();
    file.seekg(0, std::ios::beg);

    // Transmettre le nom du fichier en premier paquet
    std::string file_name = std::string(file_path).substr(std::string(file_path).find_last_of("/\\") + 1);
    size_t name_length = file_name.size();

    if (name_length >= BUFFER_SIZE - sizeof(int))
    {
        std::cerr << "Nom de fichier trop long pour être transmis" << std::endl;
        return;
    }

    *reinterpret_cast<int *>(buffer.data()) = 0; // Special packet for file name
    std::memcpy(buffer.data() + sizeof(int), file_name.data(), file_name.size());

    ssize_t bytes_sent = sendto(udp_socket, buffer.data(), sizeof(int) + file_name.size(), 0, (struct sockaddr *)&server_addr, server_len);
    if (bytes_sent < 0)
    {
        std::cerr << "Erreur d'envoi du nom de fichier" << std::endl;
        return;
    }

    // Attendre la confirmation pour le nom de fichier
    int ack_seq_num;
    ulong retries = 0;

    for (retries = 0; retries < MAX_RETRIES; retries++)
    {

        if (recv(tcp_socket, &ack_seq_num, sizeof(ack_seq_num), 0) > 0 && ack_seq_num == 0)
        {
            break;
        }
        std::cerr << "\nErreur de confirmation pour le nom de fichier, tentative " << retries + 1 << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10000)); // Attendre un peu avant de réessayer
    }
    if (retries == MAX_RETRIES - 1)
    {
        std::cerr << "Échec de la confirmation du nom de fichier" << std::endl;
        return;
    }
    auto startTime = std::chrono::steady_clock::now();
    // Dynamically allocate an initial buffer
    size_t chunkSize = calculateDynamicBufferSize(0); // Default size to determine initial chunk size
    buffer.clear();
    buffer.resize(chunkSize);
    bytes_sent = 0;
    while (file.read(buffer.data() + HEADER_SIZE, buffer.size() - HEADER_SIZE))
    {
        // hexDump(buffer);
        double elapsedTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        double transferRate = (bytes_sent / 1024.0) / elapsedTime; // Ko/s

        chunkSize = calculateDynamicBufferSize(transferRate);
        std::cout << "\nChunksize is: " << chunkSize << std::endl;
        packet_number++;
        *reinterpret_cast<int *>(buffer.data()) = packet_number;
        *reinterpret_cast<int *>(buffer.data() + sizeof(int)) = buffer.size();

        size_t bytes_to_send = file.gcount() + HEADER_SIZE;
        bytes_sent = 0;
        retries = 0;
        for (retries = 0; retries < MAX_RETRIES; retries++)
        {
            bytes_sent = sendto(udp_socket, buffer.data(), bytes_to_send, 0, (struct sockaddr *)&server_addr, server_len);
            if (bytes_sent < 0)
            {
                std::cerr << "\nErreur d'envoi du paquet UDP #" << packet_number << ", tentative " << retries + 1 << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10000)); // Attendre un peu avant de réessayer
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
                std::cerr << "\nConfirmation incorrecte ou échec de réception pour le paquet #" << packet_number << ", tentative " << retries + 1 << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10000)); // Attendre un peu avant de réessayer
            }
        }

        if (retries == MAX_RETRIES)
        {
            std::cerr << "Échec de la réception de la confirmation pour le paquet #" << packet_number << " après " << MAX_RETRIES << " tentatives" << std::endl;
            break;
        }

        totalBytesSents += file.gcount();
        showProgress(totalBytesSents, totalBytestoSend, elapsedTime);
        buffer.resize(chunkSize);
    }

    // Envoyer un paquet de fin (signal de fin)
    *reinterpret_cast<int *>(buffer.data()) = END_SIGNAL;
    sendto(udp_socket, buffer.data(), sizeof(int), 0, (struct sockaddr *)&server_addr, server_len);
    if (bytes_sent < 0)
    {
        std::cerr << "Erreur d'envoi du signal de fin" << std::endl;
    }

    std::cout << "\nEnvoi du fichier terminé." << std::endl;

    file.close();
    close(udp_socket);
    close(tcp_socket);
}

int main(int argc, char *argv[])
{
    // Verify if no arguments are passed
    if (argc == 1)
    {
        showUsage();
        return 0;
    }

    std::string filePath;
    std::string serverIP = SERVER_ADDR;
    int udp_port = UDP_PORT;
    int tcp_port = TCP_PORT;
    bool compressFlag = false;
    bool verbose = false;

    // Parse command-line options
    struct option longOpts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"test", required_argument, nullptr, 'T'},
        {"file", required_argument, nullptr, 'f'},
        {"udp_port", required_argument, nullptr, 'u'},
        {"tcp_port", required_argument, nullptr, 't'},
        {"address", required_argument, nullptr, 'a'},
        {"compress", no_argument, nullptr, 'c'},
        {"verbose", no_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "hf:u:t:a:cvT:", longOpts, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'T':
            try
            {
                int test = std::stoi(optarg); // Convert argument to integer
                if (std::to_string(test).length() != std::strlen(optarg))
                {
                    std::cout << "\"" << optarg << "\" is not a valid integer\n";
                    showUsage();
                    return 1;
                }

                void (*tests[])(std::string &filePath, std::string &serverIP, bool &) = {test1, test2}; // Add other test functions
                if (test >= 1 && test <= 5)
                {
                    verbose = true;
                    tests[test - 1](filePath, serverIP, compressFlag);
                    break;
                }
                else
                {
                    std::cout << "Test " << optarg << " does not exist\n";
                    showUsage();
                    return 1;
                }
            }
            catch (const std::invalid_argument &)
            {
                std::cout << optarg << " is not a valid integer\n";
            }
            catch (const std::out_of_range &)
            {
                std::cout << optarg << " is out of range\n";
            }
            showUsage();
            return 1;

        case 'h':
            showUsage();
            return 0;
        case 'f':
            filePath = optarg;
            break;
        case 'u':
            if (optarg && *optarg != '\0')
            {
                udp_port = std::stoi(optarg);
            }
            else
            {
                std::cerr << "Option -u requires a valid argument.\n";
            }
            break;
        case 't':
            if (optarg && *optarg != '\0')
            {
                tcp_port = std::stoi(optarg);
            }
            else
            {
                std::cerr << "Option -t requires a valid argument.\n";
            }
            break;
        case 'a':
            if (optarg && *optarg != '\0')
            {
                serverIP = optarg;
            }
            else
            {
                std::cerr << "Option -a requires a valid argument.\n";
            }
            break;
        case 'c':
            compressFlag = true;
            break;
        case 'v':
            verbose = true;
            break;
        default:
            showUsage();
            return 1;
        }
    }

    if (filePath.empty())
    {
        std::cerr << "No file specified!\n";
        showUsage();
        return 1;
    }

    // Create UDP socket
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
    {
        std::cerr << "Error creating UDP socket\n";
        return -1;
    }

    // Create TCP socket
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0)
    {
        std::cerr << "Error creating TCP socket\n";
        close(udp_socket); // Close UDP socket before returning
        return -1;
    }

    // Set up sigaction
    struct sigaction sa;
    sa.sa_handler = signal_handler; // Set the signal handler function
    sigemptyset(&sa.sa_mask);       // Clear the mask (no signals blocked)
    sa.sa_flags = 0;                // No special flags

    // Register the signal handler for SIGINT and SIGTERM
    if (sigaction(SIGINT, &sa, nullptr) == -1)
    {
        perror("Error setting SIGINT handler");
        return 1;
    }
    if (sigaction(SIGTERM, &sa, nullptr) == -1)
    {
        perror("Error setting SIGTERM handler");
        return 1;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(udp_port);
    inet_pton(AF_INET, serverIP.c_str(), &server_addr.sin_addr);

    // TCP connection setup
    sockaddr_in tcp_server_addr;
    memset(&tcp_server_addr, 0, sizeof(tcp_server_addr));
    tcp_server_addr.sin_family = AF_INET;
    tcp_server_addr.sin_port = htons(tcp_port);
    inet_pton(AF_INET, serverIP.c_str(), &tcp_server_addr.sin_addr);

    // Connection attempt with timeout
    std::cout << "Attempting connection... (20 seconds max):\n"
              << serverIP << ":" << udp_port << " / " << tcp_port << std::endl;

    // Set TCP socket to non-blocking mode
    int flags = fcntl(tcp_socket, F_GETFL, 0);
    if (flags < 0 || fcntl(tcp_socket, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "Error configuring non-blocking mode\n";
        close(udp_socket);
        close(tcp_socket);
        return -1;
    }

    // Try to connect
    connect(tcp_socket, (struct sockaddr *)&tcp_server_addr, sizeof(tcp_server_addr));

    // Use `select` to wait with timeout
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(tcp_socket, &writefds);

    struct timeval timeout;
    timeout.tv_sec = 20; // 20 seconds timeout
    timeout.tv_usec = 0;

    int ret = select(tcp_socket + 1, nullptr, &writefds, nullptr, &timeout);
    if (ret > 0 && FD_ISSET(tcp_socket, &writefds))
    {
        std::cout << "Connection established!\n";
    }
    else if (ret == 0)
    {
        std::cerr << "No connection after 20 seconds, closing.\n";
        close(udp_socket);
        close(tcp_socket);
        return -1;
    }
    else
    {
        std::cerr << "Select error\n";
        close(udp_socket);
        close(tcp_socket);
        return -1;
    }

    // Restore the socket to blocking mode
    if (fcntl(tcp_socket, F_SETFL, flags) < 0)
    {
        std::cerr << "Error restoring blocking mode\n";
        close(udp_socket);
        close(tcp_socket);
        return -1;
    }

    // Send the file over UDP with TCP confirmations
    send_file_udp(udp_socket, server_addr, filePath.c_str(), tcp_socket); // You should implement this function

    // Close the sockets after sending
    close(udp_socket);
    close(tcp_socket);
    return 0;
}
