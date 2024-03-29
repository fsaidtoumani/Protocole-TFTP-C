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
#define MAX_SIZE_FILE 496
#define SERVER_PORT 69
// #define SERVER_IP "127.0.0.1"

// Fonction de lecture du fichier RRQ
void lire_fichier_rrq(const char *filename, struct sockaddr_in server_addr, const char *option, const char *optionvalue) {
    int sockfd;
    unsigned char buffer[MAX_PACKET_SIZE];
    int len, n;
    struct timeval tv;
    FILE *fptr;
    // Création du socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erreur de création du socket");
        exit(EXIT_FAILURE);
    }

    // Construction du paquet RRQ
    // Format : | 00 | filename | 0 | mode | 0 |
    size_t size_filename = strlen(filename);
    int offset = 4 + size_filename + 1 + 5; // 2 octets pour l'opcode, 2 octet pour les 0 apres le filname et final, 5 octets pour le mode

    if (offset > MAX_PACKET_SIZE) {
        perror("Erreur : Nom de fichier trop long");
        exit(EXIT_FAILURE);
    }

    /* Version plus performant*/
    buffer[0] = 0;
    buffer[1] = RRQ;
    strcpy((char *)buffer + 2, filename);
    buffer[2 + size_filename] = 0;
    strcpy((char *)buffer + 3 + size_filename, "octet");
    buffer[3 + size_filename + 5] = 0;
    strcpy((char *)buffer + 3 + size_filename + 5, option);
    buffer[3 + size_filename + 5 + strlen(option)] = 0;
    strcpy((char *)buffer+3 + size_filename + 5 + strlen(option),optionvalue);
    buffer[3 + size_filename + 5 + strlen(option) + strlen(optionvalue)] = 0;
    // Envoi du RRQ au serveur
    sendto(sockfd, buffer, MAX_PACKET_SIZE, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));

    // Definition des paramettre du time out
    tv.tv_sec = 10;  // Timeout de 5 secondes
    tv.tv_usec = 0; // 0 microsecondes

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0) {
        perror("Erreur lors de la définition du timeout");
        exit(EXIT_FAILURE);
    }

    unsigned short bloc_atuel = 1;
    unsigned char ack[4];
    ack[0] = 0;
    ack[1] = 4; // Opcode pour ACK
    ack[2] = 0;
    ack[3] = bloc_atuel; // Numéro de bloc

    while (1) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        unsigned char buffer_recv[MAX_PACKET_SIZE];
        len = recvfrom(sockfd, buffer_recv, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout : aucun paquet reçu dans l'intervalle de temps spécifié
                printf("Timeout : aucun paquet reçu. | On renvoie la requette au serveur\n");
                sendto(sockfd, buffer, MAX_PACKET_SIZE, 0, (const struct sockaddr *)&from_addr, sizeof(from_len));
                continue;
            } else {
                perror("Erreur de réception du paquet");
                exit(EXIT_FAILURE);
            }
        }
        memcpy(buffer, buffer_recv, len);
        // Vérification de l'opcode du paquet reçu
        if (buffer[1] == 6) {
            char opt[10];
            int i=2,j=0;
            while (buffer[i]!='0')
            {
                opt[j]=buffer[i];
                i++;
                j++;
            }
            opt[j]='\0';
            
            if (!strcmp( opt, option)) {
                printf("Reception de OACK avec %s accepted\n", option);
                ack[0] = 0;
                ack[1] = 4; // Opcode pour ACK
                ack[2] = 0;
                ack[3] = 0;
                sendto(sockfd, ack, 4, 0, (const struct sockaddr *)&from_addr, sizeof(from_len));
            }
            else{
                buffer[0] = 0;
                buffer[1] = 5; // Opcode pour ERROR
                buffer[2] = 0;
                buffer[3] = 8; // Code d'erreur 8 :Transfer should be terminated
                strcpy((char*)buffer + 4, "Transfer should be terminated\n");
                sendto(sockfd, buffer, 4 + strlen("Transfer should be terminated\n") + 1, 0, (const struct sockaddr *)&from_addr, sizeof(from_addr));
                return;
            }
        } else if (buffer[1] == 3) // Opcode 3 indique un paquet de données
        {
            // Recuperation du numéro de bloc
            unsigned int blocknum = (buffer[2] << 8) | buffer[3];

            // printf("Bloc %d reçu, taille des données: %d octets\n", blocknum, len - 4);

            // Verfiication du numéro de bloc
            if (blocknum != bloc_atuel) {
                printf("Erreur : numéro de bloc incorrect | on redemande le bon bloque au serveur \n");
                ack[0] = 0;
                ack[1] = 4; // Opcode pour ACK
                ack[2] = buffer[2];
                ack[3] = bloc_atuel; // Numéro de bloc
                sendto(sockfd, ack, 4, 0, (const struct sockaddr *)&from_addr, from_len);
                continue;
            }

            // Incrémenter le numéro de bloc
            bloc_atuel++;

            // On reconstruit le fichier  à partir des paquets de données reçus

            if (blocknum == 1) {
                fptr = fopen(filename, "w");
                if (fptr == NULL) {
                    printf("Error! lors de l'ouverture | creation du fichier");
                    exit(1);
                }
            }
            for (int i = 4; i < len; ++i) {
                fprintf(fptr, "%c", buffer[i]);
            }

            // Envoyer un accusé de réception (ACK)
            ack[0] = 0;
            ack[1] = 4; // Opcode pour ACK
            ack[2] = buffer[2];
            ack[3] = buffer[3]; // Numéro de bloc
            sendto(sockfd, ack, 4, 0, (const struct sockaddr *)&from_addr, from_len);

            // Si la taille des données est inférieure à 512, c'est le dernier paquet
            if (len < MAX_PACKET_SIZE) {
                printf("Transmission terminée.\n");
                // On ferme le fichier
                fclose(fptr);
                break;
            }
        } else if (buffer[1] == 5) // Opcode 5 indique un paquet d'erreur
        {
            fprintf(stderr, "Erreur reçue du serveur: %s\n", buffer + 4);
            break;
        }
    }
    // Fermeture du socket
    close(sockfd);
}

