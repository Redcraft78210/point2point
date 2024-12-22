# Définir les variables
CXX = g++
CXXFLAGS = -Wall -std=c++11
LDFLAGS = -lz  # Ajouter la bibliothèque zlib pour la compression (si nécessaire)
SRC_SERVER = server.cpp
SRC_CLIENT = client.cpp
OBJ_SERVER = server.o
OBJ_CLIENT = client.o
EXEC_SERVER = bin/server
EXEC_CLIENT = bin/client

# Cible par défaut
all: $(EXEC_SERVER) $(EXEC_CLIENT)

# Compiler le serveur
$(EXEC_SERVER): $(OBJ_SERVER)
	$(CXX) $(OBJ_SERVER) -o $(EXEC_SERVER) $(LDFLAGS)

# Compiler le client
$(EXEC_CLIENT): $(OBJ_CLIENT)
	$(CXX) $(OBJ_CLIENT) -o $(EXEC_CLIENT) $(LDFLAGS)

# Compiler le fichier source du serveur
$(OBJ_SERVER): $(SRC_SERVER)
	$(CXX) $(CXXFLAGS) -c $(SRC_SERVER)

# Compiler le fichier source du client
$(OBJ_CLIENT): $(SRC_CLIENT)
	$(CXX) $(CXXFLAGS) -c $(SRC_CLIENT)

# Nettoyer les fichiers objets et exécutables
clean:
	rm -f $(OBJ_SERVER) $(OBJ_CLIENT) $(EXEC_SERVER) $(EXEC_CLIENT)

# Cible de test pour vérifier les dépendances
test:
	@echo "Testing the Makefile"

# Créer le répertoire bin si nécessaire
bin/:
	mkdir -p bin

# Assurer que le répertoire bin/ existe avant de créer les exécutables
$(EXEC_SERVER): | bin/
$(EXEC_CLIENT): | bin/
