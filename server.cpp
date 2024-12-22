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

#define DEFAULT_PORT 12345
#define CHUNK_SIZE 4096

void showUsage() {
    std::cout << "Usage: file_receiver [options]\n";
    std::cout << "  -h, --help            Display help\n";
    std::cout << "  -p, --port <port>      Port to listen on (default: 12345)\n";
    std::cout << "  -f, --file <file>      Destination file\n";
    std::cout << "  -c, --decompress       Enable decompression\n";
    std::cout << "  -v, --verbose          Show detailed information\n";
}

// Error logging function
void logError(const std::string &message) {
    std::cerr << "Error: " << message << std::endl;
}

// Decompress a chunk of data
int decompress_chunk(const std::vector<char> &input_chunk, std::vector<char> &output_chunk) {
    uLongf output_size = input_chunk.size() * 2; // Initial guess for decompressed size
    std::vector<char> temp_buffer(output_size);

    int result;
    do {
        result = uncompress(reinterpret_cast<Bytef *>(temp_buffer.data()), &output_size,
                            reinterpret_cast<const Bytef *>(input_chunk.data()), input_chunk.size());

        if (result == Z_BUF_ERROR) {
            output_size *= 2;
            temp_buffer.resize(output_size);
        } else if (result != Z_OK) {
            logError("Decompression error: " + std::to_string(result));
            return result; // Return error code if it's not a buffer size error
        }
    } while (result == Z_BUF_ERROR);

    output_chunk.insert(output_chunk.end(), temp_buffer.begin(), temp_buffer.begin() + output_size);
    return Z_OK;
}

// Function to receive and save the file
void saveReceivedFile(int clientSocket, const char *outputFileName, bool decompressFlag, bool verbose) {
    std::ofstream outFile(outputFileName, std::ios::binary);
    if (!outFile.is_open()) {
        logError("Error opening output file!");
        return;
    }

    char buffer[CHUNK_SIZE];
    size_t bytesReceived = 0;
    std::vector<char> decompressedChunk;

    if (verbose) {
        std::cout << "Receiving file...\n";
    }

    ssize_t received;
    while ((received = recv(clientSocket, buffer, CHUNK_SIZE, 0)) > 0) {
        bytesReceived += received;

        if (decompressFlag) {
            std::vector<char> inputChunk(buffer, buffer + received);
            decompressedChunk.clear();
            int result = decompress_chunk(inputChunk, decompressedChunk);

            if (result != Z_OK) {
                logError("Failed to decompress chunk!");
                return;
            }

            outFile.write(decompressedChunk.data(), decompressedChunk.size());
        } else {
            outFile.write(buffer, received);
        }
    }

    if (received < 0) {
        logError("Error receiving data!");
    }

    if (verbose) {
        std::cout << "File received successfully! Total bytes: " << bytesReceived << std::endl;
    }

    outFile.close();
}

// Function to create and bind the server socket
int createServerSocket(int port, bool verbose) {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        logError("Error creating socket!");
        return -1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        logError("Error binding socket!");
        close(serverSocket);
        return -1;
    }

    if (listen(serverSocket, 1) == -1) {
        logError("Error listening on socket!");
        close(serverSocket);
        return -1;
    }

    if (verbose) {
        std::cout << "Server listening on port " << port << "...\n";
    }

    return serverSocket;
}

// Function to accept the client connection
int acceptClientConnection(int serverSocket, bool verbose) {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &clientLen);
    if (clientSocket == -1) {
        logError("Error accepting connection!");
        close(serverSocket);
        return -1;
    }

    if (verbose) {
        std::cout << "Connection accepted from client.\n";
    }

    return clientSocket;
}

int main(int argc, char *argv[]) {
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
    while ((opt = getopt_long(argc, argv, "h:p:f:cv", longOpts, nullptr)) != -1) {
        switch (opt) {
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
    if (serverSocket == -1) return 1;

    // Accept a client connection
    int clientSocket = acceptClientConnection(serverSocket, verbose);
    if (clientSocket == -1) return 1;

    // Receive the file from the client and save it
    saveReceivedFile(clientSocket, outputFileName.c_str(), decompressFlag, verbose);

    // Close the sockets
    close(clientSocket);
    close(serverSocket);

    return 0;
}
