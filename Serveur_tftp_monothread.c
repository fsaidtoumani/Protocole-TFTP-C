#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h> // Pour errno et les codes d'erreur
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>

/*
    Version avec un seul thread avec la fonction select() pour gérer les requêtes de plusieurs clients
*/

#define NBMAX_CLIENTS 5
#define MAX_ACK_SIZE 4
#define RRQ 1
#define WRQ 2
#define MAX_PACKET_SIZE 516
#define MAX_SIZE_DATA 512
#define MAX_SIZE_FILE 506
#define SERVER_PORT 69
#define SERVER_IP "0.0.0.0"
#define REPERTOIR_SERV "./tftpboot/"

// Structure pour stocker les informations du client
typedef struct
{
    int sockfd;                    // Socket du client
    struct sockaddr_in addr;       // Adresse du client
    int pos;                       // Position dans le buffer du fichier
    char filename[MAX_SIZE_FILE];  // Nom du fichier
    int opcode;                    // Opcode de la requête de base du client
    unsigned short blocknum;       // Numéro de bloc
    unsigned short blocknum_recev; // Dernier numéro de bloc
    bool endfile;                  // Indique si le transfert est terminé
    bool fini;                     // Indique si le transfert est terminé

} Client;

// Liste chainée pour stocker les clients
typedef struct Listeclient
{
    Client *client;
    struct Listeclient *next;
} Listeclient;

// Fontion pour les elements de la liste  si fini
void supprimer(Listeclient **listeclients)
{
    Listeclient *ptrliste = *listeclients;
    Listeclient *ptrliste_prec = NULL;
    while (ptrliste != NULL)
    {
        if (ptrliste->client->fini)
        {
            if (ptrliste_prec == NULL)
            {
                *listeclients = ptrliste->next;
                free(ptrliste->client);
                free(ptrliste);
                ptrliste = *listeclients;
            }
            else
            {
                ptrliste_prec->next = ptrliste->next;
                free(ptrliste->client);
                free(ptrliste);
                ptrliste = ptrliste_prec->next;
            }
        }
        else
        {
            ptrliste_prec = ptrliste;
            ptrliste = ptrliste->next;
        }
    }
}

// Fonction d'authorisation pour la priorité d'écriture
bool autorisation_ecrire(Listeclient *listeclients, Client *client)
{
    // On parcourt la liste des clients pour vérifier si un autre clien avant n'est pas entrain de lire ou d'écrire sur le même fichier
    Listeclient *ptrliste = listeclients;
    while (ptrliste->client != client)
    {
        if (strcmp(ptrliste->client->filename, client->filename) == 0) // On a trouvé un client qui fait une opperation sur le même fichier
        {
            return false;
        }
        ptrliste = ptrliste->next;
    }
    return true;
}

// Fonction d'autorisation pour la priorité de lecture
bool autorisation_lecture(Listeclient *listeclient, Client *client)
{
    // On parcour la liste des client pour verifier si un client avant lui n'est pas entrain decrire ou shouaite ecrire
    Listeclient *ptrlist = listeclient;
    while (ptrlist->client != client)
    {
        if (strcmp(ptrlist->client->filename, client->filename) == 0 && (ptrlist->client->opcode == 3 || ptrlist->client->opcode == WRQ))
        {
            return false;
        }
        ptrlist = ptrlist->next;
    }
    return true;
}

// Fonction qui verifie si un client n'est pas deja dans la liste
Listeclient *client_existe(Listeclient *listeclients, struct sockaddr_in clientAddr)
{
    Listeclient *ptrliste = listeclients;
    while (ptrliste != NULL)
    {
        if (ptrliste->client->addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr && ptrliste->client->addr.sin_port == clientAddr.sin_port)
        {
            return ptrliste;
        }
        ptrliste = ptrliste->next;
    }
    return NULL;
}

