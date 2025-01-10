
#include "MurmurHash3.h" // Include MurmurHash3 header
#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctype.h> // For isdigit
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h> // Pour struct timeval
#include <regex>
#include <thread>
#include <unistd.h>
#include <vector>
#include <zlib.h>

#define UDP_PORT 12345
#define TCP_PORT 12346
#define SERVER_ADDR "127.0.0.1"

#define HEADER_SIZE 8
#define FOOTER_SIZE 4

#define ACK_SIZE 64
#define METADATA_SIZE 256
#define BUFFER_SIZE 8096
#define MIN_BUFFER_SIZE 8096
#define MAX_BUFFER_SIZE 60000

#define END_SIGNAL -1  // Signal de fin
#define MAX_RETRIES 20 // Nombre de tentatives de ré-essai pour chaque paquet

#define ALPHA 0.5 // DEFAULT= 0.9
#define BETA 0.5  // DEFAULT = 0.3

int udp_socket = -1;
int tcp_socket = -1;

void hexDump(const std::vector<char> &buffer)
{
    const size_t bytesPerLine = 16; // Nombre d'octets par ligne

    for (size_t i = 0; i < buffer.size(); i += bytesPerLine)
    {
        // Afficher l'offset
        std::cerr << std::setw(8) << std::setfill('0') << std::hex << i << ": ";

        // Afficher les octets en hexadécimal
        for (size_t j = 0; j < bytesPerLine; ++j)
        {
            if (i + j < buffer.size())
            {
                unsigned char byte = static_cast<unsigned char>(buffer[i + j]);
                std::cerr << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(byte) << " ";
            }
            else
            {
                std::cerr << "   "; // Espace pour les octets manquants
            }
        }

        // Afficher les caractères ASCII
        std::cerr << "| ";
        for (size_t j = 0; j < bytesPerLine; ++j)
        {
            if (i + j < buffer.size())
            {
                unsigned char byte = static_cast<unsigned char>(buffer[i + j]);
                if (std::isprint(byte))
                {
                    std::cerr << static_cast<char>(byte);
                }
                else
                {
                    std::cerr << '.';
                }
            }
            else
            {
                std::cerr << ' '; // Espace pour les octets manquants
            }
        }
        std::cerr << std::endl;
    }
    std::cerr << std::dec;
}

