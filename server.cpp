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
#define CHUNK_SIZE 4096
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

// Function to receive and save the file
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
            // Log the error message and errno description
            logError("Failed to receive metadata! Error: " + std::string(strerror(errno)));
        }
        return;
    }

    // Vérification du format de la donnée reçue
    std::string metadata(metadataBuffer, metadataReceived);
    size_t delimiterPos = metadata.find('\0'); // Cherche le séparateur '\0'
    if (delimiterPos == std::string::npos)
    {
        logError("Invalid metadata format received! No delimiter found.");
        return;
    }

    std::string fileName = metadata.substr(0, delimiterPos);

    // Assurer que la taille du fichier est correctement extraite après le délimiteur
    std::string fileSizeStr = metadata.substr(delimiterPos + 1);
    size_t fileSize = 0;

    try
    {
        // Convertir la chaîne en taille
        fileSize = std::stoull(fileSizeStr);
    }
    catch (const std::exception &e)
    {
        logError("Failed to parse file size from metadata! Error: " + std::string(e.what()));
        return;
    }

    std::cout << "Receiving file: " << fileName << ", size: " << fileSize << " bytes\n";

    // Ouverture du fichier en écriture
    std::ofstream outFile(fileName, std::ios::binary);
    if (!outFile.is_open())
    {
        logError("Error opening output file!");
        return;
    }

    size_t totalBytesWritten = 0; // Variable pour suivre la progression
    ssize_t bytesReceived = 0;
    std::vector<char> decompressedChunk;

    // Boucle pour recevoir les données
    while ((bytesReceived = recvfrom(serverSocket, metadataBuffer, sizeof(metadataBuffer), 0,
                                     (struct sockaddr *)&clientAddr, &clientAddrLen)) > 0)
    {
        if (bytesReceived == -1)
        {
            logError("Error receiving data from client. Error: " + std::string(strerror(errno)));
            break;
        }

        if (decompressFlag)
        {
            // Décompression des données reçues
            bool decompressionSuccess = decompressChunk(std::vector<char>(metadataBuffer, metadataBuffer + bytesReceived), decompressedChunk, verbose);

            if (!decompressionSuccess)
            {
                std::cerr << "Decompression error. Waiting for the client to resend the chunk.\n";

                // Send error message back to client asking for re-compression or resending the chunk
                const char *ackMessage = "Decompression failed. Please resend the chunk.";
                ssize_t ackSent = sendto(serverSocket, ackMessage, strlen(ackMessage), 0,
                                         (struct sockaddr *)&clientAddr, sizeof(clientAddr));
                if (ackSent == -1)
                {
                    logError("Error sending decompression error message to client.");
                    break;
                }

                // Continue to the next iteration to wait for the client to resend the chunk
                continue;
            }else {
                // Send error message back to client asking for re-compression or resending the chunk
                const char *ackMessage = "1";
                ssize_t ackSent = sendto(serverSocket, ackMessage, strlen(ackMessage), 0,
                                         (struct sockaddr *)&clientAddr, sizeof(clientAddr));
                if (ackSent == -1)
                {
                    logError("Error sending decompression success message to client.");
                    break;
                }
            }

            // Écriture des données décompressées dans le fichier
            outFile.write(decompressedChunk.data(), decompressedChunk.size());
            totalBytesWritten += decompressedChunk.size();
        }
        else
        {
            // Écriture des données directement dans le fichier
            outFile.write(metadataBuffer, bytesReceived);
            totalBytesWritten += bytesReceived;
        }

        // Affichage de progression
        if (verbose)
        {
            std::cout << "Progress: " << totalBytesWritten << " / " << fileSize << " bytes written.\n";
        }

        // Stop if we have received the full file size
        if (totalBytesWritten >= fileSize)
        {
            break;
        }
    }

    std::cout << "\nFile reception completed.\n";

    // Vérification finale et fermeture des ressources
    if (bytesReceived == -1)
    {
        logError("Error receiving data from client.");
    }
    else if (verbose)
    {
        std::cout << "File received successfully! Total bytes written: " << totalBytesWritten << " bytes.\n";
    }

    outFile.close(); // Fermeture explicite du fichier
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
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Adresse localhost

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
