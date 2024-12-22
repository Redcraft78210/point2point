#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <getopt.h>
#include <zlib.h>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cerrno>  // For errno
#include <cstring> // For strerror()

#define DEFAULT_PORT 12345
#define DEFAULT_CHUNK_SIZE 4096
#define ACK_BUFFER_SIZE 256

void showUsage()
{
    std::cout << "Usage: file_receiver [options]\n";
    std::cout << "  -h, --help            Display help\n";
    std::cout << "  -p, --port <port>      Port to listen on (default: 12345)\n";
    std::cout << "  -f, --file <file>      Destination file\n";
    std::cout << "  -c, --decompress       Enable decompression\n";
    std::cout << "  -v, --verbose          Show detailed information\n";
}

// Error logging function
void logError(const std::string &message)
{
    std::cerr << "Error: " << message << std::endl;
}

// Fonction pour mesurer la bande passante
double calculateBandwidth(size_t bytesReceived, double elapsedTime)
{
    return (bytesReceived / 1024.0) / elapsedTime; // Ko/s
}

// Fonction pour ajuster dynamiquement la taille du buffer en fonction de la bande passante
size_t adjustBufferSize(double bandwidth)
{
    size_t dynamicChunkSize = DEFAULT_CHUNK_SIZE;
    if (bandwidth > 1000) // Si la bande passante est supérieure à 1 Mo/s, augmentez le buffer
    {
        dynamicChunkSize = 8192; // Augmentez la taille du buffer (par exemple à 8 Ko)
    }
    else if (bandwidth < 100) // Si la bande passante est inférieure à 100 Ko/s, réduisez le buffer
    {
        dynamicChunkSize = 1024; // Réduisez la taille du buffer (par exemple à 1 Ko)
    }
    return dynamicChunkSize;
}

// Function to decompress a chunk of data
bool decompressChunk(const std::vector<char> &input, std::vector<char> &output, bool verbose)
{
    uLongf outputSize = input.size() * 2; // Initial guess for decompressed size
    std::vector<char> tempBuffer(outputSize);
    int result;

    while (true)
    {
        result = uncompress(reinterpret_cast<Bytef *>(tempBuffer.data()), &outputSize,
                            reinterpret_cast<const Bytef *>(input.data()), input.size());

        if (result == Z_OK)
        {
            // Decompression successful
            break;
        }
        else if (result == Z_BUF_ERROR)
        {
            // Buffer size insufficient, double the buffer size
            outputSize *= 2;
            tempBuffer.resize(outputSize);
        }
        else
        {
            // Decompression error
            std::cerr << "Decompression error: " << result << "\n";
            return false;
        }
    }

    // Resize output to the actual decompressed size
    output.assign(tempBuffer.begin(), tempBuffer.begin() + outputSize);

    if (verbose)
    {
        std::cout << "Decompressed chunk successfully. Original size: " << input.size()
                  << ", Decompressed size: " << outputSize << " bytes.\n";
    }

    return true;
}