// fonction pour la reponse d'ecriture
void WRQ_reponse(Client *client, unsigned char *buffer, ssize_t len, Listeclient *listeclients)
{
    unsigned char buffererr[MAX_PACKET_SIZE];
    char dir[100];
    char filename[MAX_SIZE_FILE];
    char ack[MAX_ACK_SIZE];
    ack[0] = 0;
    ack[1] = 4; // Opcode pour ACK
    FILE *fptr;
    bool autoriser = autorisation_ecrire(listeclients, client);

    // Analyse du satatut du clien pour savoir ou on en est
    // On refarde si c'est le premier paquet qui est envoyé

    strcpy(filename, client->filename);
    strcpy(dir, REPERTOIR_SERV);
    strcat(dir, filename);

    // On essaie d'ouvrir le fichier en mode binaire
    if (autoriser)
    {
        printf("le clien %s:%d est autorisé à écrire blocknum=%d\n", inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), client->blocknum);
        if (client->blocknum == 0)
            fptr = fopen(dir, "wb");
        else
            fptr = fopen(dir, "ab");
        printf("Ouverture du fichier %s\n", dir);

        if (fptr == NULL)
        {
            // Gestion de l'erreur
            if (errno == ENOENT)
            {
                printf("Erreur : Le fichier n'existe pas\n");

                // Envoi d'un paquet d'erreur
                buffererr[0] = 0;
                buffererr[1] = 5; // Opcode pour ERROR
                buffererr[2] = 0;
                buffererr[3] = 1; // Code d'erreur 1 : Fichier non trouvé
                strcpy((char *)buffererr + 4, "File not found");
                sendto(client->sockfd, buffererr, 4 + strlen("File not found") + 1, 0, (const struct sockaddr *)&client->addr, sizeof(client->addr));
                return;
            }
            else if (errno == EACCES)
            {
                printf("Erreur : Permission refusée\n");

                // Envoi d'un paquet d'erreur
                buffererr[0] = 0;
                buffererr[1] = 5; // Opcode pour ERROR
                buffererr[2] = 0;
                buffererr[3] = 2; // Code d'erreur 2 : Access violation
                strcpy((char *)buffererr + 4, "Access violation");
                sendto(client->sockfd, buffererr, 4 + strlen("Access violation") + 1, 0, (const struct sockaddr *)&client->addr, sizeof(client->addr));
                return;
            }
            else
            {
                printf("Erreur : %s\n", strerror(errno));
                // Gerere les autres erreurs ...
                return;
            }
        }
    }
    else
    {
        printf("Le client ip/port %s:%d n'est pas autorisé car une autre operation est deja en court sur le fichier %s", inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), client->filename);
    }

    if (client->opcode == 3 && autoriser)
    {
        // Cas particulier si le client etait en attente: le block num= zero alors qu'on a recu un paquet de donnée
        if (client->blocknum == 0)
            client->blocknum = 1;

        // On verifie si le bloc coorespondant au dernier paquet de donnée envoyé a été reçu
        if (client->blocknum != client->blocknum_recev)
        {
            printf("Erreur : numéro de bloc incorrect :%d | %d\n ", client->blocknum_recev, client->blocknum);
            // On renvoie on demande le bon
            ack[3] = client->blocknum >> 8;
            ack[4] = client->blocknum;
            sendto(client->sockfd, ack, MAX_ACK_SIZE, 0, (const struct sockaddr *)&client->addr, sizeof(client->addr));
            fclose(fptr);
            return;
        }

        /* En voie et Reception des paquets de donnée */

        // On se depalce dans le fichier
        fseek(fptr, client->pos, SEEK_SET);
        for (int i = 0; i < len - 4; i++)
        {
            int f;
            f = fwrite(buffer + 4 + i, 1, 1, fptr);
            if (f == EOF)
            {
                printf("Erreur lors de l'écriture dans le fichier\n");
                // On envoie un paquet d'erreur
                buffererr[0] = 0;
                buffererr[1] = 5; // Opcode pour ERROR
                buffererr[2] = 0;
                buffererr[3] = 3; // Code d'erreur 3 : Disque plein
                strcpy((char *)buffer + 4, "Disk full");
                sendto(client->sockfd, buffererr, 4 + strlen("Disk full") + 1, 0, (const struct sockaddr *)&client->addr, sizeof(client->addr));
                fclose(fptr);
                return;
            }
        }
        // Si c'est le dernier paquet, on met à jour les informations du client
        if (len - 4 < MAX_SIZE_DATA)
        {
            printf("Transmission terminée.\n");
            client->fini = true;
        }

        client->pos += len - 4;
        // Fermeture du fichier
        fclose(fptr);
    }

    ack[2] = client->blocknum >> 8;
    ack[3] = client->blocknum; // Numéro de bloc
    if (autoriser)             // On incremente si celui ci peut ecrir
        client->blocknum++;
    // Envoi du premier ACK pour indiquer que le serveur est prêt à recevoir le fichier
    sendto(client->sockfd, ack, MAX_ACK_SIZE, 0, (const struct sockaddr *)&client->addr, sizeof(client->addr));
}

