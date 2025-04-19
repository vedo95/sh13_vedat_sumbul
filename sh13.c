/******************************************************************************
 * Projet  : Sherlock 13 – Jeu de déduction en C/SDL2
 * Fichier : sh13.c
 *
 * Description :
 *
 *   – sh13.c   : client SDL2, interface graphique, interactions joueur
 *
 * Auteur    : Vedat SUMBUL
 * Promotion : EI4
 * Étudiant  : 21115513
 * Email     : vedat.sumbul@etu.sorbonne-universite.fr
 *
 * Date      : 2025‑04‑19
 ******************************************************************************/


#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdarg.h>
#include <errno.h>

#define MAX_BUFFER 512
#define MAX_CLIENTS 4
#define NB_ITEMS 8
#define MAX_CARDS 13
#define CARDS_PER_PLAYER 3

// --- Variables Globales -
// Connexion
char gServerIpAddress[256];
int gServerPort;
char gClientIpAddress[256];
int gClientPort;
int server_sockfd = -1;     // Socket unique connecté au serveur
fd_set client_fds;          // Ensemble pour select() côté client
struct timeval select_timeout; // Timeout pour select() (non bloquant)

// Infos Joueur
char gName[256];
char gNames[MAX_CLIENTS][256];
int gId = -1; // ID assigné par le serveur
int myCards[CARDS_PER_PLAYER]; // Cartes reçues (indices)

// État du jeu
int joueurCourant = -1;
int tableCartes[MAX_CLIENTS][NB_ITEMS]; // [joueur][objet] = valeur (-1: inconnu, 0-N: suspicion, 100: possède via 'K')
int guiltGuess[MAX_CARDS]; // 0 = non barré, 1 = barré
int playerLost[MAX_CLIENTS] = {0}; // 0 = actif, 1 = perdu/éliminé (Nouveau)

// État UI / Sélection
int joueurSel = -1;   // Joueur sélectionné pour suspicion
int objetSel = -1;    // Objet sélectionné pour suspicion ou demande générale
int guiltSel = -1;    // Suspect sélectionné pour accusation

// Drapeaux d'état UI
int connectEnabled = 1;
int goEnabled = 0;
int gameStarted = 0;
int gameOver = 0;
int iWon = 0;


char statusMessage[256] = "Entrez vos informations et connectez-vous"; // Message pour l'utilisateur

// Ressources SDL
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
TTF_Font* Sans = NULL;
TTF_Font* StatusFont = NULL;
SDL_Texture *texture_deck[MAX_CARDS];
SDL_Texture *texture_objet[NB_ITEMS];
SDL_Texture *texture_gobutton, *texture_connectbutton;
SDL_Texture *texture_perdu, *texture_gagner; // Images de fin
// --- Textures Texte (à gérer dynamiquement ou pré-calculer) ---
SDL_Texture* textureNames[MAX_CLIENTS] = {NULL};
SDL_Texture* textureSuspectNames[MAX_CARDS] = {NULL};
SDL_Texture* textureTableValues[MAX_CLIENTS][NB_ITEMS] = {NULL};
SDL_Texture* textureMyCards[CARDS_PER_PLAYER] = {NULL};
SDL_Texture* textureStatus = NULL;
SDL_Texture* textureObjectCounts[NB_ITEMS] = {NULL};
SDL_Texture* textureMyName = NULL; // Nouvelle texture pour le nom en bas à droite


// Noms des objets et compte initial (basé sur les images fournies)
char *nomObjets[] = {"Pipe", "Ampoule", "Poing", "Couronne", "Carnet", "Collier", "Oeil", "Crane"};
char *nbObjetsStr[] = {"5", "5", "5", "5", "4", "3", "3", "3"}; // Total dans le jeu

// Noms des personnages (doit correspondre aux images SH13_0..12.png)
char *nomPersonnages[] = {
    "Sebastian Moran", "Irene Adler", "Inspector Lestrade",
    "Inspector Gregson", "Inspector Baynes", "Inspector Bradstreet",
    "Inspector Hopkins", "Sherlock Holmes", "John Watson", "Mycroft Holmes",
    "Mrs. Hudson", "Mary Morstan", "James Moriarty"
};

// --- Fonctions ---

void updateStatusMessage(const char *message) {
    strncpy(statusMessage, message, sizeof(statusMessage) - 1);
    statusMessage[sizeof(statusMessage) - 1] = '\0';
     // Libérer l'ancienne texture si elle existe
    if (textureStatus != NULL) {
        SDL_DestroyTexture(textureStatus);
        textureStatus = NULL;
    }
}

// Fonction pour créer une texture à partir de texte (utilise une police donnée)
SDL_Texture* createTextTextureWithFont(TTF_Font* font, const char* text, SDL_Color color) {
    if (font == NULL || renderer == NULL || text == NULL || strlen(text) == 0) {
        //fprintf(stderr, "WARN: Cannot create text texture (font=%p, renderer=%p, text='%s')\n", font, renderer, text?text:"NULL");
        return NULL;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Solid(font, text, color);
    if (!surface) {
        fprintf(stderr, "TTF_RenderUTF8_Solid Error: %s\n", TTF_GetError());
        return NULL;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!texture) {
        fprintf(stderr, "CreateTextureFromSurface Error: %s\n", SDL_GetError());
    }
    return texture;
}

// Initialise l'état du client
void initializeClientState() {
    gId = -1;
    joueurCourant = -1;
    joueurSel = -1;
    objetSel = -1;
    guiltSel = -1;

    connectEnabled = 1;
    goEnabled = 0;
    gameStarted = 0;
    gameOver = 0;
    iWon = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        strcpy(gNames[i], "-");
        playerLost[i] = 0; // Marquer tout le monde comme actif au début
        for (int j = 0; j < NB_ITEMS; j++) {
            tableCartes[i][j] = -1; // Inconnu
        }
    }
    for (int i = 0; i < MAX_CARDS; i++) {
        guiltGuess[i] = 0; // Non barré
    }
     for (int i = 0; i < CARDS_PER_PLAYER; i++) {
        myCards[i] = -1; // Pas de carte
    }
    updateStatusMessage("Entrez vos infos et connectez-vous");

    // Détruire l'ancienne texture du nom si elle existe
    if (textureMyName) {
        SDL_DestroyTexture(textureMyName);
        textureMyName = NULL;
    }
}

