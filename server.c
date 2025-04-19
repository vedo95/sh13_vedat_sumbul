/******************************************************************************
 * Projet  : Sherlock 13 – Jeu de déduction en C/SDL2
 * Fichier : server.c
 *
 * Description :
 *
 *   – server.c : logique serveur, distribution des cartes, gestion des tours
 *
 * Auteur    : Vedat SUMBUL
 * Promotion : EI4
 * Étudiant  : 21115513
 * Email     : vedat.sumbul@etu.sorbonne-universite.fr
 *
 * Date      : 2025‑04‑19
 ******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>

#define MAX_CLIENTS 4
#define MAX_CARDS 13
#define CARDS_PER_PLAYER 3
#define NB_ITEMS 8
#define MAX_BUFFER 512
#define CULPRIT_CARD_INDEX (MAX_CARDS - 1)

// Indicateur pour la réponse 'K' quand un joueur possède l'objet
#define OBJECT_FOUND_INDICATOR 100
// États pour BlackList
#define PLAYER_ACTIVE 0
#define PLAYER_ELIMINATED 1
#define PLAYER_WON -1

typedef struct {
    char ipAddress[40];
    int port; // Port d'écoute *du client* (utile si on garde l'ancien système, moins maintenant)
    char name[40];
    int sockfd;       // Socket de communication persistant avec ce client
    int isActive;     // Indicateur si le slot client est utilisé/connecté
} ClientInfo;

// --- Variables Globales ---
ClientInfo tcpClients[MAX_CLIENTS];
int nbClients = 0;
int fsmServer = 0; // 0: Attente connexions, 1: Jeu en cours, 2: Fin de partie
int deck[MAX_CARDS];
int tableCartes[MAX_CLIENTS][NB_ITEMS]; // [joueur][type_objet] = nb_cartes_avec_cet_objet
int joueurCourant = 0;
int playerStatus[MAX_CLIENTS]; // Remplace BlackList: 0=actif, 1=perdu/eliminé, -1=gagnant
int listen_sockfd; // Socket d'écoute du serveur
fd_set active_fds, read_fds; // Ensembles de descripteurs pour select()
int game_over = 0;
int winnerId = -1;

char *nomcartes[]= {
    "Sebastian Moran", "Irene Adler", "Inspector Lestrade",
    "Inspector Gregson", "Inspector Baynes", "Inspector Bradstreet",
    "Inspector Hopkins", "Sherlock Holmes", "John Watson", "Mycroft Holmes",
    "Mrs. Hudson", "Mary Morstan", "James Moriarty"
};

// Indices des objets (correspondent aux colonnes de tableCartes et aux images objet[0]..objet[7])
// 0: Pipe, 1: Ampoule, 2: Poing, 3: Couronne, 4: Carnet, 5: Collier, 6: Oeil, 7: Crane

// --- Fonctions Utilitaires ---

void error(const char *msg) {
    perror(msg);
    // On ne quitte pas forcément pour une erreur client
    // exit(1);
}

void initializeServerState() {
    nbClients = 0;
    fsmServer = 0; // Attente connexions
    joueurCourant = 0;
    game_over = 0;
    winnerId = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&tcpClients[i], 0, sizeof(ClientInfo));
        tcpClients[i].sockfd = -1;
        tcpClients[i].isActive = 0;
        playerStatus[i] = PLAYER_ACTIVE;
    }
    // Initialiser les ensembles de descripteurs pour select()
    FD_ZERO(&active_fds);
    FD_ZERO(&read_fds);
}

void melangerDeck() {
    srand(time(NULL)); // Initialiser le générateur aléatoire
    for (int i = 0; i < 1000; i++) {
        int index1 = rand() % MAX_CARDS;
        int index2 = rand() % MAX_CARDS;
        int tmp = deck[index1];
        deck[index1] = deck[index2];
        deck[index2] = tmp;
    }
    printf("Deck mélangé.\n");
}

void createTable() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        for (int j = 0; j < NB_ITEMS; j++) {
            tableCartes[i][j] = 0;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) { // Pour chaque joueur
        for (int j = 0; j < CARDS_PER_PLAYER; j++) { // Pour chaque carte du joueur
            int c = deck[i * CARDS_PER_PLAYER + j];
            // Incrémenter les compteurs d'objets pour ce joueur basé sur la carte 'c'
            switch (c) {
                case 0:  tableCartes[i][7]++; tableCartes[i][2]++; break; // Moran (Crane, Poing)
                case 1:  tableCartes[i][7]++; tableCartes[i][1]++; tableCartes[i][5]++; break; // Adler (Crane, Ampoule, Collier) - MAJ : Crane ajouté basé sur l'image
                case 2:  tableCartes[i][3]++; tableCartes[i][6]++; tableCartes[i][4]++; break; // Lestrade (Couronne, Oeil, Carnet) - MAJ : Oeil ajouté basé sur l'image
                case 3:  tableCartes[i][3]++; tableCartes[i][2]++; tableCartes[i][4]++; break; // Gregson (Couronne, Poing, Carnet)
                case 4:  tableCartes[i][3]++; tableCartes[i][1]++; tableCartes[i][4]++; break; // Baynes (Couronne, Ampoule, Carnet) - MAJ : Carnet ajouté basé sur l'image
                case 5:  tableCartes[i][3]++; tableCartes[i][2]++; tableCartes[i][4]++; break; // Bradstreet (Couronne, Poing, Carnet) - MAJ : Carnet ajouté basé sur l'image
                case 6:  tableCartes[i][3]++; tableCartes[i][0]++; tableCartes[i][6]++; break; // Hopkins (Couronne, Pipe, Oeil)
                case 7:  tableCartes[i][0]++; tableCartes[i][1]++; tableCartes[i][2]++; break; // Holmes (Pipe, Ampoule, Poing)
                case 8:  tableCartes[i][0]++; tableCartes[i][6]++; tableCartes[i][2]++; break; // Watson (Pipe, Oeil, Poing)
                case 9:  tableCartes[i][0]++; tableCartes[i][1]++; tableCartes[i][4]++; break; // Mycroft (Pipe, Ampoule, Carnet)
                case 10: tableCartes[i][0]++; tableCartes[i][5]++; break; // Hudson (Pipe, Collier) - MAJ : Pipe ajouté basé sur l'image
                case 11: tableCartes[i][4]++; tableCartes[i][5]++; break; // Morstan (Carnet, Collier)
                case 12: tableCartes[i][7]++; tableCartes[i][1]++; break; // Moriarty (Crane, Ampoule) - MAJ : Ampoule au lieu de revolver basé sur l'image
            }
        }
    }
    printf("Table des objets créée.\n");
}

void printDeck() {
    printf("--- Deck ---\n");
    for (int i = 0; i < MAX_CARDS; i++)
        printf("Carte %d: %d (%s)\n", i, deck[i], nomcartes[deck[i]]);
    printf("Coupable: %d (%s)\n", deck[CULPRIT_CARD_INDEX], nomcartes[deck[CULPRIT_CARD_INDEX]]);
    printf("--- Table Cartes ---\n");
    printf("        Pi Am Po Co Ca Cl Oe Cr\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        printf("Joueur %d:", i);
        for (int j = 0; j < NB_ITEMS; j++)
            printf("%2d ", tableCartes[i][j]);
        printf("\n");
    }
    printf("------------\n");
}

void printClients() {
    printf("--- Clients Connectés (%d) ---\n", nbClients);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (tcpClients[i].isActive) {
             printf("ID %d: %s (%s:%d) Socket: %d Status: %d\n", i,
                    tcpClients[i].name, tcpClients[i].ipAddress,
                    tcpClients[i].port, tcpClients[i].sockfd, playerStatus[i]);
        } else {
             printf("ID %d: -\n", i);
        }
    }
     printf("------------\n");
}

// Trouve le premier slot libre ou retourne -1
int findFreeClientSlot() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!tcpClients[i].isActive) return i;
    }
    return -1;
}

// Trouve un client par son nom (devrait être unique)
int findClientByName(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (tcpClients[i].isActive && strcmp(tcpClients[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Envoie un message à un client spécifique via son socket persistant
void sendMessageToClient(int clientId, const char *mess) {
    if (clientId < 0 || clientId >= MAX_CLIENTS || !tcpClients[clientId].isActive || tcpClients[clientId].sockfd == -1) {
        fprintf(stderr, "Tentative d'envoi à un client invalide ou déconnecté (ID: %d)\n", clientId);
        return;
    }
    int n = write(tcpClients[clientId].sockfd, mess, strlen(mess));
    if (n < 0) {
        perror("ERROR writing to socket");
        // Le client s'est probablement déconnecté
        fprintf(stderr, "Client %d (%s) semble déconnecté. Fermeture socket %d.\n",
                clientId, tcpClients[clientId].name, tcpClients[clientId].sockfd);
        close(tcpClients[clientId].sockfd);
        FD_CLR(tcpClients[clientId].sockfd, &active_fds);
        tcpClients[clientId].isActive = 0;
        tcpClients[clientId].sockfd = -1;
        // Gérer l'impact sur le jeu (ex: le joueur perd automatiquement ?)
        playerStatus[clientId] = PLAYER_ELIMINATED;
        nbClients--;
        printClients();
        // Si le joueur déconnecté était le joueur courant, passer au suivant
        if(joueurCourant == clientId && fsmServer == 1) {
             // Ne pas envoyer de message ici, la fonction appelante s'en chargera
        }
        // Vérifier si la partie est terminée à cause de cette déconnexion
        // ... (logique similaire à celle dans handleGuess) ...

    } else if (n < strlen(mess)) {
         fprintf(stderr, "WARN: Message partiellement envoyé au client %d\n", clientId);
    }
    // DEBUG: printf("Sent to client %d (%s): %s\n", clientId, tcpClients[clientId].name, mess);
}

// Envoie un message à tous les clients actifs
void broadcastMessage(const char *mess) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (tcpClients[i].isActive) {
            sendMessageToClient(i, mess);
        }
    }
}

// Passe au joueur actif suivant, retourne 1 si trouvé, 0 sinon (fin de partie ?)
int advanceTurn() {
    if (game_over) return 0; // Si déjà fini, ne rien faire

    int checked_players = 0;
    int active_players = 0;
    int last_active_player = -1;

     // Compter les joueurs actifs et trouver le dernier actif
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (tcpClients[i].isActive && playerStatus[i] == PLAYER_ACTIVE) {
            active_players++;
            last_active_player = i;
        }
    }

    // Si moins de 2 joueurs actifs, la partie est terminée
    if (active_players <= 1) {
        game_over = 1;
        fsmServer = 2; // Fin de partie
        winnerId = last_active_player; // Le dernier joueur actif gagne (ou -1 si aucun)
        printf("Fin de partie! Moins de 2 joueurs actifs. Gagnant potentiel: %d\n", winnerId);
        // Envoyer les messages de fin
        char reply[MAX_BUFFER];
         for (int i = 0; i < MAX_CLIENTS; i++) {
            if (tcpClients[i].isActive) {
                 if (i == winnerId) {
                    sprintf(reply, "Z\n"); // Gagné
                 } else {
                    sprintf(reply, "P\n"); // Perdu
                 }
                 sendMessageToClient(i, reply);
            }
        }
        // Envoyer un dernier 'M' pour indiquer la fin (par exemple avec un ID spécial)
        sprintf(reply, "M -1\n"); // -1 indique la fin
        broadcastMessage(reply);
        return 0; // Aucun joueur suivant à trouver
    }


    // Chercher le prochain joueur actif en boucle
    do {
        joueurCourant = (joueurCourant + 1) % MAX_CLIENTS;
        checked_players++;
        // Si on a fait un tour complet sans trouver de joueur actif (ne devrait pas arriver si active_players > 1)
        if (checked_players > MAX_CLIENTS) {
             fprintf(stderr, "Erreur: Boucle infinie dans advanceTurn ?\n");
             game_over = 1;
             fsmServer = 2;
             return 0;
        }
    } while (!tcpClients[joueurCourant].isActive || playerStatus[joueurCourant] != PLAYER_ACTIVE);

    printf("Joueur suivant: %d (%s)\n", joueurCourant, tcpClients[joueurCourant].name);
    return 1; // Joueur suivant trouvé
}


// Gère la logique lorsqu'un joueur fait une accusation ('G')
void handleGuess(int playerId, int guessCard) {
    char reply[MAX_BUFFER];

    printf("Joueur %d accuse la carte %d (%s). La vraie carte est %d (%s).\n",
        playerId, guessCard, nomcartes[guessCard], deck[CULPRIT_CARD_INDEX], nomcartes[deck[CULPRIT_CARD_INDEX]]);

    if (guessCard == deck[CULPRIT_CARD_INDEX]) {
        // Bonne accusation ! Le joueur gagne.
        game_over = 1;
        fsmServer = 2; // Fin de partie
        winnerId = playerId;
        playerStatus[playerId] = PLAYER_WON;
        printf("Joueur %d a gagné !\n", playerId);

        // Informer tout le monde
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (tcpClients[i].isActive) {
                 if (i == playerId) {
                    sprintf(reply, "Z\n"); // Gagné
                 } else {
                    sprintf(reply, "P\n"); // Perdu
                 }
                 sendMessageToClient(i, reply);
            }
        }
        // Envoyer un dernier 'M' pour indiquer la fin
        sprintf(reply, "M -1\n"); // -1 indique la fin
        broadcastMessage(reply);

    } else {
        // Mauvaise accusation ! Le joueur est éliminé.
        playerStatus[playerId] = PLAYER_ELIMINATED;
        printf("Joueur %d est éliminé.\n", playerId);
        sprintf(reply, "P\n"); // Perdu
        sendMessageToClient(playerId, reply);

        // Vérifier si la partie continue (et passer le tour)
        if (advanceTurn()) {
            sprintf(reply, "M %d\n", joueurCourant);
            broadcastMessage(reply);
        }
        // advanceTurn() gère déjà le cas où il ne reste qu'un joueur
    }
     printClients(); // Afficher le nouvel état
}

// Gère une demande d'objet ('O')
void handleObjectQuery(int askerId, int objectId) {
    char reply[MAX_BUFFER];
    printf("Joueur %d demande l'objet %d à tout le monde.\n", askerId, objectId);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (tcpClients[i].isActive) {
            // On informe tout le monde si le joueur 'i' possède l'objet 'objectId'
            int count = tableCartes[i][objectId];
            // On utilise OBJECT_FOUND_INDICATOR si > 0, sinon 0
            sprintf(reply, "K %d %d %d\n", i, objectId, (count > 0) ? OBJECT_FOUND_INDICATOR : 0);
            broadcastMessage(reply);
        }
    }

    // Passer au joueur suivant
    if (advanceTurn()) {
        sprintf(reply, "M %d\n", joueurCourant);
        broadcastMessage(reply);
    }
}

// Gère une suspicion ('S')
void handleSuspicion(int askerId, int targetId, int objectId) {
     char reply[MAX_BUFFER];
     printf("Joueur %d suspecte joueur %d à propos de l'objet %d.\n", askerId, targetId, objectId);

    if (targetId < 0 || targetId >= MAX_CLIENTS || !tcpClients[targetId].isActive) {
        fprintf(stderr, "Suspicion vers un joueur invalide (%d)\n", targetId);
        // On pourrait juste ignorer ou informer le joueur demandeur de l'erreur
    } else {
        int count = tableCartes[targetId][objectId];
        sprintf(reply, "V %d %d %d\n", targetId, objectId, count);
        // Envoyer la réponse à tout le monde (comme dans le code original)
        broadcastMessage(reply);
    }

     // Passer au joueur suivant
    if (advanceTurn()) {
        sprintf(reply, "M %d\n", joueurCourant);
        broadcastMessage(reply);
    }
}

// Traite un message reçu d'un client
void processClientMessage(int clientId, char *buffer) {
    char command;
    char reply[MAX_BUFFER];

    printf("Received from client %d (%s): %s", clientId, tcpClients[clientId].name, buffer);

    if (strlen(buffer) == 0) return; // Message vide

    command = buffer[0];

    if (fsmServer == 0) { // Phase de connexion
        if (command == 'C') {
            char clientIpAddress[40];
            int clientPort; // Port d'écoute du client (moins utile maintenant)
            char clientName[40];
            int n = sscanf(buffer, "%c %39s %d %39s", &command, clientIpAddress, &clientPort, clientName);

            if (n != 4) {
                 fprintf(stderr, "Client %d: Message 'C' mal formé: %s", clientId, buffer);
                 // Fermer la connexion ? Envoyer erreur ? Pour l'instant on ignore.
                 return;
            }

            printf("Client %d demande connexion: IP=%s Port=%d Nom=%s\n", clientId, clientIpAddress, clientPort, clientName);

            // Vérifier si le nom est déjà pris (important si un client se reconnecte)
            if (findClientByName(clientName) != -1) {
                 fprintf(stderr, "Nom '%s' déjà utilisé. Refus connexion.\n", clientName);
                 sprintf(reply, "E Name already taken\n"); // 'E' pour Erreur
                 sendMessageToClient(clientId, reply);
                 // Fermer proprement la connexion temporaire venant d'accept()
                 close(tcpClients[clientId].sockfd);
                 FD_CLR(tcpClients[clientId].sockfd, &active_fds);
                 tcpClients[clientId].isActive = 0;
                 tcpClients[clientId].sockfd = -1;
                 nbClients--; // On avait incrémenté trop tôt
                 return;
            }


            // Stocker les infos (l'ID est déjà correct car assigné dans la boucle principale)
            strcpy(tcpClients[clientId].ipAddress, clientIpAddress); // Utiliser l'adresse réelle ? inet_ntoa ?
            tcpClients[clientId].port = clientPort;
            strcpy(tcpClients[clientId].name, clientName);
            playerStatus[clientId] = PLAYER_ACTIVE; // Marquer comme actif

            printClients();

            // Lui envoyer son ID
            sprintf(reply, "I %d\n", clientId);
            sendMessageToClient(clientId, reply);

            // Envoyer la liste actuelle des joueurs à tout le monde
            char nameList[MAX_BUFFER] = "L";
            for (int i = 0; i < MAX_CLIENTS; i++) {
                strcat(nameList, " ");
                strcat(nameList, tcpClients[i].isActive ? tcpClients[i].name : "-");
            }
            strcat(nameList, "\n");
            broadcastMessage(nameList);

            // Si 4 joueurs connectés, lancer le jeu
            if (nbClients == MAX_CLIENTS) {
                 printf("4 joueurs connectés. Lancement du jeu !\n");
                 melangerDeck();
                 createTable();
                 printDeck();

                 // Distribuer les cartes et les infos de la table
                 for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (tcpClients[i].isActive) {
                        // Cartes (D)
                        sprintf(reply, "D %d %d %d\n",
                                deck[i * CARDS_PER_PLAYER + 0],
                                deck[i * CARDS_PER_PLAYER + 1],
                                deck[i * CARDS_PER_PLAYER + 2]);
                        sendMessageToClient(i, reply);

                        // Ligne de la table (V) pour ce joueur
                        for (int j = 0; j < NB_ITEMS; j++) {
                             sprintf(reply, "V %d %d %d\n", i, j, tableCartes[i][j]);
                             sendMessageToClient(i, reply);
                        }
                    }
                 }

                 // Définir le premier joueur et informer tout le monde
                 joueurCourant = 0; // Commencer par le joueur 0
                  // S'assurer que le joueur 0 est bien actif (normalement oui)
                 while (!tcpClients[joueurCourant].isActive || playerStatus[joueurCourant] != PLAYER_ACTIVE) {
                     joueurCourant = (joueurCourant + 1) % MAX_CLIENTS;
                 }
                 printf("Premier joueur: %d (%s)\n", joueurCourant, tcpClients[joueurCourant].name);
                 sprintf(reply, "M %d\n", joueurCourant);
                 broadcastMessage(reply);

                 fsmServer = 1; // Passer à l'état Jeu en cours
            }

        } else {
             fprintf(stderr, "Client %d: Commande '%c' inattendue pendant la connexion.\n", clientId, command);
        }

    } else if (fsmServer == 1) { // Phase de jeu
         // Vérifier si c'est bien le tour du joueur qui envoie la commande
         if (clientId != joueurCourant) {
              fprintf(stderr, "WARN: Client %d (%s) a envoyé une commande hors de son tour (%c).\n",
                      clientId, tcpClients[clientId].name, command);
              // Ignorer la commande ? Envoyer un message d'erreur ?
              sprintf(reply, "E Not your turn\n");
              sendMessageToClient(clientId, reply);
              return;
         }

         // Vérifier si le joueur est éliminé (ne devrait pas arriver si la logique est bonne)
         if (playerStatus[clientId] != PLAYER_ACTIVE) {
              fprintf(stderr, "WARN: Client %d (%s) éliminé a envoyé une commande (%c).\n",
                      clientId, tcpClients[clientId].name, command);
             sprintf(reply, "E You are eliminated\n");
             sendMessageToClient(clientId, reply);
             // Passer le tour au cas où ?
             if (advanceTurn()) {
                 sprintf(reply, "M %d\n", joueurCourant);
                 broadcastMessage(reply);
             }
             return;
         }

        switch (command) {
            case 'G': { // Guess: G <playerId> <cardId>
                int pId, cardId;
                if (sscanf(buffer, "%c %d %d", &command, &pId, &cardId) == 3) {
                    if (pId != clientId) { fprintf(stderr,"ID joueur incohérent dans G\n"); return; }
                    handleGuess(clientId, cardId);
                } else { fprintf(stderr, "Client %d: Message 'G' mal formé: %s", clientId, buffer); }
                break;
            }
            case 'O': { // Object Query: O <playerId> <objectId>
                 int pId, objId;
                 if (sscanf(buffer, "%c %d %d", &command, &pId, &objId) == 3) {
                    if (pId != clientId) { fprintf(stderr,"ID joueur incohérent dans O\n"); return; }
                    handleObjectQuery(clientId, objId);
                 } else { fprintf(stderr, "Client %d: Message 'O' mal formé: %s", clientId, buffer); }
                 break;
            }
            case 'S': { // Suspicion: S <playerId> <targetPlayerId> <objectId>
                 int pId, targetId, objId;
                  if (sscanf(buffer, "%c %d %d %d", &command, &pId, &targetId, &objId) == 4) {
                     if (pId != clientId) { fprintf(stderr,"ID joueur incohérent dans S\n"); return; }
                     handleSuspicion(clientId, targetId, objId);
                 } else { fprintf(stderr, "Client %d: Message 'S' mal formé: %s", clientId, buffer); }
                 break;
            }
            default:
                fprintf(stderr, "Client %d: Commande '%c' inconnue ou inattendue en jeu.\n", clientId, command);
                break;
        }
    } else { // fsmServer == 2 (Fin de partie)
         fprintf(stderr, "Client %d: Message '%c' reçu après la fin de partie.\n", clientId, command);
         // On pourrait ignorer ou renvoyer l'état de fin de partie
    }
}

// --- Main ---
int main(int argc, char *argv[]) {
    int portno;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen;
    char buffer[MAX_BUFFER];

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);
    if (portno <= 0 || portno > 65535) {
         fprintf(stderr, "Port invalide: %s\n", argv[1]);
         exit(1);
    }

    // Création du socket d'écoute
    listen_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sockfd < 0) error("ERROR opening listening socket");

    // Permettre la réutilisation de l'adresse (utile pour redémarrer rapidement)
    int optval = 1;
    setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // Configuration de l'adresse du serveur
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    // Liaison du socket à l'adresse
    if (bind(listen_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        error("ERROR on binding");
        close(listen_sockfd);
        exit(1);
    }

    // Mettre le socket en mode écoute
    if (listen(listen_sockfd, 5) < 0) {
         error("ERROR on listen");
         close(listen_sockfd);
         exit(1);
    }
    printf("Serveur Sherlock 13 démarré sur le port %d. En attente de %d joueurs...\n", portno, MAX_CLIENTS);

    // Initialiser l'état du serveur
    initializeServerState();

    // Ajouter le socket d'écoute à l'ensemble des sockets actifs
    FD_SET(listen_sockfd, &active_fds);
    int max_fd = listen_sockfd;

    // Initialiser le deck (une seule fois au démarrage)
    for (int i = 0; i < MAX_CARDS; i++) deck[i] = i;


    // Boucle principale du serveur
    while (1) {
        read_fds = active_fds; // Copier l'ensemble actif pour select()

        // Attendre une activité sur l'un des sockets
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            // Ignorer EINTR (interruption par un signal)
            if (errno == EINTR) {
                continue;
            }
            error("ERROR in select");
            break; // Sortir en cas d'erreur grave de select
        }

        // 1. Vérifier si une nouvelle connexion arrive sur le socket d'écoute
        if (FD_ISSET(listen_sockfd, &read_fds)) {
            clilen = sizeof(cli_addr);
            int newsockfd = accept(listen_sockfd, (struct sockaddr *)&cli_addr, &clilen);
            if (newsockfd < 0) {
                error("ERROR on accept");
            } else {
                 printf("Nouvelle connexion de %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

                 if (nbClients >= MAX_CLIENTS || fsmServer != 0) {
                     fprintf(stderr, "Trop de clients ou jeu déjà démarré. Connexion refusée.\n");
                     char *msg = "Server full or game in progress\n";
                     write(newsockfd, msg, strlen(msg));
                     close(newsockfd);
                 } else {
                     // Trouver un slot libre
                     int newClientId = findFreeClientSlot();
                     if (newClientId == -1) { // Ne devrait pas arriver si nbClients < MAX_CLIENTS
                          fprintf(stderr, "Erreur: nbClients < MAX_CLIENTS mais pas de slot libre trouvé!\n");
                          close(newsockfd);
                     } else {
                        // Ajouter le nouveau socket à l'ensemble actif
                        FD_SET(newsockfd, &active_fds);
                        if (newsockfd > max_fd) max_fd = newsockfd; // Mettre à jour max_fd

                        // Initialiser les infos client (le message 'C' complétera)
                        tcpClients[newClientId].sockfd = newsockfd;
                        tcpClients[newClientId].isActive = 1;
                        strcpy(tcpClients[newClientId].ipAddress, inet_ntoa(cli_addr.sin_addr));
                        tcpClients[newClientId].port = ntohs(cli_addr.sin_port); // Port source de la connexion TCP
                        strcpy(tcpClients[newClientId].name, "?"); // Nom temporaire
                        nbClients++;
                        printf("Client connecté, assigné à l'ID %d. Socket %d ajouté. nbClients = %d\n", newClientId, newsockfd, nbClients);
                        printClients();
                        // Attendre le message 'C' du client
                     }
                 }
            }
        }

        // 2. Vérifier les sockets des clients existants pour des données entrantes
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (tcpClients[i].isActive && tcpClients[i].sockfd != -1 && FD_ISSET(tcpClients[i].sockfd, &read_fds)) {

                bzero(buffer, MAX_BUFFER);
                int n = read(tcpClients[i].sockfd, buffer, MAX_BUFFER - 1);

                if (n <= 0) {
                    // Erreur ou connexion fermée par le client
                    if (n == 0) {
                        printf("Client %d (%s) a fermé la connexion (socket %d).\n", i, tcpClients[i].name, tcpClients[i].sockfd);
                    } else {
                        perror("ERROR reading from socket");
                    }
                    close(tcpClients[i].sockfd);
                    FD_CLR(tcpClients[i].sockfd, &active_fds); // Retirer de l'ensemble actif
                    tcpClients[i].isActive = 0;
                    tcpClients[i].sockfd = -1;
                    nbClients--;

                    // Gérer l'impact sur le jeu
                    if (fsmServer == 1) { // Si le jeu était en cours
                         playerStatus[i] = PLAYER_ELIMINATED; // Considérer comme éliminé
                         printf("Joueur %d (%s) marqué comme éliminé suite à déconnexion.\n", i, tcpClients[i].name);

                         // Informer les autres joueurs ? Optionnel.
                         // char disco_msg[64];
                         // sprintf(disco_msg, "X %d\n", i); // 'X' pour déconnexion
                         // broadcastMessage(disco_msg);

                         // Si c'était son tour, passer au suivant
                         if (i == joueurCourant && !game_over) {
                              printf("C'était le tour du joueur déconnecté. Passage au suivant.\n");
                              if (advanceTurn()) {
                                 char reply[MAX_BUFFER];
                                 sprintf(reply, "M %d\n", joueurCourant);
                                 broadcastMessage(reply);
                             }
                         } else {
                             // Vérifier si la partie est terminée maintenant
                             int active_count = 0;
                             int last_active = -1;
                              for(int p=0; p<MAX_CLIENTS; ++p) {
                                 if(tcpClients[p].isActive && playerStatus[p] == PLAYER_ACTIVE) {
                                     active_count++;
                                     last_active = p;
                                 }
                             }
                             if (active_count <= 1 && fsmServer == 1) {
                                 // Déclencher la fin de partie gérée par advanceTurn lors du prochain tour
                                 printf("Déconnexion a potentiellement terminé la partie (active_count=%d).\n", active_count);
                                 // Forcer une vérification si nécessaire ? Ou attendre le prochain tour.
                             }
                         }
                    } else if (fsmServer == 0) {
                         // Si un joueur déco avant le début, informer les autres que la liste a changé
                         char nameList[MAX_BUFFER] = "L";
                         for (int k = 0; k < MAX_CLIENTS; k++) {
                             strcat(nameList, " ");
                             strcat(nameList, tcpClients[k].isActive ? tcpClients[k].name : "-");
                         }
                         strcat(nameList, "\n");
                         broadcastMessage(nameList);
                    }
                     printClients(); // Afficher le nouvel état

                } else {
                    // Données reçues, traiter le message
                    buffer[n] = '\0'; // Assurer la terminaison de la chaîne
                     // Gérer les messages multiples dans un seul read ? Pour l'instant, on suppose un message par read.
                    processClientMessage(i, buffer);
                }
            } // end if FD_ISSET client socket
        } // end for loop clients
    } // end while(1) server loop

    // Fermeture du socket d'écoute (si on sort de la boucle)
    printf("Arrêt du serveur.\n");
    close(listen_sockfd);
    // Fermer les sockets clients restants
    for (int i=0; i<MAX_CLIENTS; ++i) {
        if (tcpClients[i].sockfd != -1) {
            close(tcpClients[i].sockfd);
        }
    }

    return 0;
}