// Fonction pour la reponse de lecture RRQ
void RRQ_reponse(Client *client, Listeclient *listeclients)
{
    unsigned char buffer[MAX_PACKET_SIZE];
    size_t longeur_data = 0;
    int len, n;
    char dir[100];
    char filename[MAX_SIZE_FILE];
    struct timeval tv;
    FILE *fptr;
    bool autoriser = autorisation_lecture(listeclients, client);
    // Analyse du satatut du clien pour savoir ou on en est
    socklen_t from_len = sizeof(client->addr);
    // On refarde si c'est le premier paquet qui est envoyé
    strcpy(filename, client->filename);
    strcpy(dir, REPERTOIR_SERV);
    strcat(dir, filename);

    // On essaie d'ouvrir le fichier en mode binaire
    fptr = fopen(dir, "rb");
    printf("[RRQ]Ouverture du fichier %s\n", dir);
    if (fptr == NULL)
    {
        // Gestion de l'erreur
        if (errno == ENOENT)
        {
            printf("Erreur : Le fichier n'existe pas\n");

            // Envoi d'un paquet d'erreur
            buffer[0] = 0;
            buffer[1] = 5; // Opcode pour ERROR
            buffer[2] = 0;
            buffer[3] = 1; // Code d'erreur 1 : Fichier non trouvé
            strcpy((char *)buffer + 4, "File not found");
            sendto(client->sockfd, buffer, 4 + strlen("File not found") + 1, 0, (const struct sockaddr *)&client->addr, from_len);
            return;
        }
        else if (errno == EACCES)
        {
            printf("Erreur : Permission refusée\n");

            // Envoi d'un paquet d'erreur
            buffer[0] = 0;
            buffer[1] = 5; // Opcode pour ERROR
            buffer[2] = 0;
            buffer[3] = 2; // Code d'erreur 2 : Access violation
            strcpy((char *)buffer + 4, "Access violation");
            sendto(client->sockfd, buffer, 4 + strlen("Access violation") + 1, 0, (const struct sockaddr *)&client->addr, from_len);
            return;
        }
        else
        {
            printf("Erreur : %s\n", strerror(errno));
            // Gerere les autres erreurs ...
            return;
        }
    }

    /* En voie et Reception des paquets de donnée */

    // On se positionne dans le fichier
    fseek(fptr, client->pos, SEEK_SET);

    // On verifie si l'ACK  coorespondant au dernier paquet de donnée envoyé a été reçu
    if (client->opcode == 4)
    {

        if (client->blocknum_recev != client->blocknum)
        {
            printf("Erreur : numéro de bloc incorrect :%d | %d\n ", client->blocknum_recev, client->blocknum);
            // On renvoie le paquet de données en deplaçant le curseur
            fseek(fptr, client->blocknum_recev * MAX_SIZE_DATA, SEEK_SET);
            client->pos = client->blocknum_recev * MAX_SIZE_DATA - client->pos;
            client->blocknum = client->blocknum_recev;
        }
        else if (client->blocknum_recev == client->blocknum)
        {
            // On regade si c'tait le dernier paquet

            if (client->endfile)
            {
                printf("Transmission terminée.\n");
                fclose(fptr);
                client->fini = true;
                return;
            }
            client->blocknum++;
        }
    }

    if (!autoriser)
    {
        printf("Le client ip/port %s:%d n'est pas autorisé a lire car une autre operation d'ecriture est deja en cours ou prevu avant lui sur le fichier %s", inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), client->filename);
        client->blocknum = 0; // On remet à zero le numéro de bloc pour que le client puisse réessayer
    }

    // Envoi du paquet de données
    for (int i = 0; i < MAX_SIZE_DATA; i++)
    {
        buffer[i + 4] = fgetc(fptr);
        longeur_data++;
        if (feof(fptr))
        {
            longeur_data--;
            client->endfile = true;
            break;
        }
    }
    // Initialisation du numéro de bloc
    buffer[0] = 0;
    buffer[1] = 3;
    buffer[2] = client->blocknum >> 8;
    buffer[3] = client->blocknum;
    sendto(client->sockfd, buffer, longeur_data + 4, 0, (const struct sockaddr *)&client->addr, from_len);

    // On met à jour les informations du client
    client->pos += longeur_data;

    // Fermeture du fichier
    fclose(fptr);
}