// Nettoie les ressources SDL et la connexion
void cleanup() {
    printf("Nettoyage...\n");
    // Fermer la connexion
    if (server_sockfd != -1) {
        close(server_sockfd);
        server_sockfd = -1;
    }
    // Libérer les textures (important !)
    for (int i = 0; i < MAX_CARDS; i++) if(texture_deck[i]) SDL_DestroyTexture(texture_deck[i]);
    for (int i = 0; i < NB_ITEMS; i++) if(texture_objet[i]) SDL_DestroyTexture(texture_objet[i]);
    if(texture_gobutton) SDL_DestroyTexture(texture_gobutton);
    if(texture_connectbutton) SDL_DestroyTexture(texture_connectbutton);
    if(texture_perdu) SDL_DestroyTexture(texture_perdu);
    if(texture_gagner) SDL_DestroyTexture(texture_gagner);
     if (textureStatus) SDL_DestroyTexture(textureStatus);
     if (textureMyName) SDL_DestroyTexture(textureMyName); // Nettoyer le nom
     for(int i=0; i<MAX_CLIENTS; ++i) if(textureNames[i]) SDL_DestroyTexture(textureNames[i]);
     for(int i=0; i<MAX_CARDS; ++i) if(textureSuspectNames[i]) SDL_DestroyTexture(textureSuspectNames[i]);
     for(int i=0; i<NB_ITEMS; ++i) if(textureObjectCounts[i]) SDL_DestroyTexture(textureObjectCounts[i]);
     for(int i=0; i<CARDS_PER_PLAYER; ++i) if(textureMyCards[i]) SDL_DestroyTexture(textureMyCards[i]);
     for(int i=0; i<MAX_CLIENTS; ++i)
         for(int j=0; j<NB_ITEMS; ++j)
             if(textureTableValues[i][j]) SDL_DestroyTexture(textureTableValues[i][j]);


    // Fermer les polices
    if (Sans) TTF_CloseFont(Sans);
    if (StatusFont) TTF_CloseFont(StatusFont); // Fermer la nouvelle police
    // Détruire le renderer et la fenêtre
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    // Quitter les sous-systèmes SDL
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

// Envoie un message formaté au serveur
void sendMessageToServer(const char *format, ...) {
    if (server_sockfd == -1) {
        fprintf(stderr, "Non connecté au serveur.\n");
        updateStatusMessage("Erreur: Non connecté au serveur.");
        return;
    }

    char sendbuffer[MAX_BUFFER];
    va_list args;
    va_start(args, format);
    vsnprintf(sendbuffer, sizeof(sendbuffer), format, args);
    va_end(args);

    // S'assurer que le message se termine par \n pour le serveur
    size_t current_len = strlen(sendbuffer);
    if (current_len > 0 && current_len < sizeof(sendbuffer) - 1 && sendbuffer[current_len-1] != '\n') {
        sendbuffer[current_len] = '\n';
        sendbuffer[current_len + 1] = '\0';
    } else if (current_len == 0) {
        return; // Ne pas envoyer de message vide
    }


    // printf("DEBUG: Sending to server: %s", sendbuffer); // DEBUG
    int n = write(server_sockfd, sendbuffer, strlen(sendbuffer));
    if (n < 0) {
        perror("ERROR writing to server socket");
        updateStatusMessage("Erreur: Déconnecté du serveur !");
        close(server_sockfd);
        server_sockfd = -1;
        initializeClientState();
    } else if (n < strlen(sendbuffer)) {
         fprintf(stderr, "WARN: Message partiellement envoyé au serveur.\n");
    }
}

// Traite un message reçu du serveur
void processServerMessage(char *buffer) {
     printf("DEBUG: Received from server: %s\n", buffer); // DEBUG
    char command = buffer[0];
    char reply[MAX_BUFFER]; // Pour les messages status
    SDL_Color black = {0,0,0,255};

    switch (command) {
        case 'I': // ID Assignment: I <yourId>
            if (sscanf(buffer, "%*c %d", &gId) == 1) {
                 printf("ID reçu du serveur: %d\n", gId);
                 sprintf(reply, "Connecté avec ID %d. Attente joueurs...", gId);
                 updateStatusMessage(reply);
                 connectEnabled = 0;
            } else { fprintf(stderr, "Message 'I' mal formé: %s", buffer); }
            break;

        case 'L': // Player List: L <name0> <name1> <name2> <name3>
             if (sscanf(buffer, "%*c %s %s %s %s", gNames[0], gNames[1], gNames[2], gNames[3]) == 4) {
                 printf("Liste des joueurs mise à jour: %s %s %s %s\n", gNames[0], gNames[1], gNames[2], gNames[3]);
                  // Mettre à jour les textures des noms
                  for(int i=0; i<MAX_CLIENTS; ++i) {
                     if(textureNames[i]) SDL_DestroyTexture(textureNames[i]);
                     textureNames[i] = createTextTextureWithFont(Sans, gNames[i], black);
                  }
                 // Créer/Mettre à jour la texture pour notre nom en bas à droite
                 if (gId != -1 && gId < MAX_CLIENTS) {
                     if (textureMyName) SDL_DestroyTexture(textureMyName);
                     textureMyName = createTextTextureWithFont(StatusFont, gNames[gId], black);
                 }
             } else { fprintf(stderr, "Message 'L' mal formé: %s", buffer); }
             break;

        case 'D': // Deck (Your Cards): D <card1> <card2> <card3>
             if (sscanf(buffer, "%*c %d %d %d", &myCards[0], &myCards[1], &myCards[2]) == 3) {
                 if (myCards[0] < 0 || myCards[0] >= MAX_CARDS ||
                     myCards[1] < 0 || myCards[1] >= MAX_CARDS ||
                     myCards[2] < 0 || myCards[2] >= MAX_CARDS) {
                      fprintf(stderr, "Indices de cartes invalides dans 'D': %s", buffer);
                 } else {
                    printf("Cartes reçues: %d(%s), %d(%s), %d(%s)\n",
                           myCards[0], nomPersonnages[myCards[0]],
                           myCards[1], nomPersonnages[myCards[1]],
                           myCards[2], nomPersonnages[myCards[2]]);
                    gameStarted = 1;
                }
             } else { fprintf(stderr, "Message 'D' mal formé: %s", buffer); }
             break;

        case 'V': // Table Value (Suspicion result or initial value): V <playerId> <objectId> <value>
            {
                int pId, objId, value;
                if (sscanf(buffer, "%*c %d %d %d", &pId, &objId, &value) == 3) {
                    if (pId >= 0 && pId < MAX_CLIENTS && objId >= 0 && objId < NB_ITEMS) {
                        printf("Info Table: Joueur %d, Objet %d, Valeur %d\n", pId, objId, value);
                        tableCartes[pId][objId] = value;
                         // Mettre à jour la texture
                         if(textureTableValues[pId][objId]) SDL_DestroyTexture(textureTableValues[pId][objId]);
                         char valStr[5];
                         sprintf(valStr, "%d", value);
                         textureTableValues[pId][objId] = createTextTextureWithFont(Sans, valStr, black);

                    } else { fprintf(stderr, "Indices invalides dans message 'V': %s", buffer); }
                } else { fprintf(stderr, "Message 'V' mal formé: %s", buffer); }
            }
            break;

         case 'K': // Knowledge (Object Query result): K <playerId> <objectId> <value> (value is 0 or OBJECT_FOUND_INDICATOR)
            {
                int pId, objId, value;
                 SDL_Color color; // Déclarer la variable couleur ici
                if (sscanf(buffer, "%*c %d %d %d", &pId, &objId, &value) == 3) {
                     if (pId >= 0 && pId < MAX_CLIENTS && objId >= 0 && objId < NB_ITEMS) {
                        printf("Résultat Demande Objet: Joueur %d, Objet %d, Status %d\n", pId, objId, value);
                        tableCartes[pId][objId] = value;
                         // Mettre à jour la texture
                         if(textureTableValues[pId][objId]) SDL_DestroyTexture(textureTableValues[pId][objId]);
                         char valStr[5];
                         if (value != 0) { sprintf(valStr, "*"); } else { sprintf(valStr, "0"); }

                         // Utilisation correcte des compound literals pour l'assignation
                         if (value != 0) { color = (SDL_Color){0, 150, 0, 255}; } // Vert
                         else { color = (SDL_Color){150, 0, 0, 255}; } // Rouge

                         textureTableValues[pId][objId] = createTextTextureWithFont(Sans, valStr, color);

                    } else { fprintf(stderr, "Indices invalides dans message 'K': %s", buffer); }
                } else { fprintf(stderr, "Message 'K' mal formé: %s", buffer); }
            }
            break;

        case 'M': // Move (Current Player Turn): M <playerId>
            {
                 int currentPId;
                 if (sscanf(buffer, "%*c %d", &currentPId) == 1) {
                     joueurCourant = currentPId;
                      if (joueurCourant == -1) { // Fin de partie signalée par le serveur
                        gameOver = 1;
                        goEnabled = 0;
                        if (!iWon && gId != -1 && !playerLost[gId]) { // Si on n'a pas déjà reçu Z ou P
                             updateStatusMessage("Partie terminée.");
                        }
                     } else if (joueurCourant >= 0 && joueurCourant < MAX_CLIENTS) {
                         // Vérifier si JE peux jouer (mon tour ET je n'ai pas perdu)
                         if (joueurCourant == gId && gId != -1 && !playerLost[gId]) {
                             goEnabled = 1;
                             sprintf(reply, "C'est votre tour (Joueur %d)", gId);
                             updateStatusMessage(reply);
                         } else {
                             goEnabled = 0; // Pas mon tour ou j'ai perdu
                             // Afficher le nom du joueur courant si l'ID est valide
                             sprintf(reply, "Tour du joueur %d (%s)", joueurCourant, gNames[joueurCourant]);
                             updateStatusMessage(reply);
                         }
                     } else {
                         fprintf(stderr, "ID joueur courant invalide reçu: %d\n", joueurCourant);
                         updateStatusMessage("Erreur: Joueur courant invalide.");
                         goEnabled = 0;
                     }
                      printf("Joueur courant mis à jour: %d\n", joueurCourant);
                 } else { fprintf(stderr, "Message 'M' mal formé: %s", buffer); }
            }
            break;

        case 'Z': // Win: Z
            printf("Message Z reçu: J'ai gagné !\n");
            gameOver = 1;
            iWon = 1;
            if (gId != -1) playerLost[gId] = 0; // Assurer qu'on n'est pas marqué perdu
            goEnabled = 0;
            updateStatusMessage("VOUS AVEZ GAGNÉ !");
            break;

        case 'P': // Lose: P
            printf("Message P reçu: J'ai perdu !\n");
            //gameOver = 1; // Pas forcément terminé pour tout le monde
            iWon = 0;
            if (gId != -1) playerLost[gId] = 1; // Marquer comme perdu
            goEnabled = 0; // On ne peut plus jouer
            updateStatusMessage("VOUS AVEZ PERDU ! (Éliminé)");
            break;

        case 'E': // Error message from server: E <message>
             printf("Erreur du serveur: %s", buffer + 2);
             updateStatusMessage(buffer + 2);
             if (strstr(buffer, "Name already taken")) {
                 if(server_sockfd != -1) close(server_sockfd);
                 server_sockfd = -1;
                 initializeClientState();
             } else if (strstr(buffer, "You are eliminated")) {
                  if (gId != -1) playerLost[gId] = 1;
                  goEnabled = 0;
             }
             break;

        default:
            fprintf(stderr, "Commande serveur inconnue reçue: %c\n", command);
            break;
    }
}

// Charge les ressources SDL (images, police)
int loadResources() {
     // Charger les cartes personnages
    char filename[50];
    for (int i = 0; i < MAX_CARDS; i++) {
        sprintf(filename, "images/SH13_%d.png", i);
        SDL_Surface* surf = IMG_Load(filename);
        if (!surf) { fprintf(stderr, "Erreur chargement %s: %s\n", filename, IMG_GetError()); return 0; }
        texture_deck[i] = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_FreeSurface(surf);
        if (!texture_deck[i]) { fprintf(stderr, "Erreur création texture %s: %s\n", filename, SDL_GetError()); return 0; }
    }

    // Charger les objets
    char* objFiles[] = {"pipe", "ampoule", "poing", "couronne", "carnet", "collier", "oeil", "crane"};
    for (int i = 0; i < NB_ITEMS; i++) {
         sprintf(filename, "images/SH13_%s_120x120.png", objFiles[i]);
         SDL_Surface* surf = IMG_Load(filename);
         if (!surf) { fprintf(stderr, "Erreur chargement %s: %s\n", filename, IMG_GetError()); return 0; }
         texture_objet[i] = SDL_CreateTextureFromSurface(renderer, surf);
         SDL_FreeSurface(surf);
         if (!texture_objet[i]) { fprintf(stderr, "Erreur création texture %s: %s\n", filename, SDL_GetError()); return 0; }
    }

    // Charger les boutons et images fin
    SDL_Surface* surf_go = IMG_Load("images/gobutton.png");
    SDL_Surface* surf_connect = IMG_Load("images/connectbutton.png");
    SDL_Surface* surf_perdu = IMG_Load("images/perdu.png");
    SDL_Surface* surf_gagner = IMG_Load("images/gagner.png");
    if (!surf_go || !surf_connect || !surf_perdu || !surf_gagner) { fprintf(stderr, "Erreur chargement boutons/fin: %s\n", IMG_GetError()); return 0; }

    texture_gobutton = SDL_CreateTextureFromSurface(renderer, surf_go);
    texture_connectbutton = SDL_CreateTextureFromSurface(renderer, surf_connect);
    texture_perdu = SDL_CreateTextureFromSurface(renderer, surf_perdu);
    texture_gagner = SDL_CreateTextureFromSurface(renderer, surf_gagner);
    SDL_FreeSurface(surf_go); SDL_FreeSurface(surf_connect); SDL_FreeSurface(surf_perdu); SDL_FreeSurface(surf_gagner);
    if (!texture_gobutton || !texture_connectbutton || !texture_perdu || !texture_gagner) { fprintf(stderr, "Erreur création texture boutons/fin: %s\n", SDL_GetError()); return 0; }

    // Charger les polices
    Sans = TTF_OpenFont("font/sans.ttf", 15);
    if (!Sans) { fprintf(stderr, "Erreur chargement police font/sans.ttf (taille 15): %s\n", TTF_GetError()); return 0; }
    StatusFont = TTF_OpenFont("font/sans.ttf", 18); // Charger la police pour le statut (taille 18)
    if (!StatusFont) { fprintf(stderr, "Erreur chargement police font/sans.ttf (taille 18): %s\n", TTF_GetError()); return 0; }


    SDL_Color black = {0,0,0,255};
    for(int i=0; i<MAX_CARDS; ++i) {
        if(textureSuspectNames[i]) SDL_DestroyTexture(textureSuspectNames[i]);
        textureSuspectNames[i] = createTextTextureWithFont(Sans, nomPersonnages[i], black);
    }
    for(int i=0; i<NB_ITEMS; ++i) {
        if(textureObjectCounts[i]) SDL_DestroyTexture(textureObjectCounts[i]);
        textureObjectCounts[i] = createTextTextureWithFont(Sans, nbObjetsStr[i], black);
    }


    printf("Ressources chargées.\n");
    return 1;
}

// Dessine l'interface
void renderUI() {
    // Fond
	SDL_SetRenderDrawColor(renderer, 255, 230, 230, 255); // Rose pâle
	SDL_Rect bgRect = {0, 0, 1024, 768};
	SDL_RenderFillRect(renderer, &bgRect);

    int gridStartX = 200, gridStartY = 90;
    int cellWidth = 60, cellHeight = 60;
    int listStartX = 100, listStartY = 350;
    int listCellHeight = 30;
    int listNameWidth = 150;
    int listIconWidth = 30;
    SDL_Color black = {0, 0, 0, 255};
    SDL_Color white = {255, 255, 255, 255}; // Pour texte sur fond coloré
    SDL_Color grey = {100, 100, 100, 255};
    SDL_Color highlightColorPlayer = {255, 180, 180, 255}; // Rose
    SDL_Color highlightColorObject = {180, 255, 180, 255}; // Vert
    SDL_Color highlightColorSuspect = {180, 180, 255, 255}; // Bleu
    SDL_Color myTurnColor = {0, 180, 0, 255};       // Vert plus foncé
    SDL_Color notMyTurnColor = {180, 0, 0, 255};    // Rouge plus foncé
    SDL_Color eliminatedColor = {100, 100, 100, 255}; // Gris foncé

    // --- Affichage Grille Joueurs/Objets ---
    // Lignes
    SDL_SetRenderDrawColor(renderer, grey.r, grey.g, grey.b, grey.a);
    for (int i = 0; i <= MAX_CLIENTS; i++) {
         SDL_RenderDrawLine(renderer, 0, gridStartY + i * cellHeight, gridStartX + NB_ITEMS * cellWidth, gridStartY + i * cellHeight);
    }
     SDL_RenderDrawLine(renderer, 0, gridStartY, 0, gridStartY + MAX_CLIENTS * cellHeight);
     SDL_RenderDrawLine(renderer, gridStartX, 0, gridStartX, gridStartY + MAX_CLIENTS * cellHeight);
     for (int j = 0; j <= NB_ITEMS; j++) {
         SDL_RenderDrawLine(renderer, gridStartX + j * cellWidth, 0, gridStartX + j * cellWidth, gridStartY + MAX_CLIENTS * cellHeight);
     }
     SDL_RenderDrawLine(renderer, gridStartX, 0, gridStartX + NB_ITEMS * cellWidth, 0);
     SDL_RenderDrawLine(renderer, gridStartX, gridStartY, gridStartX + NB_ITEMS * cellWidth, gridStartY);


    // Noms des joueurs et indicateur de tour/état
    for (int i = 0; i < MAX_CLIENTS; i++) {
        SDL_Rect playerRect = {0, gridStartY + i * cellHeight, gridStartX, cellHeight};
        SDL_Color nameColor = black; // Couleur du texte par défaut

        // Dessiner le fond de la case en premier
        if (gameStarted && !gameOver) {
            if (playerLost[i]) { // Si le joueur a perdu
                 SDL_SetRenderDrawColor(renderer, eliminatedColor.r, eliminatedColor.g, eliminatedColor.b, eliminatedColor.a);
                 nameColor = white; // Texte blanc sur fond gris
            } else if (i == joueurCourant) { // Si c'est son tour (et pas perdu)
                 SDL_SetRenderDrawColor(renderer, myTurnColor.r, myTurnColor.g, myTurnColor.b, myTurnColor.a);
                 nameColor = white; // Texte blanc sur fond vert
            } else { // Si ce n'est pas son tour (et pas perdu)
                 SDL_SetRenderDrawColor(renderer, notMyTurnColor.r, notMyTurnColor.g, notMyTurnColor.b, notMyTurnColor.a);
                 nameColor = white; // Texte blanc sur fond rouge
            }
             SDL_RenderFillRect(renderer, &playerRect); // Remplir toute la case
        }

        // Highlight par dessus si sélectionné pour suspicion
        if (!gameOver && !playerLost[i] && joueurSel == i) {
            SDL_SetRenderDrawColor(renderer, highlightColorPlayer.r, highlightColorPlayer.g, highlightColorPlayer.b, 150); // Semi-transparent?
            SDL_RenderFillRect(renderer, &playerRect);
        }

        // Afficher nom (
        if (strcmp(gNames[i], "-") != 0) { // Ne pas afficher si "-"
             // Détruire l'ancienne texture du nom avant d'en créer une nouvelle si la couleur change
             if (textureNames[i]) SDL_DestroyTexture(textureNames[i]);
             textureNames[i] = createTextTextureWithFont(Sans, gNames[i], nameColor);

             if (textureNames[i]) {
                SDL_Rect nameRect;
                SDL_QueryTexture(textureNames[i], NULL, NULL, &nameRect.w, &nameRect.h);
                nameRect.x = 10;
                nameRect.y = gridStartY + i * cellHeight + (cellHeight - nameRect.h) / 2;
                 // S'assurer que le nom ne dépasse pas
                 if (nameRect.x + nameRect.w > gridStartX - 5) nameRect.w = gridStartX - 5 - nameRect.x;
                 if (nameRect.w < 0) nameRect.w = 0;

                SDL_RenderCopy(renderer, textureNames[i], NULL, &nameRect);
            }
        }
    }

    // Icônes objets et compte total
     for (int j = 0; j < NB_ITEMS; j++) {
         SDL_Rect objIconRect = {gridStartX + j * cellWidth + (cellWidth - 40)/2, 10, 40, 40};
         // Highlight si sélectionné
         if (!gameOver && objetSel == j) {
             SDL_SetRenderDrawColor(renderer, highlightColorObject.r, highlightColorObject.g, highlightColorObject.b, highlightColorObject.a);
             SDL_Rect hlRect = {gridStartX + j * cellWidth, 0, cellWidth, gridStartY};
             SDL_RenderFillRect(renderer, &hlRect);
         }
         if(texture_objet[j]) SDL_RenderCopy(renderer, texture_objet[j], NULL, &objIconRect);

         // Afficher compte total
         if (textureObjectCounts[j]) {
              SDL_Rect countRect;
              SDL_QueryTexture(textureObjectCounts[j], NULL, NULL, &countRect.w, &countRect.h);
              countRect.x = gridStartX + j * cellWidth + (cellWidth - countRect.w) / 2;
              countRect.y = 60;
              SDL_RenderCopy(renderer, textureObjectCounts[j], NULL, &countRect);
         }
     }

    // Valeurs dans la grille (tableCartes)
     for (int i = 0; i < MAX_CLIENTS; i++) {
         for (int j = 0; j < NB_ITEMS; j++) {
             if (tableCartes[i][j] != -1) {
                  SDL_Texture* valTexture = textureTableValues[i][j];
                  if (valTexture) {
                      SDL_Rect valRect;
                      SDL_QueryTexture(valTexture, NULL, NULL, &valRect.w, &valRect.h);
                      valRect.x = gridStartX + j * cellWidth + (cellWidth - valRect.w) / 2;
                      valRect.y = gridStartY + i * cellHeight + (cellHeight - valRect.h) / 2;
                      SDL_RenderCopy(renderer, valTexture, NULL, &valRect);
                  }
             }
         }
     }


    // --- Liste des Suspects ---
    SDL_SetRenderDrawColor(renderer, grey.r, grey.g, grey.b, grey.a);
    for (int i = 0; i < MAX_CARDS; i++) {
         SDL_Rect suspectRect = {listStartX, listStartY + i * listCellHeight, listNameWidth, listCellHeight};
         SDL_Rect guessBoxRect = {listStartX + listNameWidth, listStartY + i * listCellHeight, listCellHeight, listCellHeight};

         // Highlight si sélectionné pour accusation
         if (!gameOver && guiltSel == i) {
              SDL_SetRenderDrawColor(renderer, highlightColorSuspect.r, highlightColorSuspect.g, highlightColorSuspect.b, highlightColorSuspect.a);
              SDL_RenderFillRect(renderer, &suspectRect);
         }

         // Afficher icônes objets associés
         int icons[3] = {-1, -1, -1};
         int icon_count = 0;
          switch (i) { // Copié de la version précédente
                case 0:  icons[0]=7; icons[1]=2; icon_count=2; break; // Moran (Crane, Poing)
                case 1:  icons[0]=7; icons[1]=1; icons[2]=5; icon_count=3; break; // Adler (Crane, Ampoule, Collier)
                case 2:  icons[0]=3; icons[1]=6; icons[2]=4; icon_count=3; break; // Lestrade (Couronne, Oeil, Carnet)
                case 3:  icons[0]=3; icons[1]=2; icons[2]=4; icon_count=3; break; // Gregson (Couronne, Poing, Carnet)
                case 4:  icons[0]=3; icons[1]=1; icons[2]=4; icon_count=3; break; // Baynes (Couronne, Ampoule, Carnet)
                case 5:  icons[0]=3; icons[1]=2; icons[2]=4; icon_count=3; break; // Bradstreet (Couronne, Poing, Carnet)
                case 6:  icons[0]=3; icons[1]=0; icons[2]=6; icon_count=3; break; // Hopkins (Couronne, Pipe, Oeil)
                case 7:  icons[0]=0; icons[1]=1; icons[2]=2; icon_count=3; break; // Holmes (Pipe, Ampoule, Poing)
                case 8:  icons[0]=0; icons[1]=6; icons[2]=2; icon_count=3; break; // Watson (Pipe, Oeil, Poing)
                case 9:  icons[0]=0; icons[1]=1; icons[2]=4; icon_count=3; break; // Mycroft (Pipe, Ampoule, Carnet)
                case 10: icons[0]=0; icons[1]=5; icon_count=2; break; // Hudson (Pipe, Collier)
                case 11: icons[0]=4; icons[1]=5; icon_count=2; break; // Morstan (Carnet, Collier)
                case 12: icons[0]=7; icons[1]=1; icon_count=2; break; // Moriarty (Crane, Ampoule)
            }
         for (int k = 0; k < icon_count; k++) {
             SDL_Rect iconRect = {listStartX - (k+1)*listIconWidth, listStartY + i * listCellHeight + (listCellHeight-listIconWidth)/2, listIconWidth, listIconWidth};
             if (icons[k] != -1 && icons[k] < NB_ITEMS && texture_objet[icons[k]]) {
                 SDL_RenderCopy(renderer, texture_objet[icons[k]], NULL, &iconRect);
             }
         }


         // Afficher nom suspect
         if (textureSuspectNames[i]) {
            SDL_Rect sNameRect;
            SDL_QueryTexture(textureSuspectNames[i], NULL, NULL, &sNameRect.w, &sNameRect.h);
            sNameRect.x = listStartX + 5;
            sNameRect.y = listStartY + i * listCellHeight + (listCellHeight - sNameRect.h) / 2;
            if (sNameRect.w > listNameWidth - 10) sNameRect.w = listNameWidth - 10;
            SDL_RenderCopy(renderer, textureSuspectNames[i], NULL, &sNameRect);
         }

         // Dessiner la boîte pour barrer
         SDL_SetRenderDrawColor(renderer, grey.r, grey.g, grey.b, 255);
         SDL_RenderDrawRect(renderer, &guessBoxRect);
         // Dessiner la croix si barré
         if (guiltGuess[i]) {
             SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // Rouge
             SDL_RenderDrawLine(renderer, guessBoxRect.x, guessBoxRect.y, guessBoxRect.x + guessBoxRect.w, guessBoxRect.y + guessBoxRect.h);
             SDL_RenderDrawLine(renderer, guessBoxRect.x + guessBoxRect.w, guessBoxRect.y, guessBoxRect.x, guessBoxRect.y + guessBoxRect.h);
         }
    }
     // Lignes liste suspects
     SDL_RenderDrawLine(renderer, listStartX - 3*listIconWidth, listStartY, listStartX - 3*listIconWidth, listStartY + MAX_CARDS * listCellHeight);
     SDL_RenderDrawLine(renderer, listStartX, listStartY, listStartX, listStartY + MAX_CARDS * listCellHeight);
     SDL_RenderDrawLine(renderer, listStartX + listNameWidth, listStartY, listStartX + listNameWidth, listStartY + MAX_CARDS * listCellHeight);
     SDL_RenderDrawLine(renderer, listStartX + listNameWidth + listCellHeight, listStartY, listStartX + listNameWidth + listCellHeight, listStartY + MAX_CARDS * listCellHeight);
     for(int i=0; i<=MAX_CARDS; ++i) {
         SDL_RenderDrawLine(renderer, listStartX - 3*listIconWidth, listStartY + i*listCellHeight, listStartX + listNameWidth + listCellHeight, listStartY + i*listCellHeight);
     }


    // --- Affichage Cartes du Joueur ---
    int cardAreaX = 700, cardAreaY = 0;
    int cardWidth = 250, cardHeight = 165;
     for (int i = 0; i < CARDS_PER_PLAYER; i++) {
         if (myCards[i] != -1 && myCards[i] < MAX_CARDS && texture_deck[myCards[i]]) {
             SDL_Rect cardRect = {cardAreaX, cardAreaY + i * (cardHeight + 20), cardWidth, cardHeight};
             SDL_RenderCopy(renderer, texture_deck[myCards[i]], NULL, &cardRect);
         }
     }

    // --- Boutons et Status ---
    // Bouton Connect
	if (server_sockfd == -1) { // Afficher seulement si non connecté
        SDL_Rect connectRect = { 0, 0, 200, 50 };
        SDL_RenderCopy(renderer, texture_connectbutton, NULL, &connectRect);
	}
    // Bouton Go
    if (goEnabled && !gameOver) {
        SDL_Rect goRect = { 500, 450, 150, 100 };
        SDL_RenderCopy(renderer, texture_gobutton, NULL, &goRect);
    }
    // Message de Statut (utilise StatusFont)
    if (strlen(statusMessage) > 0) {
        if (!textureStatus) { // Créer la texture si elle n'existe pas ou a changé
             SDL_Color statusCol = black;
             if (strstr(statusMessage, "Erreur") || strstr(statusMessage, "perdu") || strstr(statusMessage,"Déconnecté")) {
                 statusCol = (SDL_Color){200, 0, 0, 255};
             } else if (strstr(statusMessage, "gagné")) {
                 statusCol = (SDL_Color){0, 200, 0, 255};
             } else if (strstr(statusMessage, "votre tour")) {
                 statusCol = (SDL_Color){0, 150, 0, 255};
             } else if (strstr(statusMessage, "Attente")) {
                 statusCol = (SDL_Color){0, 0, 150, 255};
             }

             textureStatus = createTextTextureWithFont(StatusFont, statusMessage, statusCol);
        }
        // --- Affichage Message de Statut (au milieu en bas) ---
        if (textureStatus) {
             SDL_Rect statusRect;
             SDL_QueryTexture(textureStatus, NULL, NULL, &statusRect.w, &statusRect.h);
             // Positionnement au milieu en bas avec une petite marge
             int margin = 10; // Marge par rapport au bas
             statusRect.x = (1024 - statusRect.w) / 2; // Centré horizontalement
             statusRect.y = 768 - statusRect.h - margin; // Positionné près du bas
             SDL_RenderCopy(renderer, textureStatus, NULL, &statusRect);
        }
    }

    // --- Affichage Nom du Joueur (en bas à droite) ---
    if (textureMyName) {
         SDL_Rect myNameRect;
         SDL_QueryTexture(textureMyName, NULL, NULL, &myNameRect.w, &myNameRect.h);
         // Positionnement en bas à droite avec une marge
         int margin = 20;
         myNameRect.x = 1024 - myNameRect.w - margin; // 1024 est la largeur de la fenêtre
         myNameRect.y = 768 - myNameRect.h - margin;  // 768 est la hauteur de la fenêtre
         SDL_RenderCopy(renderer, textureMyName, NULL, &myNameRect);
    }


    // Images de fin de partie (superposées)
    if (gameOver) {
         SDL_Rect endRect = {300, 200, 400, 300};
         if (iWon) {
             if(texture_gagner) SDL_RenderCopy(renderer, texture_gagner, NULL, &endRect);
         } else {
             if(texture_perdu) SDL_RenderCopy(renderer, texture_perdu, NULL, &endRect);
         }
    }


    // Afficher le rendu
    SDL_RenderPresent(renderer);
}


// --- Main ---
int main(int argc, char ** argv) {
    if (argc < 6) {
        fprintf(stderr,"Usage: %s <server_ip> <server_port> <client_ip> <client_port> <player_name>\n", argv[0]);
        fprintf(stderr,"NOTE: client_ip et client_port ne sont plus utilisés pour l'écoute.\n");
        return 1;
    }

    // Récupérer les arguments
    strncpy(gServerIpAddress, argv[1], sizeof(gServerIpAddress) - 1);
    gServerPort = atoi(argv[2]);
    strncpy(gClientIpAddress, argv[3], sizeof(gClientIpAddress) - 1);
    gClientPort = atoi(argv[4]);
    strncpy(gName, argv[5], sizeof(gName) - 1);
    // Nettoyer les noms
    gServerIpAddress[sizeof(gServerIpAddress)-1] = '\0';
    gClientIpAddress[sizeof(gClientIpAddress)-1] = '\0';
    gName[sizeof(gName)-1] = '\0';

    if (gServerPort <= 0 || gServerPort > 65535) {
         fprintf(stderr, "Port serveur invalide: %s\n", argv[2]); return 1;
    }
    if (strlen(gName) == 0) {
        fprintf(stderr, "Nom de joueur invalide.\n"); return 1;
    }


    // Initialisation SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
         fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError()); return 1;
    }
    if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) {
         fprintf(stderr, "IMG_Init Error: %s\n", IMG_GetError()); SDL_Quit(); return 1;
    }
     if (TTF_Init() != 0) {
         fprintf(stderr, "TTF_Init Error: %s\n", TTF_GetError()); IMG_Quit(); SDL_Quit(); return 1;
    }

    // Création fenêtre et renderer
    window = SDL_CreateWindow("Sherlock 13 Client", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_SHOWN);
    if (!window) { fprintf(stderr, "CreateWindow Error: %s\n", SDL_GetError()); cleanup(); return 1; }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { fprintf(stderr, "CreateRenderer Error: %s\n", SDL_GetError()); cleanup(); return 1; }

    // Charger les ressources
    if (!loadResources()) { cleanup(); return 1; }

    // Initialiser l'état du client
    initializeClientState();

    // Initialiser select() timeout
    select_timeout.tv_sec = 0;
    select_timeout.tv_usec = 1000; // 1ms timeout pour ne pas bloquer mais économiser CPU vs 0

    // --- Boucle Principale ---
    int quit = 0;
    SDL_Event event;
    char recvBuffer[MAX_BUFFER];

    while (!quit) {
        // 1. Gérer les événements SDL
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    quit = 1;
                    break;
                case SDL_MOUSEBUTTONDOWN: {
                    int mx = event.button.x;
                    int my = event.button.y;
                    //printf("Click at (%d, %d)\n", mx, my); // DEBUG

                    // --- Logique des clics ---
                    if (gameOver) break; // Ne rien faire si partie terminée

                    // Bouton Connect
                    if (server_sockfd == -1 && mx < 200 && my < 50) {
                        printf("Bouton CONNECT cliqué.\n");
                        struct sockaddr_in serv_addr;
                        struct hostent *server = gethostbyname(gServerIpAddress);
                        if (server == NULL) {
                            fprintf(stderr, "ERROR, no such host: %s\n", gServerIpAddress);
                            updateStatusMessage("Erreur: Hôte serveur introuvable.");
                            break;
                        }
                        server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
                        if (server_sockfd < 0) {
                             perror("ERROR opening socket for server");
                             updateStatusMessage("Erreur: Impossible de créer le socket.");
                             break;
                        }
                        bzero((char *) &serv_addr, sizeof(serv_addr));
                        serv_addr.sin_family = AF_INET;
                        bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
                        serv_addr.sin_port = htons(gServerPort);
                        if (connect(server_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
                             perror("ERROR connecting to server");
                             updateStatusMessage("Erreur: Connexion au serveur échouée.");
                             close(server_sockfd);
                             server_sockfd = -1;
                        } else {
                             printf("Connecté au serveur %s:%d. Socket: %d\n", gServerIpAddress, gServerPort, server_sockfd);
                             updateStatusMessage("Connecté. Envoi informations...");
                             sendMessageToServer("C %s %d %s", gClientIpAddress, gClientPort, gName);
                             connectEnabled = 0;
                        }
                    }
                    // Bouton Go
                    else if (goEnabled && gameStarted && mx >= 500 && mx < 650 && my >= 450 && my < 550) {
                         printf("Bouton GO cliqué.\n");
                         if (guiltSel != -1) {
                             if (guiltSel < 0 || guiltSel >= MAX_CARDS) { fprintf(stderr, "Accusation invalide: guiltSel=%d\n", guiltSel); break; }
                             printf("Action: Accuser %s (%d)\n", nomPersonnages[guiltSel], guiltSel);
                             sendMessageToServer("G %d %d", gId, guiltSel);
                             goEnabled = 0; updateStatusMessage("Accusation envoyée...");
                         } else if (objetSel != -1 && joueurSel == -1) {
                             if (objetSel < 0 || objetSel >= NB_ITEMS) { fprintf(stderr, "Demande objet invalide: objetSel=%d\n", objetSel); break; }
                             printf("Action: Demander objet %s (%d) à tous\n", nomObjets[objetSel], objetSel);
                             sendMessageToServer("O %d %d", gId, objetSel);
                             goEnabled = 0; updateStatusMessage("Demande objet envoyée...");
                         } else if (objetSel != -1 && joueurSel != -1) {
                             if (objetSel < 0 || objetSel >= NB_ITEMS || joueurSel < 0 || joueurSel >= MAX_CLIENTS || joueurSel == gId) {
                                 fprintf(stderr, "Suspicion invalide: objetSel=%d, joueurSel=%d\n", objetSel, joueurSel); break;
                             }
                             printf("Action: Suspecter Joueur %d (%s) pour objet %s (%d)\n", joueurSel, gNames[joueurSel], nomObjets[objetSel], objetSel);
                             sendMessageToServer("S %d %d %d", gId, joueurSel, objetSel);
                             goEnabled = 0; updateStatusMessage("Suspicion envoyée...");
                         } else {
                             updateStatusMessage("Sélectionnez une action valide avant GO.");
                         }
                    }
                    // Clic sur un joueur
                    else if (gameStarted && mx >= 0 && mx < 200 && my >= 90 && my < 90 + MAX_CLIENTS * 60) {
                        int selected = (my - 90) / 60;
                        if (selected != gId && selected < MAX_CLIENTS && strcmp(gNames[selected], "-") != 0 && !playerLost[selected]) { // Ne pas sélectionner joueur perdu
                            joueurSel = selected; guiltSel = -1;
                            printf("Joueur %d (%s) sélectionné pour suspicion.\n", joueurSel, gNames[joueurSel]);
                        } else { joueurSel = -1; } // Désélectionner si clic sur soi, vide ou perdu
                    }
                    // Clic sur un objet
                    else if (gameStarted && mx >= 200 && mx < 200 + NB_ITEMS * 60 && my >= 0 && my < 90) {
                        objetSel = (mx - 200) / 60; guiltSel = -1;
                        printf("Objet %d (%s) sélectionné.\n", objetSel, nomObjets[objetSel]);
                    }
                    // Clic sur un suspect
                    else if (gameStarted && mx >= 100 && mx < 100 + 150 && my >= 350 && my < 350 + MAX_CARDS * 30) {
                        guiltSel = (my - 350) / 30; joueurSel = -1; objetSel = -1;
                         printf("Suspect %d (%s) sélectionné pour accusation.\n", guiltSel, nomPersonnages[guiltSel]);
                    }
                    // Clic sur la case à cocher
                    else if (gameStarted && mx >= 100 + 150 && mx < 100 + 150 + 30 && my >= 350 && my < 350 + MAX_CARDS * 30) {
                        int idx = (my - 350) / 30;
                        if (idx >= 0 && idx < MAX_CARDS) {
                            guiltGuess[idx] = 1 - guiltGuess[idx];
                            printf("Suspect %d (%s) %s.\n", idx, nomPersonnages[idx], guiltGuess[idx] ? "barré" : "débarré");
                        }
                    }
                    else { // Clic ailleurs désélectionne
                         joueurSel = -1; objetSel = -1; guiltSel = -1;
                    }

                } break; // Fin SDL_MOUSEBUTTONDOWN
                 case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        quit = 1;
                    }
                    break;
            } // Fin switch event.type
        } // Fin while SDL_PollEvent

        // 2. Vérifier si des données sont arrivées du serveur (non bloquant)
        if (server_sockfd != -1) {
            FD_ZERO(&client_fds);
            FD_SET(server_sockfd, &client_fds);
            int activity = select(server_sockfd + 1, &client_fds, NULL, NULL, &select_timeout);

            if (activity < 0 && errno != EINTR) {
                perror("select error on client");
                updateStatusMessage("Erreur: Problème de communication.");
                close(server_sockfd); server_sockfd = -1; initializeClientState();
            } else if (activity > 0 && FD_ISSET(server_sockfd, &client_fds)) {
                bzero(recvBuffer, MAX_BUFFER);
                int n = read(server_sockfd, recvBuffer, MAX_BUFFER - 1);
                if (n <= 0) {
                    if (n == 0) { printf("Le serveur a fermé la connexion.\n"); updateStatusMessage("Déconnecté: Serveur fermé."); }
                    else { perror("ERROR reading from server socket"); updateStatusMessage("Erreur: Lecture serveur échouée."); }
                    close(server_sockfd); server_sockfd = -1; initializeClientState();
                } else {
                     recvBuffer[n] = '\0';
                     char *start = recvBuffer; char *end;
                     while ((end = strchr(start, '\n')) != NULL) {
                         *end = '\0';
                         if (strlen(start) > 0) { processServerMessage(start); }
                         start = end + 1;
                     }
                     if (strlen(start) > 0) { processServerMessage(start); }
                }
            }
        }

        // 3. Mettre à jour l'interface graphique
        renderUI();

        // SDL_Delay(1); // Peut être nécessaire si CPU à 100%

    } // Fin boucle principale while(!quit)

    // Nettoyage final
    cleanup();
    return 0;
}