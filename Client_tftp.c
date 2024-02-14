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
  struct timeval tv;
  FILE *fptr;
  // Création du socket UDP
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    perror("Erreur de création du socket");
    exit(EXIT_FAILURE);
  }

  // Construction du paquet RRQ
  // Format : | 00 | filename | 0 | mode | 0 |
  size_t size_filename = strlen(filename);
  int offset = 4 + size_filename + 1 + 5; // 2 octets pour l'opcode, 2 octet pour les 0 apres le filname et final, 5 octets pour le mode

  if (offset > MAX_PACKET_SIZE)
  {
    perror("Erreur : Nom de fichier trop long");
    exit(EXIT_FAILURE);
  }

  /* Version plus performant*/
  buffer[0] = 0;
  buffer[1] = RRQ;
  strcpy(buffer + 2, filename);
  buffer[2 + size_filename] = 0;
  strcpy(buffer + 3 + size_filename, "octet");
  buffer[3 + size_filename + 5] = 0;

  // Envoi du RRQ au serveur
  sendto(sockfd, buffer, MAX_PACKET_SIZE, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));

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
  int bloc_atuel = 1;
  char ack[4];
  ack[0] = 0;
  ack[1] = 4; // Opcode pour ACK
  ack[2] = 0;
  ack[3] = bloc_atuel; // Numéro de bloc

  while (1)
  {
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    char buffer_recv[MAX_PACKET_SIZE];
    // Réception d'un paquet
    len = recvfrom(sockfd, buffer_recv, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
    if (len < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        // Timeout : aucun paquet reçu dans l'intervalle de temps spécifié
        printf("Timeout : aucun paquet reçu. | On renvoie la requette au serveur\n");
        sendto(sockfd, buffer, MAX_PACKET_SIZE, 0, (const struct sockaddr *)&from_addr, sizeof(from_len));
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
    if (buffer[1] == 3) // Opcode 3 indique un paquet de données
    {
      // Recuperation du numéro de bloc
      int blocknum = (buffer[2] << 8) | buffer[3];

      // printf("Bloc %d reçu, taille des données: %d octets\n", blocknum, len - 4);

      // Verfiication du numéro de bloc
      if (blocknum != bloc_atuel)
      {
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

      if (blocknum == 1)
      {
        fptr = fopen(filename, "w");
        if (fptr == NULL)
        {
          printf("Error! lors de l'ouverture | creation du fichier");
          exit(1);
        }
      }
      for (int i = 4; i < len; ++i)
      {
        fprintf(fptr, "%c", buffer[i]);
      }

      // Envoyer un accusé de réception (ACK)
      ack[0] = 0;
      ack[1] = 4; // Opcode pour ACK
      ack[2] = buffer[2];
      ack[3] = buffer[3]; // Numéro de bloc
      sendto(sockfd, ack, 4, 0, (const struct sockaddr *)&from_addr, from_len);

      // Si la taille des données est inférieure à 512, c'est le dernier paquet
      if (len < MAX_PACKET_SIZE)
      {
        printf("Transmission terminée.\n");
        // On ferme le fichier
        fclose(fptr);
        break;
      }
    }
    else if (buffer[1] == 5) // Opcode 5 indique un paquet d'erreur
    {
      fprintf(stderr, "Erreur reçue du serveur: %s\n", buffer + 4);
      break;
    }
  }

  // Fermeture du socket
  close(sockfd);
}
void ecrire_fichier_wrq(const char *filename, struct sockaddr_in server_addr){
  int sockfd;
  char buffer[MAX_PACKET_SIZE];
  int len, n,block_actuel=1;

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
  strcpy(buffer + 2, filename);
  buffer[2 + size_filename] = 0;
  strcpy(buffer + 3 + size_filename, "octet");
  buffer[3 + size_filename + 5] = 0;

  // Envoi du RRQ au serveur
  n = sendto(sockfd, buffer, offset, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));

  tv.tv_sec = 5;  // Timeout de 5 secondes
  tv.tv_usec = 0; // 0 microsecondes
   if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0)
  {
    perror("Erreur lors de la définition du timeout");
    exit(EXIT_FAILURE);
  }
  char data[516];
  char ack[4];
  fptr = fopen(filename,"r");
  
  int flag=0;
  while (1)
  {

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    n=recvfrom(sockfd, ack, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
        printf("ack1 %d ,ack2 %d ack3 %d ack4 %d\n",ack[0],ack[1],ack[2],ack[3]);
    if(n==0 || ack[1]!=4){
      perror("Errorrrr ");
      exit(1);
    }
    if(flag){
      fclose(fptr);
      break;
    }
    size_t bytesRead = fread(data+4,1,512,fptr);
    if (bytesRead == 0) {
        perror("Erreur lors de la lecture du fichier");
        // fclose(fptr);
        // flag=1;
        //exit(1);
    }
    if(feof(fptr)){
      flag=1;
    }

    data[0] = 0;
    data[1] = 3;
    if(block_actuel==9){
      block_actuel=0;
      data[2]++;
    }else{
      data[2]=0;
    }
    data[3] = block_actuel;
    printf("****************************data[0 ]= %d data[1]= %d data[2] = %d data[3]=%d\n",data[0],data[1],data[2],data[3]);
    printf("darta = %s\n",data+4);
    n = sendto(sockfd, data, 4+bytesRead, 0, (const struct sockaddr *)&from_addr, from_len);

      block_actuel++;

    memset(data+4, 0, 512);

  }
  
  if(fptr==NULL){
    perror("FILE NOT FOUND");
    return;
  }

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