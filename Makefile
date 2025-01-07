# Définir les variables
CXX = g++
DEBUGFLAG = -g
CXXFLAGS = -Wall -std=c++17 -I./include
LDFLAGS = -lzstd  # Ajouter la bibliothèque zlib pour la compression (si nécessaire)


# Source files
SRC_SERVER = server/server.cpp
SRC_CLIENT = client/client.cpp
SRC_COMMON = commons/MurmurHash3.cpp  # Fichier commun unique

# Object files
OBJ_SERVER = server/server.o $(SRC_COMMON:.cpp=.o)
OBJ_CLIENT = client/client.o $(SRC_COMMON:.cpp=.o)
OBJ_COMMON = $(SRC_COMMON:.cpp=.o)

# Executables
EXEC_SERVER = bin/server
EXEC_CLIENT = bin/client

# Cible par défaut
all: $(EXEC_SERVER) $(EXEC_CLIENT)

# Debug build
debug: CXXFLAGS += -DDEBUG -g
debug: all

# Créer le répertoire bin si nécessaire
bin/:
	mkdir -p bin

# Assurer que le répertoire bin/ existe avant de créer les exécutables
$(EXEC_SERVER): $(OBJ_SERVER) | bin/
	$(CXX) $(OBJ_SERVER) -o $(EXEC_SERVER) $(LDFLAGS)

$(EXEC_CLIENT): $(OBJ_CLIENT) | bin/
	$(CXX) $(OBJ_CLIENT) -o $(EXEC_CLIENT) $(LDFLAGS)

# Compiler les fichiers sources en objets pour le serveur
server/%.o: server/%.cpp | bin/
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compiler les fichiers sources en objets pour le client
client/%.o: client/%.cpp | bin/
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compiler MurmurHash3.cpp une seule fois pour les deux exécutables
$(OBJ_COMMON): $(SRC_COMMON) | bin/
	$(CXX) $(CXXFLAGS) -c $(SRC_COMMON) -o $(OBJ_COMMON)

# Nettoyer les fichiers objets et exécutables
clean:
	rm -f $(OBJ_SERVER) $(OBJ_CLIENT) $(OBJ_COMMON) $(EXEC_SERVER) $(EXEC_CLIENT)
	rm -rf bin/

# Cible de test pour vérifier les dépendances
test:
	@echo "Testing the Makefile"

