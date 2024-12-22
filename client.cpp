#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <vector> // Ajout de l'en-tête nécessaire pour std::vector
#include <zlib.h> // Pour la compression
#include <chrono> // Pour mesurer le temps d'envoi

#define DEFAULT_PORT 12345
#define DEFAULT_SERVER "127.0.0.1"
#define CHUNK_SIZE 1024

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
    for (int i = 0; i < 50; i++) {
        if (i < progress / 2) std::cout << "#";
        else std::cout << " ";
    }
    std::cout << "] " << progress << "%  Rate: " << transferRate << " KB/s";
    std::flush(std::cout);
}

bool fileExists(const std::string &fileName)
{
    std::ifstream file(fileName);
    return file.is_open();
}

void sendFileMetadata(int sockfd, const std::string &fileName, size_t fileSize)
{
    std::string metadata = fileName + "\0" + std::to_string(fileSize);
    if (send(sockfd, metadata.c_str(), metadata.size(), 0) == -1)
    {
        std::cerr << "Error sending file metadata!" << std::endl;
        exit(1);
    }
}

void logError(const std::string &message)
{
    std::cerr << "Error: " << message << std::endl;
}

void setupClient(int &clientSocket, sockaddr_in &serverAddr, const std::string &serverIP, int port, bool verbose)
{
    if (verbose)
    {
        std::cout << "Creating socket...\n";
    }

    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1)
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

bool compressChunk(const std::vector<char> &input, std::vector<char> &output, bool verbose)
{
    uLongf compressedSize = compressBound(input.size());
    output.resize(compressedSize);

    int result = compress2(reinterpret_cast<Bytef *>(output.data()), &compressedSize,
                          reinterpret_cast<const Bytef *>(input.data()), input.size(), Z_BEST_COMPRESSION);
    if (result != Z_OK)
    {
        logError("Compression failed for chunk!");
        return false;
    }
    output.resize(compressedSize);

    if (verbose)
    {
        std::cout << "Chunk compressed, size after compression: " << compressedSize << " bytes.\n";
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

void sendFile(int sockfd, const char *fileName, bool compressFlag, bool verbose)
{
    if (!fileExists(fileName))
    {
        logError("File does not exist!");
        return;
    }

    std::ifstream file(fileName, std::ios::binary);
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
    auto startTime = std::chrono::steady_clock::now();

    if (verbose)
    {
        std::cout << "File size: " << fileSize << " bytes. Sending file...\n";
    }

    // Send file metadata
    sendFileMetadata(sockfd, fileName, fileSize);

    // Envoyer le fichier par morceaux
    while (bytesSent < fileSize)
    {
        size_t bytesToRead = my_min(static_cast<size_t>(CHUNK_SIZE), fileSize - bytesSent);
        size_t readBytes = readFileChunk(file, buffer, bytesToRead);
        
        if (compressFlag)
        {
            // Compresser le chunk
            if (!compressChunk(std::vector<char>(buffer, buffer + readBytes), compressedChunk, verbose))
            {
                logError("Failed to compress chunk!");
                return;
            }

            // Envoyer le chunk compressé
            if (send(sockfd, compressedChunk.data(), compressedChunk.size(), 0) == -1)
            {
                logError("Error sending compressed data!");
                return;
            }

            bytesSent += readBytes;
        }
        else
        {
            // Envoyer le chunk sans compression
            if (send(sockfd, buffer, readBytes, 0) == -1)
            {
                logError("Error sending uncompressed data!");
                return;
            }

            bytesSent += readBytes;
        }

        // Calcul de la durée écoulée pour calculer le taux de transfert
        auto elapsedTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();

        // Afficher la barre de progression et le taux
        showProgress(bytesSent, fileSize, elapsedTime);
    }

    std::cout << std::endl; // Nouvelle ligne après la barre de progression

    if (verbose)
    {
        std::cout << "File sent successfully!\n";
    }

    file.close(); // Explicitly close the file
}

int main(int argc, char *argv[])
{
    std::string serverIP = DEFAULT_SERVER;
    int port = DEFAULT_PORT;
    std::string fileName;
    bool compressFlag = false;
    bool verbose = false;

    // Parsing des options en ligne de commande
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
            fileName = optarg;
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

    if (fileName.empty())
    {
        logError("No file specified!");
        return 1;
    }

    int clientSocket;
    sockaddr_in serverAddr;
    setupClient(clientSocket, serverAddr, serverIP, port, verbose);

    // Connexion au serveur
    if (connect(clientSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        logError("Connection failed!");
        return 1;
    }

    if (verbose)
    {
        std::cout << "Connected to server " << serverIP << ":" << port << "!\n";
    }

    // Envoi du fichier
    sendFile(clientSocket, fileName.c_str(), compressFlag, verbose);

    closeSocket(clientSocket);  // Ensure socket is closed after use
    return 0;
}