void ecrire_fichier_wrq(const char *filename, struct sockaddr_in server_addr, const char *option, const char *optionvalue)
{
    int sockfd;
    unsigned char buffer[MAX_PACKET_SIZE];
    unsigned char data[MAX_PACKET_SIZE];
    int n;
    unsigned short block_actuel = 1;
    int flag = 0;

    struct timeval tv;
    FILE *fptr;
    // Création du socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("Erreur de création du socket");
        exit(EXIT_FAILURE);
    }
    size_t size_filename = strlen(filename);
    int offset = 4 + size_filename + 1 + 5; // 2 octets pour l'opcode, 2 octet pour les 0 apres le filname et final, 5 octets pour le mode

    if (offset > MAX_PACKET_SIZE)
    {
        perror("Erreur : Nom de fichier trop long");
        exit(EXIT_FAILURE);
    }
    buffer[0] = 0;
    buffer[1] = WRQ;
    strcpy((char *)buffer + 2, filename);
    buffer[2 + size_filename] = 0;
    strcpy((char *)buffer + 3 + size_filename, "octet");
    buffer[3 + size_filename + 5] = 0;
    strcpy((char *)buffer + 3 + size_filename + 5, option);
    buffer[3 + size_filename + 5 + strlen(option)] = 0;
    strcpy((char *)buffer+3 + size_filename + 5 + strlen(option),optionvalue);
    buffer[3 + size_filename + 5 + strlen(option) + strlen(optionvalue)] = 0;
    // Envoi du WRQ au serveur
    n = sendto(sockfd, buffer, offset, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
    //printf("Envoi du WRQ au serveur : %d octets\n", n);
    tv.tv_sec = 10;  // Timeout de 5 secondes
    tv.tv_usec = 0; // 0 microsecondes
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0)
    {
        perror("Erreur lors de la définition du timeout");
        exit(EXIT_FAILURE);
    }

    fptr = fopen(filename, "rb");
    memset(buffer, 0, MAX_PACKET_SIZE);

    while (1)
    {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        // Reception des paquet;
        n = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
        //printf("Réception du paquet numero : %d \n", buffer[3]);
        if (buffer[1] == 5)
        {
            printf("Error code %d :%s\n", buffer[3], buffer + 4);
            break;
        }
        else if (buffer[1] == 6)
        {   
          char opt[10];
          int i=2,j=0;
          while (buffer[i]!='0')
          {
            opt[j]=buffer[i];
            i++;
            j++;
          }
          opt[j]='\0';
          
            if (!strcmp(opt, option))
            {
                // bigfile = 1;
                size_t bytesRead = fread(data + 4, 1, 512, fptr);
                if (bytesRead == 0)
                {
                    perror("Erreur lors de la lecture du fichier");
                }
                if (feof(fptr))
                {
                    flag = 1;
                }
                data[0] = 0;
                data[1] = 3;
                data[2] = block_actuel >> 8;
                data[3] = block_actuel;

                n = sendto(sockfd, data, 4 + bytesRead, 0, (const struct sockaddr *)&from_addr, from_len);
                //printf("Envoi du paquet numero : %d \n", data[3]);
                if (n < MAX_PACKET_SIZE)
                {
                    n = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
                    //printf("Réception du paquet numero : %d \n", buffer[3]);
                    printf("Fin de transmission\n");
                    break;
                }
                block_actuel++;
                memset(data + 4, 0, 512);
            }else{
                buffer[0] = 0;
                buffer[1] = 5; // Opcode pour ERROR
                buffer[2] = 0;
                buffer[3] = 8; // Code d'erreur 8 :Transfer should be terminated
                strcpy((char*)buffer + 4, "Transfer should be terminated\n");
                sendto(sockfd, buffer, 4 + strlen("Transfer should be terminated\n") + 1, 0, (const struct sockaddr *)&from_addr, sizeof(from_addr));
                return;
            }
        }
        else if (buffer[1] == 3)
        {
            size_t bytesRead = fread(data + 4, 1, 512, fptr);
            if (bytesRead == 0)
            {
                perror("Erreur lors de la lecture du fichier");
            }
            if (feof(fptr))
            {
                flag = 1;
            }
            data[0] = 0;
            data[1] = 3;
            data[2] = block_actuel >> 8;
            data[3] = block_actuel;

            n = sendto(sockfd, data, 4 + bytesRead, 0, (const struct sockaddr *)&from_addr, from_len);
            //printf("Envoi du paquet numero : %d \n", data[3]);
            if (n < MAX_PACKET_SIZE)
            {
                n = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
                //printf("Réception du paquet numero : %d \n", buffer[3]);
                printf("Fin de transmission\n");
                break;
            }
            block_actuel++;
            memset(data + 4, 0, 512);
            if (block_actuel == 65535)
            {
                printf("on pourra envoyer plus de 32 MO\n");
                fclose(fptr);
                break;
            }
        }
        if (flag)
        {
            fclose(fptr);
            break;
        }
    }
}

