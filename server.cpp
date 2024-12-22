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
#include <cerrno>
#include <cstring>

#define DEFAULT_PORT 12345
#define CHUNK_SIZE 4096
#define ACK_BUFFER_SIZE 256

void showUsage()
{
    std::cout << "Usage: file_receiver [options]\n";
    std::cout << "  -h, --help            Display help\n";
    std::cout << "  -p, --port <port>     Port to listen on (default: 12345)\n";
    std::cout << "  -f, --file <file>     Destination file\n";
    std::cout << "  -c, --decompress      Enable decompression\n";
    std::cout << "  -v, --verbose         Show detailed information\n";
}

void logError(const std::string &message)
{
    std::cerr << "Error: " << message << std::endl;
}

bool decompressChunk(const std::vector<char> &input, std::vector<char> &output, bool verbose)
{
    uLongf outputSize = input.size() * 2;
    std::vector<char> tempBuffer(outputSize);

    while (true)
    {
        const uLongf MAX_BUFFER_SIZE = 1024 * 1024 * 1024; // 1 GB cap for example
        if (outputSize > MAX_BUFFER_SIZE)
        {
            if (verbose)
            {
                std::cerr << "Decompression buffer size exceeded maximum limit.\n";
            }
            return false;
        }

        int result = uncompress(reinterpret_cast<Bytef *>(tempBuffer.data()), &outputSize,
                                reinterpret_cast<const Bytef *>(input.data()), input.size());

        if (result == Z_OK)
        {
            output.assign(tempBuffer.begin(), tempBuffer.begin() + outputSize);
            if (verbose)
            {
                std::cout << "Decompressed successfully. Original size: " << input.size()
                          << ", Decompressed size: " << tempBuffer.size() << " bytes.\n";
            }
            return true;
        }
        else if (result == Z_BUF_ERROR)
        {
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

    // Resize output to the actual decompressed size
    output.assign(tempBuffer.begin(), tempBuffer.begin() + outputSize);

    if (verbose)
    {
        std::cout << "Decompressed chunk successfully. Original size: " << input.size()
                  << ", Decompressed size: " << outputSize << " bytes.\n";
    }

    return true;
}

// Function to receive and save the file with enhanced ACK handling
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
    std::string fileSizeStr = metadata.substr(delimiterPos + 1);
    size_t fileSize = 0;

    try
    {
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
    ssize_t paquet_index = 0;
    // Boucle pour recevoir les données
    while (totalBytesWritten < fileSize)
    {
        // Premièrement, recevoir la taille du buffer
        ssize_t bufferSizeReceived = recvfrom(serverSocket, metadataBuffer, sizeof(metadataBuffer), 0,
                                              (struct sockaddr *)&clientAddr, &clientAddrLen);

        if (bufferSizeReceived == -1)
        {
            logError("Error receiving buffer size. Error: " + std::string(strerror(errno)));
            break;
        }

        // Convertir la taille du buffer reçu en size_t
        ssize_t bufferSize;
        memcpy(&bufferSize, metadataBuffer, sizeof(ssize_t));

        if (bufferSize <= 0)
        {
            logError("Invalid buffer size received: " + std::to_string(bufferSize));
            break;
        }

        const ssize_t MAX_CHUNK_SIZE = 50 * 1024 * 1024; // 50 MB cap
        if (bufferSize > MAX_CHUNK_SIZE || bufferSize <= 0)
        {
            logError("Received invalid or excessively large buffer size: " + std::to_string(bufferSize));
            break;
        }

        // Allocating buffer to receive the chunk of data of this size
        std::vector<char> chunkBuffer(bufferSize);

        // Recevoir les données du client
        ssize_t bytesReceived = recvfrom(serverSocket, chunkBuffer.data(), bufferSize, 0,
                                         (struct sockaddr *)&clientAddr, &clientAddrLen);

        if (bytesReceived <= 0)
        {
            logError("Error receiving data from client. Error: " + std::string(strerror(errno)));
            break;
        }

        if (decompressFlag)
        {
            // Décompression des données reçues
            bool decompressionSuccess = decompressChunk(
                chunkBuffer,
                decompressedChunk,
                verbose);

            if (!decompressionSuccess)
            {
                if (verbose)
                {
                    std::cerr << "Decompression error. Waiting for the client to resend the chunk.\n";
                }

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
            }
            else
            {
                // Send ACK message with the paquet index
                paquet_index++;
                std::string ackMessage = "ACK " + std::to_string(paquet_index);
                ssize_t ackSent = sendto(serverSocket, ackMessage.c_str(), ackMessage.length(), 0,
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
            outFile.write(chunkBuffer.data(), bytesReceived);
            totalBytesWritten += bytesReceived;

            // Send ACK message with the paquet index
            paquet_index++;
            std::string ackMessage = "ACK " + std::to_string(paquet_index);
            ssize_t ackSent = sendto(serverSocket, ackMessage.c_str(), ackMessage.length(), 0,
                                     (struct sockaddr *)&clientAddr, sizeof(clientAddr));
            if (ackSent == -1)
            {
                logError("Error sending ACK message to client.");
                break;
            }
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
    serverAddr.sin_addr.s_addr = inet_addr("0.0.0.0"); // Adresse localhost

    if (bind(serverSocket, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) == -1)
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
