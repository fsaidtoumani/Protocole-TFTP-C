# Protocole-TFTP-C
## Description
    Ce projet vise à implémenter, et à étendre, le protocole Trivial File Transfer Protocol (TFTP) en C, en se concentrant sur le contrôle de flux et la gestion des connexions réseau. TFTP est un protocole léger utilisé pour transférer des fichiers sur un réseau. Ce projet approfondira vos compétences en programmation réseau, en gestion de la perte de paquets, et en synchronisation dans un environnement multi-utilisateurs.

## Fonctionnaliés 
    Gestion des requêtes de lecture (RRQ) pour envoyer des fichiers au client.
    Gestion des requêtes d'écriture (WRQ) pour recevoir des fichiers du client.
    Gestion des erreurs pour les cas où le fichier demandé n'existe pas ou lorsque les permissions d'accès sont refusées.
    Support des timeouts pour les opérations réseau afin de gérer les pertes de paquets.

## Prérequis
    Système d'exploitation compatible.
    Compilateur C (GCC par exemple).

## Utilisation
    Cloner le dépot github : 
        https://github.com/fsaidtoumani/Protocole-TFTP-C.git
    
    Se placer dans le répertoire et compiler le programme principal avec
        gcc Serveur_tftp.c -o serveur
        gcc Client_tftp.c -o client

    Exécuter le serveur et le client avec
        ./serveur
        ./client <Adresse IP>

## Configuration
    Le serveur est configuré pour écouter sur le port 69 par défaut. Vous pouvez modifier ce paramètre dans le fichier source serveur.c si nécessaire.
    Les fichiers à lire ou à écrire doivent être stockés dans le répertoire tftpboot/ à la racine du projet.

## Auteur
    SAID TOUMANI FAHARDINE
    SAD SAOUD Farid
