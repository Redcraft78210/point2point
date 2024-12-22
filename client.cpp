#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <vector>
#include <zlib.h>
#include <chrono>

#define DEFAULT_PORT 12345
#define DEFAULT_SERVER "127.0.0.1"
#define CHUNK_SIZE 50000
#define ACK_BUFFER_SIZE 1024

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

    std::cout << "\rProgress: [";
    for (int i = 0; i < 50; i++)
    {
        if (i < progress / 2)
            std::cout << "#";
        else
            std::cout << " ";
    }
    std::cout << "] " << progress << "%  Rate: " << transferRate << " KB/s";
    std::flush(std::cout);
}

bool fileExists(const std::string &fileName)
{
    std::ifstream file(fileName);
    return file.is_open();
}

void sendFileMetadata(int sockfd, const std::string &fileName, size_t fileSize, sockaddr_in &serverAddr)
{
    // Convertir la taille du fichier en chaîne
    std::string fileSizeStr = std::to_string(fileSize);

    // Calcul de la taille totale des métadonnées : nom du fichier + '\0' + taille du fichier
    size_t metadataSize = fileName.size() + 1 + fileSizeStr.size(); // +1 pour '\0'

    // Vérifier si la taille est raisonnable pour éviter les débordements
    if (metadataSize > 1024)
    { // Par exemple, limiter la taille à 1024 octets
        std::cerr << "Metadata size is too large!" << std::endl;
        return; // Eviter de poursuivre l'exécution si la taille est trop grande
    }

    // Allocation de mémoire pour les métadonnées
    char *metadata = new char[metadataSize];

    // Copier le nom du fichier et la taille dans le buffer
    memcpy(metadata, fileName.c_str(), fileName.size());
    metadata[fileName.size()] = '\0'; // Ajouter explicitement le terminator null
    memcpy(metadata + fileName.size() + 1, fileSizeStr.c_str(), fileSizeStr.size());

    // Envoyer les métadonnées
    ssize_t sentBytes = sendto(sockfd, metadata, metadataSize, 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (sentBytes == -1)
    {
        std::cerr << "Error sending file metadata!" << std::endl;
        delete[] metadata;
        return; // Retour au lieu de exit(1) pour permettre une gestion plus souple des erreurs
    }

    std::cout << "File metadata sent successfully." << std::endl;

    delete[] metadata; // Libérer la mémoire allouée
}

void logError(const std::string &message)
{
    std::cerr << "Error: " << message << std::endl;
}

void setupClient(int &serverSocket, sockaddr_in &serverAddr, const std::string &serverIP, int port, bool verbose)
{
    if (verbose)
    {
        std::cout << "Creating socket...\n";
    }

    serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == -1)
    {
        logError("Error creating socket!");
        exit(1);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(serverIP.c_str());

    if (verbose)
    {
        std::cout << "Socket created, preparing to connect to " << serverIP << ":" << port << "...\n";
    }
}

bool compressChunkWithFallback(const std::vector<char> &input, std::vector<char> &output, bool &fallbackToUncompressed, bool verbose)
{
    uLongf compressedSize = compressBound(input.size());
    output.resize(compressedSize);

    int result = compress2(reinterpret_cast<Bytef *>(output.data()), &compressedSize,
                           reinterpret_cast<const Bytef *>(input.data()), input.size(), Z_BEST_COMPRESSION);
    if (result != Z_OK)
    {
        fallbackToUncompressed = true; // Indique un échec de compression
        if (verbose)
        {
            std::cerr << "Compression failed, fallback to uncompressed.\n";
        }
        return false;
    }

    output.resize(compressedSize);

    if (compressedSize > CHUNK_SIZE) // Vérifie si la taille compressée dépasse la limite
    {
        fallbackToUncompressed = true; // Indique que le chunk compressé est trop gros
        if (verbose)
        {
            std::cerr << "Compressed chunk exceeds size limit, fallback to uncompressed.\n";
        }
        return false;
    }

    return true;
}

size_t readFileChunk(std::ifstream &file, char *buffer, size_t chunkSize)
{
    file.read(buffer, chunkSize);
    return file.gcount();
}

void closeSocket(int sockfd)
{
    if (close(sockfd) == -1)
    {
        logError("Error closing socket!");
        exit(1);
    }
}

void sendFile(int sockfd, const char *filePath, bool compressFlag, bool verbose, sockaddr_in &serverAddr)
{
    if (!fileExists(filePath))
    {
        logError("File does not exist!");
        return;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        logError("Error opening file!");
        return;
    }

    size_t bytesSent = 0;
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    char buffer[CHUNK_SIZE];
    std::vector<char> compressedChunk;
    bool fallbackToUncompressed = false;
    auto startTime = std::chrono::steady_clock::now();

    if (verbose)
    {
        std::cout << "File size: " << fileSize << " bytes. Sending file...\n";
    }
    std::string fileName = std::string(filePath).substr(std::string(filePath).find_last_of("/\\") + 1);

    // Send file metadata
    sendFileMetadata(sockfd, fileName, fileSize, serverAddr);

    // Wait for acknowledgment before starting transfer
    char ackBuffer[ACK_BUFFER_SIZE];
    sockaddr_in ackAddr;
    socklen_t ackLen = sizeof(ackAddr);

    while (bytesSent < fileSize)
    {
        size_t bytesToRead = my_min(static_cast<size_t>(CHUNK_SIZE), fileSize - bytesSent);
        size_t readBytes = readFileChunk(file, buffer, bytesToRead);

        if (compressFlag)
        {
            if (!compressChunkWithFallback(std::vector<char>(buffer, buffer + readBytes), compressedChunk, fallbackToUncompressed, verbose))
            {
                // If compression fails, send uncompressed data
                if (sendto(sockfd, buffer, readBytes, 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
                {
                    logError("Error sending uncompressed data after compression failed!");
                    return;
                }
                fallbackToUncompressed = false;
            }
            else
            {
                // Send compressed data
                if (sendto(sockfd, compressedChunk.data(), compressedChunk.size(), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
                {
                    logError("Error sending compressed data!");
                    return;
                }
            }
        }
        else
        {
            if (sendto(sockfd, buffer, readBytes, 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
            {
                logError("Error sending uncompressed data!");
                return;
            }
        }

        // Wait for acknowledgment (server response)
        ssize_t ackReceived = recvfrom(sockfd, ackBuffer, ACK_BUFFER_SIZE, 0, (struct sockaddr *)&ackAddr, &ackLen);
        if (ackReceived == -1)
        {
            logError("Error receiving acknowledgment!");
            return;
        }

        // If decompression failed on the server, we need to resend the data
        if (std::string(ackBuffer, ackReceived) == "Decompression failed. Please re-compress and resend.")
        {
            std::cerr << "\nDecompression failed on server. Retrying...\n";
            bytesSent -= readBytes; // Rewind the sent bytes for retry
        }

        bytesSent += readBytes;
        auto elapsedTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();

        // Display progress
        size_t totalBytesSent = bytesSent;
        showProgress(totalBytesSent, fileSize, elapsedTime);
    }

    std::cout << std::endl;
    if (verbose)
    {
        std::cout << "File sent successfully!\n";
    }

    file.close();
}

int main(int argc, char *argv[])
{
    std::string serverIP = DEFAULT_SERVER;
    int port = DEFAULT_PORT;
    std::string filePath;
    bool compressFlag = false;
    bool verbose = false;

    // Parse command-line options
    struct option longOpts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"file", required_argument, nullptr, 'f'},
        {"port", required_argument, nullptr, 'p'},
        {"address", required_argument, nullptr, 'a'},
        {"compress", no_argument, nullptr, 'c'},
        {"verbose", no_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "h:f:p:a:c:v", longOpts, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'h':
            showUsage();
            return 0;
        case 'f':
            filePath = optarg;
            break;
        case 'p':
            port = std::stoi(optarg);
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

    if (filePath.empty())
    {
        logError("No file specified!");
        return 1;
    }

    int serverSocket;
    sockaddr_in serverAddr;
    setupClient(serverSocket, serverAddr, serverIP, port, verbose);

    // Send the file
    sendFile(serverSocket, filePath.c_str(), compressFlag, verbose, serverAddr);

    closeSocket(serverSocket); // Ensure socket is closed after use
    return 0;
}
