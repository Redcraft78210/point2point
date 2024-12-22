#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <memory> // For std::unique_ptr

#include <getopt.h>
#include <vector>
#include <zlib.h>
#include <chrono>

#define DEFAULT_PORT 12345
#define DEFAULT_SERVER "127.0.0.1"
#define CHUNK_SIZE 50000
#define ACK_BUFFER_SIZE 1024

#define MAX_CHUNK_SIZE 1000000        // Max chunk size (1 MB)
#define MIN_CHUNK_SIZE 1024           // Min chunk size (1 KB)
#define BANDWIDTH_UPDATE_INTERVAL 1.0 // Interval to update bandwidth in seconds

template <typename T>
T my_min(T a, T b)
{
    return a < b ? a : b;
}

template <typename T>
T my_max(T a, T b)
{
    return a > b ? a : b;
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
    std::string fileSizeStr = std::to_string(fileSize);
    size_t metadataSize = fileName.size() + 1 + fileSizeStr.size();

    if (metadataSize > 1024) // Metadata size limit check
    {
        std::cerr << "Metadata size is too large!" << std::endl;
        return;
    }

    char *metadata = new char[metadataSize];
    memcpy(metadata, fileName.c_str(), fileName.size());
    metadata[fileName.size()] = '\0';
    memcpy(metadata + fileName.size() + 1, fileSizeStr.c_str(), fileSizeStr.size());

    ssize_t sentBytes = sendto(sockfd, metadata, metadataSize, 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (sentBytes == -1)
    {
        std::cerr << "Error sending file metadata!" << std::endl;
        delete[] metadata;
        return;
    }

    std::cout << "File metadata sent successfully." << std::endl;
    delete[] metadata;
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
        fallbackToUncompressed = true;
        if (verbose)
        {
            std::cerr << "Compression failed, fallback to uncompressed.\n";
        }
        return false;
    }

    output.resize(compressedSize);

    if (compressedSize > CHUNK_SIZE)
    {
        fallbackToUncompressed = true;
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

void adjustBufferSize(size_t &currentChunkSize, double transferRate)
{
    if (transferRate > 0)
    {
        if (transferRate > 1000)
        {
            currentChunkSize = std::min(currentChunkSize * 2, (size_t)MAX_CHUNK_SIZE);
        }
        else if (transferRate < 200)
        {
            currentChunkSize = std::max(currentChunkSize / 2, (size_t)MIN_CHUNK_SIZE);
        }
    }
}

void sendFile(int sockfd, const char *filePath, bool compressFlag, bool verbose, sockaddr_in &serverAddr)
{
    // Check if file exists
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Error: File does not exist or cannot be opened!\n";
        return;
    }

    // File size calculation
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    size_t bytesSent = 0;
    size_t currentChunkSize = MIN_CHUNK_SIZE;

    // Create buffer using unique_ptr for automatic memory management
    std::unique_ptr<char[]> buffer(new char[MAX_CHUNK_SIZE]);

    if (verbose)
    {
        std::cout << "File size: " << fileSize << " bytes. Sending file...\n";
    }

    // Extract file name from the file path
    std::string fileName = std::string(filePath).substr(std::string(filePath).find_last_of("/\\") + 1);

    // Send file metadata (e.g., fileName and fileSize)
    sendFileMetadata(sockfd, fileName, fileSize, serverAddr);

    auto startTime = std::chrono::steady_clock::now();
    auto lastBandwidthUpdate = startTime;

    while (bytesSent < fileSize)
    {
        size_t bytesToRead = std::min(currentChunkSize, fileSize - bytesSent);
        size_t readBytes = readFileChunk(file, buffer.get(), bytesToRead);

        if (readBytes == 0)
            break; // End of file

        // Send the chunk
        if (sendto(sockfd, buffer.get(), readBytes, 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
        {
            std::cerr << "Error: Failed to send data!\n";
            return;
        }

        // Wait for acknowledgment (server response)
        char ackBuffer[ACK_BUFFER_SIZE];
        sockaddr_in ackAddr;
        socklen_t ackLen = sizeof(ackAddr);
        ssize_t ackReceived = recvfrom(sockfd, ackBuffer, ACK_BUFFER_SIZE, 0, (struct sockaddr *)&ackAddr, &ackLen);

        if (ackReceived == -1)
        {
            std::cerr << "Error: Failed to receive acknowledgment. Error code: " << errno << "\n";
            return;
        }

        // Check for decompression failure
        if (std::string(ackBuffer, ackReceived) == "Decompression failed. Please resend the chunk.")
        {
            std::cerr << "\nDecompression failed on server. Retrying...\n";
        }
        else
        {
            bytesSent += readBytes;

            // Calculate elapsed time and adjust chunk size
            auto currentTime = std::chrono::steady_clock::now();
            double elapsedTime = std::chrono::duration<double>(currentTime - startTime).count();
            if (std::chrono::duration<double>(currentTime - lastBandwidthUpdate).count() > BANDWIDTH_UPDATE_INTERVAL)
            {
                double transferRate = (bytesSent / 1024.0) / elapsedTime; // KB/s
                adjustBufferSize(currentChunkSize, transferRate);
                lastBandwidthUpdate = currentTime;

                if (verbose)
                {
                    std::cout << "\rBytes sent: " << bytesSent
                              << "/" << fileSize
                              << " (" << (100 * bytesSent / fileSize) << "%)"
                              << ", Current chunk size: " << currentChunkSize
                              << ", Transfer rate: " << transferRate << " KB/s"
                              << std::flush;
                }
            }
        }
    }

    if (verbose)
        std::cout << "\nFile transfer completed successfully.\n";
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
