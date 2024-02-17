#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h> // Pour errno et les codes d'erreur
#include <sys/time.h>

#define RRQ 1
#define WRQ 2
#define MAX_PACKET_SIZE 516
#define MAX_SIZE_DATA 512
#define MAX_SIZE_FILE 506
#define SERVER_PORT 69
#define SERVER_IP "127.0.0.1"

// Fonction de lecture du fichier RRQ

void lire_fichier_rrq(const char *filename, struct sockaddr_in server_addr)
{
    int sockfd;
    char buffer[MAX_PACKET_SIZE];
    int len, n;
    char dir[100];
    strcpy(dir, REPERTOIR_SERV);
    strcat(dir, filename);
    struct timeval tv;
    FILE *fptr;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    // Création du socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("Erreur de création du socket");
        exit(EXIT_FAILURE);
    }

    // On essaie d'ouvrir le fichier en mode binaire
    fptr = fopen(dir, "wb");
    printf("Ouverture du fichier %s\n", dir);
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
            strcpy(buffer + 4, "File not found");
            sendto(sockfd, buffer, 4 + strlen("File not found") + 1, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
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
            strcpy(buffer + 4, "Access violation");
            sendto(sockfd, buffer, 4 + strlen("Access violation") + 1, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
            return;
        }
        else
        {
            printf("Erreur : %s\n", strerror(errno));
            // Gerere les autres erreurs ...
            return;
        }
    }

  // Definition des paramettre du time out
  tv.tv_sec = 5;  // Timeout de 5 secondes
  tv.tv_usec = 0; // 0 microsecondes

  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0)
  {
    perror("Erreur lors de la définition du timeout");
    exit(EXIT_FAILURE);
  }

  /* Reception des paquets de donnée */

  // Initialisation du numéro de bloc
  unsigned short bloc_atuel=1;
  unsigned char ack[4];
  ack[0] = 0;
  ack[1] = 4; // Opcode pour ACK
  ack[2] = bloc_atuel >> 8;
  ack[3] = bloc_atuel; // Numéro de bloc

    while (1)
    {
        char buffer_recv[MAX_ACK_SIZE];

        // Réception d'un paquet de donné
        len = recvfrom(sockfd, buffer_recv, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Timeout : aucun paquet reçu dans l'intervalle de temps spécifié
                printf("Timeout : aucun paquet reçu. | On renvoie la requette au serveur\n");
                sendto(sockfd, ack, MAX_ACK_SIZE, 0, (const struct sockaddr *)&from_addr, sizeof(from_len));
                continue;
            }
            else
            {
                perror("Erreur de réception du paquet");
                exit(EXIT_FAILURE);
            }
        }

        memcpy(buffer, buffer_recv, len);

        // Vérification de l'opcode du paquet reçu
        if (buffer[1] == 3) // Opcode 3 indique un paquet de donnée
        {
            // Recuperation du numéro de bloc
            int blocknum = (buffer[2] << 8) | buffer[3];

       printf("Bloc %d reçu, taille des données: %d octets\n", blocknum, len - 4);

      // Verfiication du numéro de bloc
      if (blocknum != bloc_atuel)
      {
        printf("[Client] Erreur : numéro de bloc incorrect mb:%d / %d | on redemande le bon bloque au serveur \n", bloc_atuel, blocknum);
        ack[0] = 0;
        ack[1] = 4; // Opcode pour ACK
        ack[2] = bloc_atuel >> 8;
        ack[3] = bloc_atuel; // Numéro de bloc
        sendto(sockfd, ack, 4, 0, (const struct sockaddr *)&from_addr, from_len);
        break;
      }

      // Incrémenter le numéro de bloc bit a bit sur 2 bytes pour le prochain paquet
      bloc_atuel++;

            // On reconstruit le fichier  à partir des paquets de données reçus en faisant un seek pour gere les erreurs eventuelles
            for (int i = 0; i < len - 4; i++)
            {
                int f;
                f = fwrite(buffer + 4 + i, 1, 1, fptr);
                if (f == EOF)
                {
                    printf("Erreur lors de l'écriture dans le fichier\n");
                    // On envoie un paquet d'erreur
                    buffer[0] = 0;
                    buffer[1] = 5; // Opcode pour ERROR
                    buffer[2] = 0;
                    buffer[3] = 3; // Code d'erreur 3 : Disque plein
                    strcpy(buffer + 4, "Disk full");
                    sendto(sockfd, buffer, 4 + strlen("Disk full") + 1, 0, (const struct sockaddr *)&from_addr, sizeof(from_addr));
                    return;
                }
            }
            printf("Bloc %d reçu, taille des données: %d bytess\n", blocknum, len - 4);
            // Si la taille des données est inférieure à 512, c'est le dernier paquet
            if (len < MAX_PACKET_SIZE)
            {
                printf("Transmission terminée.\n");
                break;
            }
            // Envoi d'un accusé de réception (ACK)

            ack[0] = 0;
            ack[1] = 4; // Opcode pour ACK
            ack[2] = buffer[2];
            ack[3] = buffer[3]; // Numéro de bloc
            sendto(sockfd, ack, MAX_ACK_SIZE, 0, (const struct sockaddr *)&from_addr, sizeof(from_addr));
        }
    }

    // On envoie le dernier ACK
    ack[0] = 0;
    ack[1] = 4; // Opcode pour ACK
    ack[2] = buffer[2];
    ack[3] = buffer[3]; // Numéro de bloc
    sendto(sockfd, ack, MAX_ACK_SIZE, 0, (const struct sockaddr *)&from_addr, sizeof(from_addr));

  // Fermeture du socket
  close(sockfd);
}

