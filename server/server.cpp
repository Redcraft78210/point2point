#include "MurmurHash3.h" // Include MurmurHash3 header
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdlib> // Pour getenv
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip> // For std::hex, std::setw, and std::setfill
#include <iostream>
#include <limits.h> // Pour PATH_MAX
#include <pwd.h>
#include <set>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <unistd.h> // Pour chdir
#include <vector>
#include <zlib.h>

#define UDP_PORT 12345
#define TCP_PORT 12346

#define METADATA_SIZE 256
#define BUFFER_SIZE 8096
#define HEADER_SIZE 8
#define FOOTER_SIZE 4

#define OUTPUT_FILE "received_file.txt"
#define END_SIGNAL -1  // Signal de fin
#define MAX_RETRIES 20 // Nombre de tentatives pour chaque confirmation TCP

int udp_socket = -1;
int tcp_socket = -1;

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

    // Reset stream formatting to defaults
    std::cout << std::dec << std::setfill(' ') << std::setw(0);
}

// Fonction pour calculer le hash MurmurHash3 d'un tampon
uint32_t calculate_murmurhash3(const std::vector<char> &buffer, uint32_t seed = 0)
{
    uint32_t hash = 0;
    MurmurHash3_x86_32(buffer.data(), buffer.size(), seed, &hash);
    return hash;
}

bool decompressChunk(std::vector<char> &input, bool verbose = true)
{
    const char *bufferStart = input.data() + HEADER_SIZE;
    const char *bufferEnd = input.data() + input.size() - FOOTER_SIZE;
    size_t bufferSize = bufferEnd - bufferStart;

    // Estimate the maximum decompressed size
    size_t outputSize = bufferSize * 2;
    std::vector<char> tempBuffer(outputSize);

    const size_t MAX_BUFFER_SIZE = 1024 * 1024 * 1024; // 1 GB cap for example

    while (true)
    {
        if (outputSize > MAX_BUFFER_SIZE)
        {
            if (verbose)
            {
                std::cerr << "Decompression buffer size exceeded maximum limit.\n";
            }
            return false;
        }

        int result = uncompress(
            reinterpret_cast<Bytef *>(tempBuffer.data()),
            &outputSize,
            reinterpret_cast<const Bytef *>(bufferStart),
            bufferSize);

        if (result == Z_OK)
        {
            // Resize the buffer to the actual decompressed size
            input.assign(tempBuffer.begin(), tempBuffer.begin() + outputSize);
            if (verbose)
            {
                std::cout << "Decompressed successfully. Original size: " << bufferSize
                          << ", Decompressed size: " << outputSize << " bytes.\n";
            }
            return true;
        }
        else if (result == Z_BUF_ERROR)
        {
            // Expand the output buffer
            outputSize *= 2;
            tempBuffer.resize(outputSize);
        }
        else
        {
            if (verbose)
            {
                // Decompression error
                std::cerr << "Decompression error: " << result << "\n";
            }
            return false;
        }
    }
}

std::string checkProblematicComponent(const std::string &path)
{
    std::filesystem::path fsPath = path;

    // Construire chaque sous-chemin et vérifier son existence
    for (auto it = fsPath.begin(); it != fsPath.end(); ++it)
    {
        std::filesystem::path currentPath = fsPath.root_path();
        for (auto subIt = fsPath.begin(); subIt != it; ++subIt)
        {
            currentPath /= *subIt;
        }

        // Si un composant n'existe pas, retourner son nom
        if (!std::filesystem::exists(currentPath))
        {
            return it->string(); // Retourner uniquement le composant actuel
        }
    }

    return ""; // Aucun problème trouvé
}