int main(int argc, char *argv[])
{

    // Defining variables
    char sep = '\040';
    struct sockaddr_in server_addr;
    char *SERVER_IP;
    if (argc < 2)
    {
        printf("Usage: %s <IP address>\n", argv[0]);
        exit(1);
    }
    SERVER_IP = argv[1];
    // Configuration de l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    // Envoyé en RRQ le nom du fichier pour lire le fichier

    while (1)
    {
        printf("Farid/Fahardine | tftp > ");
        char filename[MAX_SIZE_FILE];
        char commande[10 + MAX_SIZE_FILE];
        char option[10];
        fgets(commande, 10 + MAX_SIZE_FILE, stdin);
        if (commande[3] != ' ')
        {
            printf("?Invalid command\n");
            continue;
        }
        char opcod[4];
        for (int i = 0; i < 3; i++)
        {
            opcod[i] = commande[i];
        }
        opcod[3] = '\0';

        int i = 3;
        while (commande[i] == ' ')
            i++;
        int j = 0;
        while (commande[i] != ' ')
        {
            filename[j] = commande[i];
            j++;
            i++;
        }
        filename[j] = '\0';
        while (commande[i] == ' ')
            i++;
        strcpy(option, commande + i);
        i = 0;
        while (option[i] != '\n')
        {
            i++;
        }
        option[i] = '\0';
        printf("opcode : '%s'\n",opcod);
        printf("filename : '%s'\n",filename);
        printf("option :'%s'\n",option);

        if (strcmp(opcod, "put") == 0)
        {
            // printf("filename : %s\n", filename);
            ecrire_fichier_wrq(filename, server_addr,option," ");
        }
        else if (strcmp(opcod, "get") == 0)
        {
            lire_fichier_rrq(filename, server_addr,option," ");
        }
        else
        {
            printf("?Invalid command\n");
            continue;
        }
    }
    return 0;
}