void traitement_requete(int sockfd, struct sockaddr_in clientAddr, unsigned char buffer[], ssize_t len, Listeclient **listeclients)
{
    // Vérification de l'opcode de la requête
    if (buffer[1] == RRQ) // Opcode 1 indique une requête de lecture
    {
        printf("Requête de lecture (RRQ) reçue du client %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        // On verifie si le client n'est pas deja dans la liste
        Listeclient *client = client_existe(*listeclients, clientAddr);
        if (client != NULL)
        {
            fprintf(stdout, "Le client %s:%d est déjà dans la liste\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
            // On met ajour les informations du client
            client->client->addr = clientAddr;
            client->client->sockfd = sockfd;
            client->client->pos = 0;
            client->client->opcode = buffer[1];
            client->client->blocknum = 1;
            client->client->endfile = false;
            client->client->fini = false;
            RRQ_reponse(client->client,*listeclients);
        }
        else
        {
            fprintf(stdout, "Le client %s:%d n'est pas dans la liste\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

            // On crée un nouveau client
            Listeclient *newelmnt = malloc(sizeof(*newelmnt));
            newelmnt->client = malloc(sizeof(*newelmnt->client));
            newelmnt->client->addr = clientAddr;
            newelmnt->client->sockfd = sockfd;
            newelmnt->client->pos = 0;
            newelmnt->client->opcode = buffer[1];
            newelmnt->client->blocknum = 1;
            newelmnt->client->endfile = false;
            newelmnt->client->fini = false;
            // On copie le nom du fichier dans le buffer du client
            for (int i = 0; i < len; i++)
            {
                if (i > 1)
                    newelmnt->client->filename[i - 2] = buffer[i];
            }
            while (newelmnt->client->blocknum != 1)
            {
                newelmnt->client->blocknum = 1;
            }

            // On ajoute le client à la liste des clients
            newelmnt->next = NULL;
            Listeclient *ptrliste = *listeclients;
            if (ptrliste == NULL)
            {
                *listeclients = newelmnt;
            }
            else
            {
                while (ptrliste->next != NULL)
                {
                    ptrliste = ptrliste->next;
                }
                ptrliste->next = newelmnt;
            }

            if (listeclients == NULL)
            {
                printf("Liste vide\n");
            }
            RRQ_reponse(newelmnt->client, *listeclients);
        }
    }
    else if (buffer[1] == WRQ) // Opcode 2 indique une requête d'écriture
    {
        printf("Requête d'écriture (WRQ) reçue du client %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        // On crée un nouveau client
        Listeclient *newelmnt = malloc(sizeof(*newelmnt));
        newelmnt->client = malloc(sizeof(*newelmnt->client));
        newelmnt->client->addr = clientAddr;
        newelmnt->client->sockfd = sockfd;
        newelmnt->client->pos = 0;
        newelmnt->client->opcode = buffer[1];
        newelmnt->client->blocknum = 0;
        newelmnt->client->endfile = false;
        newelmnt->client->fini = false;
        // On copie le nom du fichier dans le buffer du client
        for (int i = 0; i < len; i++)
        {
            if (i > 1)
                newelmnt->client->filename[i - 2] = buffer[i];
        }
        // On ajoute le client à la liste des clients
        newelmnt->next = NULL;
        Listeclient *ptrliste = *listeclients;
        if (ptrliste == NULL)
        {
            *listeclients = newelmnt;
        }
        else
        {
            while (ptrliste->next != NULL)
            {
                ptrliste = ptrliste->next;
            }
            ptrliste->next = newelmnt;
        }
        if (listeclients == NULL)
        {
            printf("Liste vide\n");
        }
        WRQ_reponse(newelmnt->client, buffer, len, *listeclients);
    }
    else if (buffer[1] == 4) // Opcode 4 indique un paquet ACK
    {
         printf("Paquet ACK reçu du client %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        //  On cherche le client correspondant à l'adresse du client
        Listeclient *ptrliste = *listeclients;
        int cpt = 0;
        if (ptrliste == NULL)
        {
            printf("Liste vide\n");
        }
        while (ptrliste != NULL)
        {

            if (ptrliste->client->addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr && ptrliste->client->addr.sin_port == clientAddr.sin_port)
            {
                printf("Client %d\n", cpt);
                printf("Client trouvé\n");
                // On met à jour les informations du client
                ptrliste->client->blocknum_recev = (buffer[2] << 8) | buffer[3];
                ptrliste->client->opcode = 4;
                // On traite la requête
                RRQ_reponse(ptrliste->client, *listeclients);
                // On supprime le client de la liste si le transfert est terminé
                if (ptrliste->client->fini)
                {
                    supprimer(listeclients);
                }
                break;
            }
            ptrliste = ptrliste->next;
            cpt++;
        }
    }
    else if (buffer[1] == 3) // Opcode 3 indique un paquet de donnée
    {
        printf("Paquet de donnée reçu du client %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        // On cherche le client correspondant à l'adresse du client
        Listeclient *ptrliste = *listeclients;
        int cpt = 0;
        if (ptrliste == NULL)
        {
            printf("Liste vide\n");
        }
        while (ptrliste != NULL)
        {
            if (ptrliste->client->addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr && ptrliste->client->addr.sin_port == clientAddr.sin_port)
            {
                printf("Client %d\n", cpt);
                printf("Client trouvé\n");
                // On met à jour les informations du client
                ptrliste->client->blocknum_recev = (buffer[2] << 8) | buffer[3];
                ptrliste->client->opcode = 3;
                // On traite la requête
                WRQ_reponse(ptrliste->client, buffer, len, *listeclients);
                // On supprime le client de la liste si le transfert est terminé
                if (ptrliste->client->fini)
                {
                    supprimer(listeclients);
                }
                break;
            }
            ptrliste = ptrliste->next;
            cpt++;
        }
    }
    else
    {
        printf("Opcode inconnu\n");
        printf("Requête reçue du client %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

    }
}

int main(void)
{

    // Defining variables
    struct sockaddr_in server_addr, clientAddr;
    char filename[MAX_SIZE_FILE];
    int sockfd;
    unsigned char buffer[MAX_PACKET_SIZE], recev_buffer[MAX_PACKET_SIZE];
    Listeclient *clients = NULL; // initialisation();
    fd_set readfds;
    int len, n;
    struct timeval timeout;
    int nbc = 0; // Nombre de clients

    // Création du socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("Erreur de création du socket");
        exit(EXIT_FAILURE);
    }

    // Configuration de l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&clientAddr, 0, sizeof(clientAddr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Liaison du socket avec l'adresse du serveur
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur de liaison du socket");
        exit(EXIT_FAILURE);
    }
    printf("Serveur TFTP en attente de requêtes...\n");
    while (1)
    {
        // printf("Serveur TFTP en attente de requêtes...\n");
        //  Réception de la requête du client
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        timeout.tv_sec = 5; // Timeout de 5 secondes
        timeout.tv_usec = 0;

        // Attente d'activité sur le socket, timeout après 5 secondes
        int activity = select(sockfd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0)
        {
            perror("select error");
        }
        else if (activity == 0)
        {
            // printf("select timeout\n");
        }
        else
        {
            if (FD_ISSET(sockfd, &readfds))
            {
                socklen_t len = sizeof(clientAddr); // Longueur de l'adresse du client
                int n = recvfrom(sockfd, recev_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&clientAddr, &len);
                if (n < 0)
                {
                    perror("Erreur de réception du paquet");
                    continue;
                }
                for (int i = 0; i < n; i++)
                {
                    buffer[i] = recev_buffer[i];
                }
                // Traitement de la requête
                traitement_requete(sockfd, clientAddr, buffer, n, &clients);
                // clear du buffer
                memset(buffer, 0, MAX_PACKET_SIZE);

                if (clients == NULL)
                {
                    printf("Lisdsdsdte vide\n");
                }
            }
        }
    }

    return 0;
}