// Fonction pour gérer la réception des paquets UDP et écrire dans le fichier
void handle_udp(int udp_socket, int tcp_socket, bool &udp_is_closed)
{
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    std::vector<char> buffer(256);
    std::string filePath;
    bool compressFlag = false;
    int next_buffer_size;
    // Set pour suivre les numéros de séquence des paquets reçus
    std::set<int> received_sequence_numbers;

    std::fstream output_file;
    bool is_filepath_packet = true;
    bool existing_file = false;
    bool packet_corrupted = false;
    bool incremental_mode = true;
    int writed_packet = 0;
    while (true)
    {
        if (!is_filepath_packet && !packet_corrupted && incremental_mode)
        {
            if (existing_file)
            {
                int current_buffer_size = buffer.size() - 1024;
                int r = recvfrom(udp_socket, buffer.data(), current_buffer_size, 0, (struct sockaddr *)&client_addr, &client_len);
                if (r > 0)
                {
                    std::string received_data(buffer.begin(), buffer.begin() + r);
                    std::vector<char> data_chunk(current_buffer_size - HEADER_SIZE - FOOTER_SIZE);

                    std::vector<std::string> tokens;
                    std::stringstream ss(received_data);
                    std::string token;

                    char delimiter = ':';
                    while (std::getline(ss, token, delimiter))
                    {
                        tokens.push_back(token);
                    }

                    if (tokens.size() != 2)
                    {
                        // Check if std::string received_data corresponds to 4 * 8 bytes of 0x01
                        if (std::all_of(buffer.data(), buffer.data() + 4, [](unsigned char c)
                                        { return c == 255; }))
                        {
                            std::cout << "Serveur UDP : fin de l'envoi des paquets" << std::endl;
                            if (writed_packet == 0)
                            {
                                std::cout << "No packet written: the sent data and the destination file content are identical." << std::endl;
                            }

                            udp_is_closed = true;
                            break;
                        }
                    }

                    // The second part contains the checksum as text
                    const std::string &checksum_str = tokens[1];

                    // Convert the string to uint32_t
                    uint32_t checksum = 0;
                    try
                    {
                        checksum = static_cast<uint32_t>(std::stoul(checksum_str, nullptr, 16));
                        checksum = ((checksum & 0xFF) << 24) |      // Moins significatif
                                   ((checksum & 0xFF00) << 8) |     // Deuxième octet
                                   ((checksum & 0xFF0000) >> 8) |   // Troisième octet
                                   ((checksum & 0xFF000000) >> 24); // Plus significatif
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "Error: Unable to convert checksum to uint32_t: " << e.what() << std::endl;
                        return;
                    }

                    uint32_t filechunk_checksum;

                    // Position de lecture précédente
                    std::streampos previous_pos = output_file.tellg();

                    // Déplacer le curseur à la fin pour vérifier si le fichier est vide
                    output_file.seekg(0, std::ios::end);
                    if (output_file.tellg() == 0)
                    {
                        std::cout << "Le fichier est vide. Poursuite de l'exécution." << std::endl;
                        output_file.seekg(previous_pos, std::ios::beg); // Restaurer la position précédente
                        // Continuer l'exécution du programme sans lire
                    }
                    else
                    {
                        // Restaurer la position précédente
                        output_file.seekg(previous_pos, std::ios::beg);
                        if (!output_file.good())
                        {
                            std::cerr << "Erreur lors du déplacement du curseur à la position précédente." << std::endl;
                            return; // Vous pouvez décider quoi faire en cas d'erreur (afficher un message ou ignorer).
                        }

                        // Lire une portion de données
                        output_file.read(data_chunk.data(), current_buffer_size - HEADER_SIZE - FOOTER_SIZE);
                        if (!output_file)
                        {
                            if (output_file.eof())
                            {
                                output_file.clear();
                            }
                        }
                        // Utilisation des données lues
                        size_t bytes_read = output_file.gcount();
                        if (bytes_read > 0)
                        {
                            if (bytes_read < data_chunk.size())
                            {
                                data_chunk.resize(bytes_read);
                            }
                            filechunk_checksum = calculate_murmurhash3(data_chunk);
                        }
                    }

                    std::string message = "SEND";
                    if (filechunk_checksum && filechunk_checksum == checksum)
                    {
                        message = "NOT";
                    }
                    else
                    {
                        output_file.seekg(previous_pos, std::ios::beg);
                    }
                    // Envoyer la confirmation via TCP avec ré-essai
                    int retries = 0;
                    bool ack_sent = false;
                    // Envoyer une confirmation pour le nom du fichier
                    while (retries < MAX_RETRIES && !ack_sent)
                    {
                        ssize_t bytes_sent = send(tcp_socket, message.c_str(), message.length(), 0);

                        if (bytes_sent > 0)
                        {
                            ack_sent = true;
                            // std::cout << "\nConfirmation TCP envoyée pour le nom de fichier" << std::endl;
                        }
                        else
                        {
                            retries++; // Increment retries on failure
                            // std::cerr << "Failed to send TCP confirmation, retrying (" << retries << "/" << MAX_RETRIES << ")" << std::endl;
                        }
                    }

                    if (!ack_sent)
                    {
                        // std::cerr << "Failed to send TCP confirmation after " << MAX_RETRIES << " retries." << std::endl;
                    }

                    buffer.clear();
                    buffer.resize(current_buffer_size + 1024);

                    if (checksum == filechunk_checksum)
                    {
                        continue;
                    }
                }
            }
            else
            {
                std::string message = "NEW FILE !";
                int current_buffer_size = buffer.size() - 1024;
                recvfrom(udp_socket, buffer.data(), current_buffer_size, 0, (struct sockaddr *)&client_addr, &client_len);

                // Envoyer la confirmation via TCP avec ré-essai
                int retries = 0;
                bool ack_sent = false;
                // Envoyer une confirmation pour le nom du fichier
                while (retries < MAX_RETRIES && !ack_sent)
                {
                    ssize_t bytes_sent = send(tcp_socket, message.c_str(), message.length(), 0);

                    if (bytes_sent > 0)
                    {
                        ack_sent = true;
                        incremental_mode = false;
                        std::cout << "\nConfirmation TCP envoyée pour le nom de fichier" << std::endl;
                        break;
                    }
                    else
                    {
                        retries++; // Increment retries on failure
                        std::cerr << "Failed to send TCP confirmation, retrying (" << retries << "/" << MAX_RETRIES << ")" << std::endl;
                    }
                }

                if (!ack_sent)
                {
                    std::cerr << "Failed to send TCP confirmation after " << MAX_RETRIES << " retries." << std::endl;
                }
                else
                {
                    buffer.clear();
                    buffer.resize(current_buffer_size + 1024);
                    continue;
                }
                return;
            }
        }
        int n = recvfrom(udp_socket, buffer.data(), buffer.size(), 0, (struct sockaddr *)&client_addr, &client_len);
        if (n > 0)
        {
            // Extraire le numéro de séquence du paquet
            int seq_num = *reinterpret_cast<int *>(buffer.data());
            next_buffer_size = *reinterpret_cast<int *>(buffer.data() + sizeof(int)) + 1024;
            buffer.resize(n);
            std::vector<char> buffer_bak(buffer);

            // Si le signal de fin est reçu, terminer l'envoi
            if (seq_num == END_SIGNAL)
            {
                std::cout << "Serveur UDP : fin de l'envoi des paquets" << std::endl;
                udp_is_closed = true;
                break;
            }

            switch (seq_num)
            {
            case 0:
                is_filepath_packet = false;
                // Extract the file name and checksum
                std::string filePath(buffer.data() + HEADER_SIZE, n - HEADER_SIZE - FOOTER_SIZE);

                // Récupérer le répertoire courant
                char currentDir[PATH_MAX];
                getcwd(currentDir, sizeof(currentDir));

                // Récupérer les informations de l'utilisateur courant
                struct passwd *pw = getpwuid(getuid());
                if (pw == nullptr)
                {
                    std::cerr << "Error: Unable to retrieve user information.\n";
                    return;
                }

                const char *homeDir = pw->pw_dir;

                // Changer le répertoire courant vers le home
                chdir(homeDir);

                // récupérer les 4 derniers bytes avant les 4 derniers bytes, qui contiennent le checksum.
                std::vector<unsigned char>
                    last_4_bytes(buffer.end() - 4, buffer.end());

                // récupérer les 4 derniers bytes avant les 4 derniers bytes, qui précisent si la compression est activée ou non.
                std::vector<unsigned char> four_bytes_before_last_four_bytes(buffer.end() - 8, buffer.end() - 4);

                // Convert last 4 bytes to uint32_t (checksum)
                uint32_t compression_status = 0;
                for (size_t i = 0; i < 4; ++i)
                {
                    compression_status |= (four_bytes_before_last_four_bytes[i] << (8 * i)); // Little-endian order
                }

                compressFlag = static_cast<bool>(compression_status);

                // Réinitialiser les 4 derniers octets à 0
                for (int i = 0; i < 4; ++i)
                {
                    buffer[buffer.size() - 1 - i] = 0;
                }

                // // Compute the checksum
                uint32_t calculated_checksum = calculate_murmurhash3(buffer);

                // Convert last 4 bytes to uint32_t (checksum)
                uint32_t checksum = 0;
                for (size_t i = 0; i < 4; ++i)
                {
                    checksum |= (last_4_bytes[i] << (8 * i)); // Little-endian order
                }

                // Envoyer la confirmation via TCP avec ré-essai
                int retries = 0;
                bool ack_sent = false;

                std::string message = "INCORRECT CRC";

                if (checksum == calculated_checksum)
                {
                    std::cout << "\nFichier Destination: " << filePath << std::endl;
                    message = std::to_string(seq_num);
                }
                else
                {
                    std::cerr << "Corrupted packet received: #" << seq_num << std::endl; // Log actual sequence number
                    packet_corrupted = true;
                }

                bool foundUnescapedSlash = false;

                // Iterate through the file path to find unescaped forward slashes
                for (size_t i = 0; i < filePath.length(); ++i)
                {
                    if (filePath[i] == '/')
                    {
                        // Check if the slash is unescaped (not preceded by a backslash)
                        if (i == 0 || filePath[i - 1] != '\\')
                        {
                            foundUnescapedSlash = true;
                            break;
                        }
                    }
                }

                std::string pathCheck = "";
                if (foundUnescapedSlash)
                {
                    // Call the function to check problematic components
                    pathCheck = checkProblematicComponent(filePath);
                }

                // If a problematic component is found, update the message
                if (!pathCheck.empty())
                {
                    message = pathCheck;
                }

                std::ifstream file_check(filePath);
                if (file_check.good())
                {
                    // File exists, open it for reading and writing
                    existing_file = true;
                    output_file.open(filePath, std::ios::in | std::ios::out | std::ios::binary);
                }
                else
                {
                    // File does not exist, create a new file and open it for writing
                    output_file.open(filePath, std::ios::out | std::ios::binary);
                }

                chdir(currentDir);

                // Envoyer une confirmation pour le nom du fichier
                while (retries < MAX_RETRIES && !ack_sent)
                {
                    ssize_t bytes_sent = send(tcp_socket, message.c_str(), message.length(), 0);

                    if (bytes_sent > 0)
                    {
                        ack_sent = true;
                        // std::cout << "\nConfirmation TCP envoyée pour le nom de fichier" << std::endl;
                    }
                    else
                    {
                        retries++; // Increment retries on failure
                        std::cerr << "Failed to send TCP confirmation, retrying (" << retries << "/" << MAX_RETRIES << ")" << std::endl;
                    }
                }

                if (!ack_sent)
                {
                    std::cerr << "Failed to send TCP confirmation after " << MAX_RETRIES << " retries." << std::endl;
                    return;
                };

                if (!output_file)
                {
                    std::cerr << "Erreur d'ouverture du fichier de sortie : " << filePath << std::endl;
                    return;
                }
                buffer.resize(BUFFER_SIZE + 1024);
                continue;
            }

            // Vérifier si le fichier est correctement ouvert
            if (!output_file.is_open())
            {
                std::cerr << "Erreur : fichier non ouvert." << std::endl;
                return;
            }

            // Si ce paquet n'a pas encore été reçu, l'ajouter et écrire dans le fichier
            if (received_sequence_numbers.find(seq_num) == received_sequence_numbers.end())
            {
                std::vector<unsigned char> last_4_bytes(buffer.begin() + buffer.size() - 4, buffer.end());

                // Réinitialiser les 4 derniers octets à 0
                for (int i = 0; i < 4; ++i)
                {
                    buffer[buffer.size() - 1 - i] = 0;
                }

                // // Compute the checksum
                uint32_t calculated_checksum = calculate_murmurhash3(buffer);

                // Convert last 4 bytes to uint32_t (checksum)
                uint32_t checksum = 0;
                for (size_t i = 0; i < 4; ++i)
                {
                    checksum |= (last_4_bytes[i] << (8 * i)); // Little-endian order
                }

                std::string message = "INCORRECT CRC";

                if (checksum == calculated_checksum)
                {
                    packet_corrupted = false;
                    message = std::to_string(seq_num);
                    if (compressFlag)
                    {
                        std::cout << "Compress flag: #" << seq_num << std::endl; // Log actual sequence number
                        if (!decompressChunk(buffer))
                        {
                            std::cerr << "Unable to decompress packet received: #" << seq_num << std::endl; // Log actual sequence number
                            message = "FAILED DECOMPRESSION";
                        }
                        else
                        {
                            // Écrire les données dans le fichier
                            output_file.write(buffer.data(), buffer.size());
                        }
                    }
                    else
                    {
                        output_file.write(reinterpret_cast<const char *>(buffer.data() + HEADER_SIZE), n - HEADER_SIZE - FOOTER_SIZE);
                    }
                    writed_packet++;
                    received_sequence_numbers.insert(seq_num);
                    std::cout << "Paquet #" << seq_num << " reçu et écrit dans le fichier." << std::endl;
                }
                else
                {
                    packet_corrupted = true;
                    hexDump(buffer_bak);
                    hexDump(buffer);
                    std::cerr << "Corrupted packet received: #" << seq_num << std::endl; // Log actual sequence number
                    std::cout << "Calculated checksum (MurmurHash3): 0x" << std::hex << calculated_checksum << std::endl;
                    std::cout << "Extracted checksum (from last 4 bytes): 0x" << std::hex << checksum << std::dec << std::endl;
                }

                // Envoyer la confirmation via TCP avec ré-essai
                int retries = 0;
                bool ack_sent = false;

                // Envoyer une confirmation pour le nom du fichier
                while (retries < MAX_RETRIES && !ack_sent)
                {
                    ssize_t bytes_sent = send(tcp_socket, message.c_str(), message.length(), 0);

                    if (bytes_sent > 0)
                    {
                        ack_sent = true;
                        std::cout << "Confirmation TCP envoyée pour le paquet #" << seq_num << std::endl;
                    }
                    else
                    {
                        retries++; // Increment retries on failure
                        std::cerr << "Échec d'envoi de la confirmation TCP pour le paquet #" << seq_num << ", tentative " << retries << std::endl;
                    }
                }

                if (!ack_sent)
                {
                    std::cerr << "Failed to send TCP confirmation after " << MAX_RETRIES << " retries." << std::endl;
                    return;
                }

                if (checksum != calculated_checksum)
                {
                    continue;
                }
            }
            else
            {
                std::cout << "Paquet déjà reçu, ignoré : #" << seq_num << std::endl;
            }
        }
        buffer.clear();
        buffer.resize(next_buffer_size);
    }

    // Fermeture du fichier après l'écriture
    output_file.close();
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

    int opt = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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
    if (listen(tcp_socket, 1) < 0)
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

    udp_thread.join();
    tcp_thread.join();

    close(udp_socket);
    close(tcp_socket);
    return 0;
}