void showUsage()
{
    // std::cout << "Usage: file_sender \033[4mOPTIONS\033[0m SRC... [USER@]HOST:DEST\n";
    std::cout << "Usage: file_sender [OPTIONS] SRC... [USER@]HOST:DEST\n";
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

void test1(std::string &local_filePath, std::string &serverIP, bool &compressFlag)
{
    // Test d'un gros fichier binaire sans compression en local
    serverIP = "127.0.0.1";
    local_filePath = "data_to_send/fichier_binaire_1G.bin";
}
void test2(std::string &local_filePath, std::string &serverIP, bool &compressFlag)
{
    // Test d'un gros fichier binaire sans compression en distant
    local_filePath = "data_to_send/large_file.txt";
    serverIP = "192.168.1.240";
}
void test3(std::string &local_filePath, std::string &serverIP, bool &compressFlag)
{
    // Test d'un gros fichier binaire avec compression en local
    local_filePath = "data_to_send/large_file.txt";
    serverIP = "127.0.0.1";
    compressFlag = true;
}
void test4(std::string &local_filePath, std::string &serverIP, bool &compressFlag)
{ // Test d'un gros fichier binaire avec compression en distant
    local_filePath = "data_to_send/fichier_binaire_1G.bin";
    serverIP = "192.168.1.240";
}
void test5(std::string &local_filePath, std::string &serverIP, bool &compressFlag) { printf("Called test5()\n"); }

// Function to calculate MurmurHash3 on a part of the buffer
uint32_t calculate_murmurhash3(const std::vector<char> &buffer,
                               int start_offset_bytes = 0,
                               int end_offset_bytes = 0,
                               uint32_t seed = 0)
{

    // Calculate the range to hash
    const char *data_start = buffer.data() + start_offset_bytes;
    int data_size = static_cast<int>(buffer.size()) - start_offset_bytes - end_offset_bytes;

    // Créer un vecteur à partir de la plage spécifiée
    std::vector<char> subvector(data_start, data_start + data_size);

    // Compute the hash
    uint32_t hash = 0;
    MurmurHash3_x86_32(data_start, data_size, seed, &hash);
    return hash;
}

// Fonction pour calculer le hash et l'ajouter à la fin du tampon
std::string murmurhash_addition(std::vector<char> &buffer, int start_offset_bytes = 0, int end_offset_bytes = 0)
{
    // Calculer le MurmurHash3 du tampon
    uint32_t hash = calculate_murmurhash3(buffer, start_offset_bytes, end_offset_bytes);

    // Obtenir un pointeur sur les octets du hash en utilisant reinterpret_cast
    const char *hash_bytes = reinterpret_cast<const char *>(&hash);

    // Remplacer les 4 derniers octets par les octets du hash (format little-endian)
    buffer[buffer.size() - 4] = hash_bytes[0];
    buffer[buffer.size() - 3] = hash_bytes[1];
    buffer[buffer.size() - 2] = hash_bytes[2];
    buffer[buffer.size() - 1] = hash_bytes[3];

    // Créer une chaîne hexadécimale des octets du hash
    std::stringstream ss;
    for (int i = 0; i < 4; ++i)
    {
        ss << std::setw(2) << std::setfill('0') << std::hex << (0xFF & static_cast<unsigned char>(hash_bytes[i]));
    }

    return ss.str();
}

void showProgress(ssize_t previous_sent_bytes, double previous_sent_duration, ssize_t bytesSent, size_t totalBytes, const std::chrono::steady_clock::time_point &startTime, double elapsedTime)
{
    int progress = static_cast<int>((bytesSent * 100) / totalBytes);
    double originalTransferRate = (bytesSent / 1024.0) / elapsedTime; // KB/s
    double transferRate = 0.0;
    if (previous_sent_duration > 0)
    {
        transferRate = (previous_sent_bytes / 1024.0) / previous_sent_duration; // KB/s
    }
    else
    {
        transferRate = originalTransferRate; // Fallback to average transfer rate
    }

    // Convertir le taux de transfert en fonction de sa taille
    std::string rateUnit = "KB/s";
    if (transferRate >= 1024)
    {
        transferRate /= 1024; // Convertir en MB/s
        rateUnit = "MB/s";
    }
    if (transferRate >= 1024)
    {
        transferRate /= 1024; // Convertir en GB/s
        rateUnit = "GB/s";
    }
    // Calculer le temps écoulé
    auto now = std::chrono::steady_clock::now();
    double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();

    // Calculer le temps restant estimé (en secondes)
    double remainingBytes = totalBytes - bytesSent;
    double leftTime = (originalTransferRate > 0) ? (remainingBytes / 1024.0) / originalTransferRate : 0; // en secondes
    // Formatage du temps écoulé et du temps restant
    int elapsedMinutes = static_cast<int>(elapsedSeconds) / 60;
    int elapsedSecondsInt = static_cast<int>(elapsedSeconds) % 60;

    int leftMinutes = static_cast<int>(leftTime) / 60;
    int leftSeconds = static_cast<int>(leftTime) % 60;

    // Affichage du progrès, du taux de transfert, du temps écoulé et du temps restant
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
              << transferRate << " " << rateUnit
              << " | Elapsed: " << elapsedMinutes << "m " << elapsedSecondsInt << "s"
              << " | Left: " << leftMinutes << "m " << leftSeconds << "s";

    std::flush(std::cout);
}

// Function to check if a path exists
bool fileExists(const std::string &path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// Function to check if a path has read permission
bool hasReadPermission(const std::string &path)
{
    std::ifstream file(path);
    return file.is_open();
}

bool compressChunk(std::vector<char> &buffer, bool verbose = false)
{
    /// Pointeur sur les données à partir de l'offset HEADER_SIZE dans buffer
    std::vector<char>::iterator dataStart = buffer.begin() + HEADER_SIZE;
    size_t dataSize = buffer.size() - HEADER_SIZE - FOOTER_SIZE;

    // Calcul de la taille maximale compressée
    uLongf compressedSize = compressBound(dataSize);
    std::vector<char> tempBuffer(HEADER_SIZE + compressedSize); // Tampon temporaire
    try
    {
        // Compression des données dans le tampon temporaire
        int result = compress2(reinterpret_cast<Bytef *>(tempBuffer.data() + HEADER_SIZE), &compressedSize,
                               reinterpret_cast<const Bytef *>(&*dataStart), dataSize, Z_BEST_COMPRESSION);

        // Vérification du résultat de la compression
        if (result != Z_OK)
        {
            if (verbose)
            {
                std::cerr << "Compression failed with error code: " << result << std::endl;
            }
            return false;
        }

        // Ajustement de la taille du tampon temporaire pour inclure uniquement les données compressées et les en-têtes
        tempBuffer.resize(HEADER_SIZE + compressedSize + FOOTER_SIZE);

        // Nettoyage du buffer d'origine et copie des données du tampon temporaire
        buffer.clear();
        buffer.insert(buffer.end(), tempBuffer.begin(), tempBuffer.end());

        if (verbose)
        {
            std::cout << "Compression succeeded. Final buffer size: " << buffer.size() << std::endl;
        }

        return true;
    }
    catch (const std::bad_alloc &e)
    {
        if (verbose)
        {
            std::cerr << "Memory allocation failed: " << e.what() << std::endl;
        }
        return false;
    }
}

// Fonction pour ajuster la taille du buffer
double adjustBufferSize(double currentSpeed = 0, double previousTime = 0, size_t currentBufferSize = BUFFER_SIZE)
{
    // Calcul d'une nouvelle taille de buffer basée sur la vitesse
    double newBufferSize = currentBufferSize * (1 + ALPHA * (currentSpeed / 1000.0));

    // Ajustement basé sur le temps d'envoi du paquet précédent
    double timeFactor = std::max(0.1, 1 - BETA * previousTime);
    newBufferSize *= timeFactor;

    // Limitation de la taille du buffer
    newBufferSize = std::max(static_cast<double>(MIN_BUFFER_SIZE),
                             std::min(static_cast<double>(MAX_BUFFER_SIZE), newBufferSize));

    return newBufferSize;
}

bool resendMetadata(int udp_socket, const std::vector<char> &buffer, size_t metadata_size,
                    const struct sockaddr_in &server_addr, socklen_t server_len,
                    int max_attempts = 5)
{
    int send_attempts = 0;
    while (send_attempts < max_attempts)
    {
        if (sendto(udp_socket, buffer.data(), metadata_size, 0, (struct sockaddr *)&server_addr, server_len) >= 0)
        {
            // Successfully sent the metadata
            return true;
        }

        std::cerr << "Erreur d'envoi du nom de fichier, tentative " << (send_attempts + 1) << " de " << max_attempts << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Wait before retrying
        ++send_attempts;
    }

    // Failed to send after max_attempts
    std::cerr << "Échec du renvoi du nom de fichier après " << max_attempts << " tentatives" << std::endl;
    return false;
}

// Fonction pour envoyer un fichier par UDP avec confirmation via TCP
void send_file_udp(int udp_socket, sockaddr_in &server_addr, const char *file_path, const char *destination, int tcp_socket, bool compressFlag)
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

    std::vector<char> buffer(METADATA_SIZE);
    socklen_t server_len = sizeof(server_addr);
    int packet_number = 0;
    size_t totalBytesSents = 0;

    file.seekg(0, std::ios::end);
    size_t totalBytestoSend = file.tellg();
    file.seekg(0, std::ios::beg);

    *reinterpret_cast<int *>(buffer.data()) = 0; // Special packet for file name
    *reinterpret_cast<int *>(buffer.data() + sizeof(int)) = BUFFER_SIZE;
    std::memcpy(buffer.data() + HEADER_SIZE, destination, std::strlen(destination));
    *reinterpret_cast<int *>(buffer.data() + buffer.size() - 2 * sizeof(int)) = compressFlag ? 1 : 0;
    std::string checksum = murmurhash_addition(buffer);
    while (sendto(udp_socket, buffer.data(), buffer.size(), 0, (struct sockaddr *)&server_addr, server_len) < 0)
    {
        std::cerr << "Erreur d'envoi du nom de fichier" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Attendre un peu avant de réessayer
    }

    ulong retries = 0;

    for (retries = 0; retries < MAX_RETRIES; retries++)
    {
        std::vector<char> ack_buffer(ACK_SIZE);
        ssize_t n = recv(tcp_socket, ack_buffer.data(), ACK_SIZE, 0);
        if (n > 0)
        {
            try
            {
                int received_packet_number = std::stoi(std::string(ack_buffer.data(), n));
                if (received_packet_number == packet_number)
                {
                    // std::cout << "ACK correct reçu pour le paquet #" << packet_number << std::endl;
                    break;
                }
            }
            catch (const std::invalid_argument &)
            {
                std::cerr << "Erreur de conversion de l'ACK en entier." << std::endl;
            }

            if (std::string(ack_buffer.data(), n) == "INCORRECT CRC")
            {
                std::cerr << "CRC incorrect pour le paquet #" << packet_number << ", tentative " << retries << std::endl;
                if (!resendMetadata(udp_socket, buffer, METADATA_SIZE, server_addr, server_len))
                {
                    std::cerr << "Échec du renvoi du nom de fichier après CRC incorrect" << std::endl;
                    return;
                }
                continue; // Skip the sleep and retry immediately
            }
            else
            {
                std::cerr << "The folder \"" << std::string(ack_buffer.data(), n) << "\" does not exist ! " << std::endl;
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Attendre un peu avant de réessayer
    }

    if (retries == MAX_RETRIES)
    {
        std::cerr << "Échec de la confirmation du nom de fichier" << std::endl;
        return;
    }

    auto startTime = std::chrono::steady_clock::now();
    // Dynamically allocate an initial buffer
    size_t chunkSize = adjustBufferSize(); // Default size to determine initial chunk size
    buffer.clear();
    buffer.resize(chunkSize);
    ssize_t bytes_sent = 0;
    ssize_t previous_sent_bytes = 0;
    double previousSentTime = 0.0;
    bool is_newfile = false;

    while (totalBytestoSend != totalBytesSents)
    {
        size_t currentBufferSize = buffer.size();
        file.read(buffer.data() + HEADER_SIZE, currentBufferSize - HEADER_SIZE - FOOTER_SIZE);
        buffer.resize(file.gcount() + HEADER_SIZE + FOOTER_SIZE);

        if (!is_newfile)
        {
            // Calculer le MurmurHash3 du tampon
            uint32_t hash = calculate_murmurhash3(buffer, HEADER_SIZE, FOOTER_SIZE);

            // Obtenir un pointeur sur les octets du hash en utilisant reinterpret_cast
            const char *hash_bytes = reinterpret_cast<const char *>(&hash);

            // Créer une chaîne hexadécimale des octets du hash
            std::stringstream ss;
            for (int i = 0; i < 4; ++i)
            {
                ss << std::setw(2) << std::setfill('0') << std::hex << (0xFF & static_cast<unsigned char>(hash_bytes[i]));
            }

            std::string checksum_data = ss.str();

            std::string data_with_crc = "DATA_CRC:" + checksum_data;                          // Concatenate strings
            std::vector<char> incremental_buffer(data_with_crc.begin(), data_with_crc.end()); // Construct vector from string

            bytes_sent = 0;
            retries = 0;
            bool must_resend = false;
            for (retries = 0; retries < MAX_RETRIES; retries++)
            {
                if (retries != 0)
                {
                    hexDump(incremental_buffer);
                }
                bytes_sent = sendto(udp_socket, incremental_buffer.data(), incremental_buffer.size(), 0, (struct sockaddr *)&server_addr, server_len);
                if (bytes_sent < 0)
                {
                    std::cerr << "\nErreur d'envoi du CRC des datas du  paquet UDP #" << packet_number << ", tentative " << retries + 1 << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Attendre un peu avant de réessayer
                    continue;
                }

                std::vector<char> ack_buffer(ACK_SIZE);
                ssize_t n = recv(tcp_socket, ack_buffer.data(), ACK_SIZE, 0);
                if (n > 0)
                {
                    std::string ack_message = std::string(ack_buffer.data(), n);
                    if (ack_message == "NOT")
                    {
                        must_resend = true;
                        double elapsedTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
                        packet_number++;
                        totalBytesSents += file.gcount();
                        showProgress(previous_sent_bytes, previousSentTime, totalBytesSents, totalBytestoSend, startTime, elapsedTime);
                        buffer.clear();
                        buffer.resize(currentBufferSize);
                    }
                    else if (ack_message == "NEW FILE !")
                    {
                        is_newfile = true;
                    }
                    break;
                }
                else
                {
                    // Error or no data received, possibly retrying
                    std::cerr << "Erreur de réception de l'ACK pour le CRC des données du paquet #" << packet_number << ", tentative " << retries << std::endl;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Attendre un peu avant de réessayer
            }

            if (retries == MAX_RETRIES)
            {
                std::cerr << "Échec de la réception de la confirmation pour le CRC des données du paquet #" << packet_number << " après " << MAX_RETRIES << " tentatives" << std::endl;
                break;
            }
            else if (must_resend)
            {
                continue;
            }
        }

        double elapsedTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        double transferRate = (previous_sent_bytes / 1024.0) / previousSentTime; // Ko/s

        chunkSize = adjustBufferSize(transferRate, previousSentTime, currentBufferSize);

        if (compressFlag)
            compressChunk(buffer);

        packet_number++;
        *reinterpret_cast<int *>(buffer.data()) = packet_number;
        *reinterpret_cast<int *>(buffer.data() + sizeof(int)) = chunkSize;

        size_t bytes_to_send = buffer.size();

        std::string checksum = murmurhash_addition(buffer);
        bytes_sent = 0;
        retries = 0;
        auto startsendTime = std::chrono::steady_clock::now();
        for (retries = 0; retries < MAX_RETRIES; retries++)
        {
            if (retries != 0)
            {
                hexDump(buffer);
            }
            bytes_sent = sendto(udp_socket, buffer.data(), bytes_to_send, 0, (struct sockaddr *)&server_addr, server_len);
            previous_sent_bytes = bytes_sent;
            if (bytes_sent < 0)
            {
                std::cerr << "\nErreur d'envoi du paquet UDP #" << packet_number << ", tentative " << retries + 1 << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Attendre un peu avant de réessayer
                continue;
            }

            std::vector<char> ack_buffer(ACK_SIZE);
            ssize_t n = recv(tcp_socket, ack_buffer.data(), ACK_SIZE, 0);
            if (n > 0)
            {
                // Case 1: ACK is correct (ack_buffer contains the packet number)
                try
                {
                    // Try converting the received buffer to an integer
                    int received_packet_number = std::stoi(std::string(ack_buffer.data(), n)); // Use only valid bytes
                    if (received_packet_number == packet_number)
                    {
                        // std::cout << "ACK correct reçu pour le paquet #" << packet_number << std::endl;
                        break; // ACK correct reçu, exit the loop
                    }
                }
                catch (const std::invalid_argument &)
                {
                    // Case 2: Received message indicates "INCORRECT CRC"
                    if (std::string(ack_buffer.data(), n) == "INCORRECT CRC")
                    {
                        std::cerr << "\nCRC incorrect (!=" << checksum << ") pour le paquet #" << packet_number << ", tentative " << retries << std::endl;
                        // You can handle the "INCORRECT CRC" case here, like retrying or other logic
                        // For now, we just log and retry
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Attendre un peu avant de réessayer
                        continue;
                    }
                }
            }
            else
            {
                // Error or no data received, possibly retrying
                std::cerr << "Erreur de réception de l'ACK pour le paquet #" << packet_number << ", tentative " << retries << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Attendre un peu avant de réessayer
        }
        previousSentTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startsendTime).count();

        if (retries == MAX_RETRIES)
        {
            std::cerr << "Échec de la réception de la confirmation pour le paquet #" << packet_number << " après " << MAX_RETRIES << " tentatives" << std::endl;
            break;
        }

        totalBytesSents += file.gcount();
        showProgress(previous_sent_bytes, previousSentTime, totalBytesSents, totalBytestoSend, startTime, elapsedTime);
        buffer.clear();
        buffer.resize(chunkSize);
    }

    if (totalBytestoSend == totalBytesSents)
    {
        buffer.resize(sizeof(int)); // Assurez-vous que le buffer est de taille suffisante pour contenir un int
        *reinterpret_cast<int *>(buffer.data()) = END_SIGNAL;
        sendto(udp_socket, buffer.data(), sizeof(int), 0, (struct sockaddr *)&server_addr, server_len);

        if (bytes_sent < 0)
        {
            std::cerr << "Erreur d'envoi du signal de fin" << std::endl;
        }
        else
        {
            std::cout << "\nEnvoi du fichier terminé." << std::endl;
        }
    }
    else
    {
        std::cerr << "\nEnvoi incomplet." << std::endl;
        std::cerr << "Seulement " << totalBytesSents << "/" << totalBytestoSend << "octets ont été envoyés" << std::endl;
    }

    file.close();
}

bool endsWithNonEscapedSlash(const std::string &str)
{
    // Check if the string is empty or doesn't end with '/'
    if (str.empty() || str.back() != '/')
    {
        return false;
    }

    // Iterate backward to count the number of consecutive backslashes before the last character
    int backslashCount = 0;
    for (int i = str.size() - 2; i >= 0; --i)
    {
        if (str[i] == '\\')
        {
            ++backslashCount;
        }
        else
        {
            break;
        }
    }

    // If the number of backslashes is even, the '/' is not escaped
    // If odd, the '/' is escaped
    return (backslashCount % 2 == 0);
}

int main(int argc, char *argv[])
{
    // Verify if no arguments are passed
    if (argc == 1)
    {
        showUsage();
        return 0;
    }

    std::string local_filePath;
    std::string remote_filePath;
    std::string destination;

    std::string serverIP = SERVER_ADDR;
    int udp_port = UDP_PORT;
    int tcp_port = TCP_PORT;
    bool compressFlag = false;
    bool verbose = false;

    // Verify if no arguments are passed
    if (argc == 1)
    {
        showUsage();
        return 0;
    }

    // Parse command-line options
    struct option longOpts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"test", required_argument, nullptr, 'T'},
        {"local_filePath", required_argument, nullptr, 'f'},
        {"remote_filePath", required_argument, nullptr, 'r'},
        {"udp_port", required_argument, nullptr, 'u'},
        {"tcp_port", required_argument, nullptr, 't'},
        {"address", required_argument, nullptr, 'a'},
        {"compress", no_argument, nullptr, 'c'},
        {"verbose", no_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "hf:u:t:a:cvlT:", longOpts, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'T':
            try
            {
                int test = std::stoi(optarg); // Convert argument to integer
                if (std::to_string(test).length() != std::strlen(optarg))
                {
                    std::cerr << "\"" << optarg << "\" is not a valid integer\n";
                    showUsage();
                    return 1;
                }

                void (*tests[])(std::string &local_filePath, std::string &serverIP, bool &) = {test1, test2, test3, test4, test5}; // Add other test functions
                if (test >= 1 && test <= 5)
                {
                    verbose = true;
                    tests[test - 1](local_filePath, serverIP, compressFlag);
                    break;
                }
                else
                {
                    std::cerr << "Test " << optarg << " does not exist\n";
                    showUsage();
                    return 1;
                }
            }
            catch (const std::invalid_argument &)
            {
                std::cerr << optarg << " is not a valid integer\n";
            }
            catch (const std::out_of_range &)
            {
                std::cerr << optarg << " is out of range\n";
            }
            showUsage();
            return 1;

        case 'h':
            showUsage();
            return 0;
        case 'f':
            local_filePath = optarg;
            break;
        case 'r':
            remote_filePath = optarg;
            break;
        case 'u':
            udp_port = std::stoi(optarg);
            break;
        case 't':
            tcp_port = std::stoi(optarg);
            break;
        case 'a':
            serverIP = optarg;
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

    // Handle remaining arguments (e.g., positional arguments like destination)
    std::vector<std::string> positionalArgs;
    for (int i = optind; i < argc; ++i)
    {
        positionalArgs.emplace_back(argv[i]);
    }

    if (positionalArgs.size() == 2)
    {
        local_filePath = positionalArgs[0];
        destination = positionalArgs[1];
        // Validate destination format ([user@]host:path)
        std::regex destRegex(R"(^[\w.-]+@[\w.-]+:[\w/]+$)"); // Regex for [user@]host:path
        std::smatch match;

        if (std::regex_match(destination, destRegex))
        {
            // If the format matches [user@]host:path
            size_t atPos = destination.find('@');
            if (atPos != std::string::npos)
            {
                std::string host = destination.substr(atPos + 1, destination.find(':') - atPos - 1);
                std::string destPath = destination.substr(destination.find(':') + 1);
                serverIP = host;
                destination = destPath;
            }
        }
        else
        {
            // If the format doesn't match the first regex, check for [host:path]
            std::regex destRegexWithoutUser(R"(^[\w.-]+:[\w/]+$)"); // Regex for host:path

            if (std::regex_match(destination, destRegexWithoutUser))
            {
                // Extract the host part before the ':'
                size_t colonPos = destination.find(':');
                if (colonPos != std::string::npos)
                {
                    std::string host = destination.substr(0, colonPos);
                    std::string destPath = destination.substr(1, colonPos);
                    serverIP = host;
                    destination = destPath;
                }
            }
            else
            {
                std::cerr << "Invalid destination format: " << destination << "\n";
                showUsage();
                return 1;
            }
        }
        if (endsWithNonEscapedSlash(destination))
        {
            std::cout << "Invalid Destination filepath: Destination filepath cannot end by a non-escaped slash." << std::endl;
            return 3;
        }
    }

    else if (!positionalArgs.empty())
    {
        std::cerr << "Bad syntax." << std::endl;
        showUsage();
        return 1;
    }

    if (fileExists(local_filePath))
    {
        if (!hasReadPermission(local_filePath))
        {
            std::cout << "Path exists but you don't have read permission." << std::endl;
            return 1;
        }
    }
    else
    {
        std::cout << "Path does not exist." << std::endl;
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
    send_file_udp(udp_socket, server_addr, local_filePath.c_str(), destination.c_str(), tcp_socket, compressFlag); // You should implement this function

    // Close the sockets after sending
    close(udp_socket);
    close(tcp_socket);
    std::cout << std::endl;
    return 0;
}