// Fonction pour la reponse de lecture RRQ
void RRQ_reponse(char *filename, struct sockaddr_in server_addr)
{
    int sockfd;
    char buffer[MAX_PACKET_SIZE];
    int len, n;
    char dir[100];
    strcpy(dir, REPERTOIR_SERV);
    strcat(dir, filename);
    struct timeval tv;
    FILE *fptr;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int flag_fin = 0;

    // Création du socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("Erreur de création du socket");
        exit(EXIT_FAILURE);
    }

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
            strcpy(buffer + 4, "File not found");
            sendto(sockfd, buffer, 4 + strlen("File not found") + 1, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
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
            strcpy(buffer + 4, "Access violation");
            sendto(sockfd, buffer, 4 + strlen("Access violation") + 1, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
            return;
        }
        else
        {
            printf("Erreur : %s\n", strerror(errno));
            // Gerere les autres erreurs ...
            return;
        }
    }

    // Definition des paramettre du time out
    tv.tv_sec = 5;  // Timeout de 5 secondes
    tv.tv_usec = 0; // 0 microsecondes

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0)
    {
        perror("Erreur lors de la définition du timeout");
        exit(EXIT_FAILURE);
    }

    /* En voie et Reception des paquets de donnée */

    // Initialisation du numéro de bloc
    int bloc_atuel = 1;
    int longeur_data = 0;

    // Envoi du premier paquet de données
    buffer[0] = 0;
    buffer[1] = 3;
    buffer[2] = 0;
    buffer[3] = bloc_atuel;
    for (int i = 0; i < MAX_SIZE_DATA; i++)
    {
        buffer[i + 4] = fgetc(fptr);
        longeur_data++;
        if (feof(fptr))
        {
            longeur_data--;
            flag_fin = 1;
            break;
        }
    }
    sendto(sockfd, buffer, longeur_data+4, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));

    while (1)
    {
        char buffer_recv[MAX_ACK_SIZE]; // buffer pour la reception des paquets ACK

        // Réception d'un paquet de donné
        len = recvfrom(sockfd, buffer_recv, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Timeout : aucun paquet reçu dans l'intervalle de temps spécifié
                printf("Timeout : aucun paquet reçu. | On renvoie la requette au serveur\n");
                sendto(sockfd, buffer, longeur_data+4, 0, (const struct sockaddr *)&from_addr, sizeof(from_len));
                continue;
            }
            else
            {
                perror("Erreur de réception du paquet");
                exit(EXIT_FAILURE);
            }
        }

        memcpy(buffer, buffer_recv, len);

        // Vérification de l'opcode du paquet reçu
        if (buffer[1] == 4) // Opcode 4 indique un paquet ACK
        {
            // Recuperation du numéro de bloc
            int blocknum = (buffer[2] << 8) | buffer[3];

            // printf("Bloc %d reçu, taille des données: %d bytess\n", blocknum, len - 4);

            if (flag_fin == 1)
            {
                // On regarde si le dernier paquet a bien été receptionné
                if (blocknum == bloc_atuel)
                {
                    printf("Transmission terminée.\n");
                    break;
                }
                else
                {
                    printf("Erreur : numéro de bloc incorrect :%d | %d\n ", blocknum, bloc_atuel);
                    // On renvoie le paquet de données
                    sendto(sockfd, buffer, MAX_PACKET_SIZE, 0, (const struct sockaddr *)&from_addr, sizeof(from_len));
                    continue;
                }
            }

            // Verfiication du numéro de bloc
            if (blocknum != bloc_atuel)
            {
                printf("Erreur : numéro de bloc incorrect :%d | %d\n ", blocknum, bloc_atuel);
                // On renvoie le paquet de données
                sendto(sockfd, buffer, MAX_PACKET_SIZE, 0, (const struct sockaddr *)&from_addr, sizeof(from_len));
                continue;
            }

            // Incrémenter le numéro de bloc
            bloc_atuel++;

            // On initialise le buffer pour le prochain paquet de données
            buffer[0] = 0;
            buffer[1] = 3;
            buffer[2] = 0;
            buffer[3] = bloc_atuel;
            longeur_data = 0;

            // On envois par paquet le fichier à partir des paquets de données reçus en faisant un seek pour gere les erreurs eventuelles
            for (int i = 0; i < MAX_SIZE_DATA; i++)
            {
                buffer[i + 4] = fgetc(fptr);
                longeur_data++;
                if (feof(fptr))
                {
                    longeur_data--;
                    flag_fin = 1;
                    break;
                }
            }
            printf("Bloc %d va etre envoyé, taille des données: %d bytess\n", blocknum, longeur_data);
            sendto(sockfd, buffer, longeur_data+4, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
        }
    }
    // Fermeture du socket
    close(sockfd);
    // Fermeture du fichier
    fclose(fptr);
}

int main(void)
{

  // Defining variables
  struct sockaddr_in server_addr;

  // Configuration de l'adresse du serveur
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);


  // Envoyé en RRQ le nom du fichier pour lire le fichier
  while (1)
  {
    char filename[MAX_SIZE_FILE];
    scanf("%s", filename);
    lire_fichier_rrq(filename, server_addr);
  }

  return 0;
}