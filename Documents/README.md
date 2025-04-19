# Sherlock 13

Un jeu de déduction réseau en C/SDL2 inspiré du « Sherlock 13 ».  
Architecture client‑serveur, communication TCP, interface graphique SDL2.

---

## Table des matières

1. [Présentation](#présentation)  
2. [Prérequis](#prérequis)  
3. [Installation & Compilation](#installation--compilation)  
4. [Lancement du serveur](#lancement-du-serveur)  
5. [Lancement des clients](#lancement-des-clients)  
6. [Protocole de messages](#protocole-de-messages)  
7. [Usage et interface](#usage-et-interface)  
8. [Structure du projet](#structure-du-projet)  
9. [Dépendances externes](#dépendances-externes)  

---

## Présentation

Sherlock 13 est un jeu de déduction multijoueur.  
- Un **serveur** central en C gère :  
  - la distribution aléatoire des cartes (13 cartes, 1 coupable + 3 par joueur),  
  - la logique de tour,  
  - la réception des actions des joueurs (accusation, suspicion, interrogation),  
  - la diffusion des résultats à tous les clients.  
- Des **clients** graphiques en C/SDL2 :  
  - se connectent au serveur,  
  - affichent la main (3 cartes), la grille partagée (compte d’objets), la liste des suspects,  
  - permettent les interactions (clics, bouton GO),  
  - contiennent un bouton **Connect** pour rejoindre la partie.

---

## Prérequis

- Linux (ou WSL sous Windows)  
- [SDL2](https://www.libsdl.org/) et extensions :  
  - `libsdl2-dev`  
  - `libsdl2-image-dev`  
  - `libsdl2-ttf-dev`  
- `gcc`, `make` ou un simple shell `sh`  
- (Optionnel) `git`  

---

## Installation & Compilation

Un script `cmd.sh` est fourni pour compiler le serveur et le client :

1. Assurez-vous que `cmd.sh` est exécutable :
   ```bash
   chmod +x cmd.sh
   ```
2. Lancez le script :
   ```bash
   ./cmd.sh
   ```
   Ce script produit :
   - un exécutable `server`
   - un exécutable `sh13`

---

## Lancement du serveur

```bash
./server <port>
```

Exemple :
```bash
./server 4444
```

Le serveur attend 4 connexions avant de démarrer la partie. Il affiche dans la console :
- les connexions clients,
- l’état du paquet (deck) et de la table d’objets,
- l’avancement du jeu (tours, éliminations, victoire).

---

## Lancement des clients

Pour chaque joueur, ouvrez un terminal différent et exécutez :

```bash
./sh13 <srv_ip> <srv_port> <cli_ip> <cli_port> <player_name>
```

Exemple (en local) :
```bash
./sh13 127.0.0.1 4444 127.0.0.1 5550 Alice
./sh13 127.0.0.1 4444 127.0.0.1 5551 Bob
./sh13 127.0.0.1 4444 127.0.0.1 5552 Carol
./sh13 127.0.0.1 4444 127.0.0.1 5553 Dave
```

- **srv_ip** / **srv_port** : adresse et port du serveur.  
- **cli_ip** / **cli_port** : (historique) non utilisés pour l’écoute, mais requis par la ligne de commande.  
- **player_name** : identifiant de joueur (unique).

---

## Protocole de messages

| Code | Direction           | Signification                         | Exemple client → serveur         | Exemple serveur → client    |
|------|---------------------|---------------------------------------|----------------------------------|-----------------------------|
| C    | C → S               | Connexion                            | `C 127.0.0.1 5550 Alice`         | —                           |
| I    | S → C               | Attribution d’ID                     | —                                | `I 0`                       |
| L    | S → C               | Liste des joueurs                    | —                                | `L Alice Bob Carol Dave`    |
| D    | S → C               | Distribution des cartes              | —                                | `D 7 2 5`                   |
| V    | S → C               | Valeur suspicion (`S`) ou initiale   | —                                | `V 1 3 0`                   |
| K    | S → C               | Résultat interrogation (`O`)         | —                                | `K 2 5 100`                 |
| M    | S → C               | Tour courant                         | —                                | `M 2` / `M -1` (fin)        |
| G    | C → S               | Accusation (Guess)                   | `G 1 6`                          | `Z` si juste / `P` si faux  |
| O    | C → S               | Interrogation d’objet                | `O 1 4`                          | `K <j> <o> <0|100>`         |
| S    | C → S               | Suspicion ciblée                     | `S 1 2 3`                        | `V <j> <o> <valeur>`        |
| Z    | S → C               | Victoire                             | —                                | affiche “GAGNÉ”            |
| P    | S → C               | Élimination                          | —                                | affiche “PERDU”            |

---

## Usage et interface

1. **Connect**  
   - Cliquez sur **Connect** pour rejoindre la partie.  
   - Les noms des joueurs apparaissent, Connect se désactive.  

2. **Début de partie**  
   - Les clients reçoivent leurs 3 cartes (affichées en bas à droite).  
   - La **grille** montre, par joueur et par objet, les comptes (–1 = inconnu, `*` = possède).  
   - Le **premier joueur** est mis en surbrillance verte.  

3. **Tour de jeu**  
   - **Sélection suspicion** (`S`) : cliquez sur une **ligne joueur** puis une **colonne objet**, puis **GO**.  
   - **Interrogation** (`O`) : sélectionnez un objet seul, puis **GO**.  
   - **Accusation** (`G`) : cliquez sur un **nom de suspect**, puis **GO**.  
   - Les résultats (`V` ou `K`) mettent à jour la grille pour tous.  
   - Le tour passe au joueur actif suivant.

4. **Carnet de notes**  
   - La **liste des suspects** (à gauche) comporte 13 noms + icônes.  
   - Cliquez sur la case de chaque suspect pour le **barrer** (croix rouge) – aide locale seulement.

5. **Fin de partie**  
   - `Z` → victoire (écran « GAGNÉ »).  
   - `P` → éliminé (écran « PERDU »).  
   - `M -1` signale la fin du jeu pour tous.  

---

## Structure du projet

```
sh13_etu/
├── server.c           # Serveur TCP, logique du jeu
├── sh13.c             # Client SDL2, interface graphique
├── cmd.sh             # Script de compilation rapide
├── images/            # Toutes les images PNG nécessaires
│   ├── SH13_0.png … SH13_12.png
│   ├── SH13_pipe_120x120.png … SH13_crane_120x120.png
│   ├── gobutton.png
│   ├── connectbutton.png
│   ├── perdu.png
│   └── gagner.png
├── font/              # Polices TTF (e.g. sans.ttf)
└── README.md          # Ce fichier
```

---

## Dépendances externes

- **SDL2**  
- **SDL2_image**  
- **SDL2_ttf**  
- **pthread** (pour la version threadée, si utilisée)  
- **make** & **gcc**  

---

## Auteur

**Vedat SUMBUL**  
Étudiant en 4ᵉ année, EI4  
Numéro étudiant : 21115513  
Email : vedat.sumbul@etu.sorbonne-universite.fr  