// Fonction de réception et de sauvegarde du fichier (ajustée)
void saveReceivedFile(int serverSocket, sockaddr_in &serverAddr, bool decompressFlag, bool verbose)
{
    char metadataBuffer[256];
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    ssize_t metadataReceived = recvfrom(serverSocket, metadataBuffer, sizeof(metadataBuffer), 0,
                                        (struct sockaddr *)&clientAddr, &clientAddrLen);

    if (metadataReceived <= 0)
    {
        if (metadataReceived == 0)
        {
            logError("No data received!");
        }
        else
        {
            logError("Failed to receive metadata! Error: " + std::string(strerror(errno)));
        }
        return;
    }

    // Extraction du nom de fichier et de la taille (inchangé)
    std::string metadata(metadataBuffer, metadataReceived);
    size_t delimiterPos = metadata.find('\0');
    if (delimiterPos == std::string::npos)
    {
        logError("Invalid metadata format received! No delimiter found.");
        return;
    }

    std::string fileName = metadata.substr(0, delimiterPos);
    std::string fileSizeStr = metadata.substr(delimiterPos + 1);
    size_t fileSize = std::stoull(fileSizeStr);

    std::cout << "Receiving file: " << fileName << ", size: " << fileSize << " bytes\n";

    std::ofstream outFile(fileName, std::ios::binary);
    if (!outFile.is_open())
    {
        logError("Error opening output file!");
        return;
    }


    char buffer[CHUNK_SIZE];
    size_t totalBytesWritten = 0;
    ssize_t bytesReceived = 0;
    std::vector<char> decompressedChunk;
    auto startTime = std::chrono::steady_clock::now();
    size_t chunkSize = DEFAULT_CHUNK_SIZE; // Taille du buffer dynamique initiale

    if (verbose) {
        std::cout << "Receiving file...\n";
    }
    
    while ((bytesReceived = recvfrom(serverSocket, metadataBuffer, chunkSize, 0,
                                     (struct sockaddr *)&clientAddr, &clientAddrLen)) > 0)
    {
        if (bytesReceived == -1)
        {
            logError("Error receiving data from client. Error: " + std::string(strerror(errno)));
            break;
        }

        auto elapsedTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        double bandwidth = calculateBandwidth(totalBytesWritten, elapsedTime);

        // Ajuster la taille du buffer en fonction de la bande passante
        chunkSize = adjustBufferSize(bandwidth);

        if (decompressFlag)
        {
            bool decompressionSuccess = decompressChunk(std::vector<char>(metadataBuffer, metadataBuffer + bytesReceived), decompressedChunk, verbose);

            if (!decompressionSuccess)
            {
                std::cerr << "Decompression error. Waiting for the client to resend the chunk.\n";
                const char *ackMessage = "Decompression failed. Please resend the chunk.";
                ssize_t ackSent = sendto(serverSocket, ackMessage, strlen(ackMessage), 0,
                                         (struct sockaddr *)&clientAddr, sizeof(clientAddr));
                if (ackSent == -1)
                {
                    logError("Error sending decompression error message to client.");
                    break;
                }
                continue;
            }

            outFile.write(decompressedChunk.data(), decompressedChunk.size());
            totalBytesWritten += decompressedChunk.size();
        }
        else
        {
            outFile.write(metadataBuffer, bytesReceived);
            totalBytesWritten += bytesReceived;
        }

        // Send error message back to client asking for re-compression or resending the chunk
        const char *ackMessage = "1";
        ssize_t ackSent = sendto(serverSocket, ackMessage, strlen(ackMessage), 0,
                                 (struct sockaddr *)&clientAddr, sizeof(clientAddr));
        if (ackSent == -1)
        {
            logError("Error sending decompression success message to client.");
            break;
        }

        if (verbose)
        {
            std::cout << "Progress: " << totalBytesWritten << " / " << fileSize << " bytes written.\n";
        }

        if (totalBytesWritten >= fileSize)
        {
            break;
        }
    }

    outFile.close();
    std::cout << "File reception completed.\n";
}

// Function to create and bind the server socket
int createServerSocket(int port, bool verbose)
{
    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == -1)
    {
        logError("Error creating socket!");
        return -1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    // serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_addr.s_addr = inet_addr("0.0.0.0"); // Adresse localhost

    if (bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        logError("Error binding socket!");
        close(serverSocket);
        return -1;
    }

    if (verbose)
    {
        std::cout << "Server listening on port " << port << "...\n";
    }

    return serverSocket;
}

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    std::string outputFileName = "received_file";
    bool decompressFlag = false;
    bool verbose = false;

    struct option longOpts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"port", required_argument, nullptr, 'p'},
        {"file", required_argument, nullptr, 'f'},
        {"decompress", no_argument, nullptr, 'c'},
        {"verbose", no_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:f:cv", longOpts, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'h':
            showUsage();
            return 0;
        case 'p':
            port = std::stoi(optarg);
            break;
        case 'f':
            outputFileName = optarg;
            break;
        case 'c':
            decompressFlag = true;
            break;
        case 'v':
            verbose = true;
            break;
        default:
            showUsage();
            return 1;
        }
    }

    // Create the server socket and bind it
    int serverSocket = createServerSocket(port, verbose);
    if (serverSocket == -1)
        return 1;

    sockaddr_in serverAddr{};
    saveReceivedFile(serverSocket, serverAddr, decompressFlag, verbose);

    // Close the socket
    close(serverSocket);

    return 0;
}
