/* 
 * server.c - An updated UDP server 
 * usage: udpserver <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <netdb.h>
#include <dirent.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024
#define DATASIZE 1020
#define MAX_ID 4294967295

#define SetBit(A,k) ( A[((k)/32)] |= (1 << ((k)%32)) )
#define TestBit(A,k) ( A[ ((k)/32)] & (1 << ((k)%32)) )

// total size BUFSIZE
struct packet{
  uint32_t id;
  char data[DATASIZE];
};

/*
 * error - wrapper for perror
 */
void error(char *msg) {
  perror(msg);
  exit(1);
}

FILE *open_file(char *filename){
  FILE *fp;
  if (filename == NULL){
    return NULL;
  }
  fp = fopen(filename, "rb");
  return(fp);
}

// Copy file names in to the buffer for sending
void ls(char *buf){
  DIR *folder;
  struct dirent *file;
  int fileloc;
  int namelen;

  fileloc = 0;
  folder = opendir("files");
  while((file = readdir(folder)) != NULL){
    if(*file->d_name != '.'){ // Ignore hidden files and the '.', '..' directories
      namelen = strlen(file->d_name);
      if(fileloc + namelen >= DATASIZE) // If there are more than 1020 characters of files to print, just truncate
        return;
      sprintf(&buf[fileloc], "%s\n", file->d_name);
      fileloc += namelen + 1;
    }
  }
}

// Waits for socket to be filled before receiving packet
int recv_when_available(int sockfd, struct packet *recvbuf, struct sockaddr *from, socklen_t *fromlen){
  struct timeval tv;
  fd_set readfds;
  int n_read;
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  FD_ZERO(&readfds);
  FD_SET(sockfd, &readfds);
  select(sockfd+1, &readfds, NULL, NULL, &tv);
  if(FD_ISSET(sockfd, &readfds)){
    n_read = recvfrom(sockfd, recvbuf, BUFSIZE, 0, from, fromlen);
    if (n_read < 0){
      printf("Receiving socket empty\n");
    }
    return n_read;
  } else
    printf("Timed out waiting for client reply\n");
  return -1;
}

// Waits for socket to be available before sending packet
int send_when_available(int sockfd, struct packet *data, int nbytes, const struct sockaddr *addr, socklen_t addrlen){
  // struct timeval tv;
  fd_set writefds;
  int n_sent;
  // tv.tv_sec = 2;
  // tv.tv_usec = 5000000;
  FD_ZERO(&writefds);
  FD_SET(sockfd, &writefds);
  select(sockfd+1, NULL, &writefds, NULL, NULL);
  if(FD_ISSET(sockfd, &writefds)){
    n_sent = sendto(sockfd, data, nbytes, 0, addr, addrlen);
    if (n_sent < 0) 
      error("ERROR in sendto");
    return n_sent;
  } else
    printf("Timed out waiting to send packet\n");
  return -1;
}


void send_file(char *filename, int sockfd, struct sockaddr *addr, socklen_t addrlen){
  int n_read;
  int n_sent;
  struct packet filebuf;
  struct packet recvbuf;
  char *eof = "!!END_FILE!!";
  struct packet header;
  uint32_t file_bytes;
  uint32_t n_packets;
  uint32_t packet_id;
  uint32_t n_missing;
  uint32_t *missing;
  struct sockaddr_in from;
  socklen_t fromlen = sizeof(struct sockaddr_storage);
  FILE *fp;

  fp = open_file(filename);
  if (fp == NULL){
    return;
  }

  // find file size
  fseek(fp, 0, SEEK_END);
  file_bytes = (uint32_t)ftell(fp);
  printf("Found %i bytes in file\n", file_bytes);
  fseek(fp, 0, SEEK_SET);
  n_packets = (file_bytes + DATASIZE - 1)/DATASIZE;

  // Send header
  sprintf(header.data, "!!HEADER_INFO!!%i", n_packets);
  header.id = n_packets;
  n_sent = sendto(sockfd, &header, 23, 0, addr, addrlen);
  if (n_sent < 0) 
    error("ERROR in sendto");
  printf("HEADER sent: %u packets\n", n_packets);

  // First pass: Send all packets in order
  bzero(&filebuf, BUFSIZE);
  packet_id = 0;
  n_read = fread(&filebuf.data[0], 1, DATASIZE, fp); // get bytes from fp into filebuf
  while(n_read > 0) {
    filebuf.id = packet_id;
    n_sent = send_when_available(sockfd, &filebuf, n_read+4, addr, addrlen);
    //n_sent = sendto(sockfd, &filebuf, n_read+4, 0, servinfo->ai_addr, servinfo->ai_addrlen);
    if (n_sent < 0) 
      error("ERROR in sendto");
    printf(". %i .", packet_id);
    packet_id++;
    bzero(&filebuf, BUFSIZE);
    n_read = fread(&filebuf.data[0], 1, DATASIZE, fp);
  }

  while(1){
    // Send EOF
    bzero(&filebuf, BUFSIZE);
    filebuf.id = MAX_ID;
    strncpy(filebuf.data, eof, strlen(eof));
    n_sent = send_when_available(sockfd, &filebuf, 16, addr, addrlen);
    if (n_sent < 0) 
      error("ERROR in sendto");
    printf("EOF sent\n");

    // Check for messages from server
    bzero(&recvbuf, BUFSIZE);
    n_read = recv_when_available(sockfd, &recvbuf, (struct sockaddr *)&from, &fromlen);
    if(n_read < 0)
      continue;
    printf("Msg from client: %i\n", recvbuf.id);
    if(recvbuf.id == 0)
      printf("Completion of file %s\n", recvbuf.data);

    // Send success
    if((recvbuf.id == 0) && (strncmp(filename, &recvbuf.data[0], strlen(filename)) == 0)){ // Success
      printf("GET file %s success!\n", filename);
      return;
    } else { // Or, some packets are missing
      n_missing = recvbuf.id;
      missing = (uint32_t *)&recvbuf.data[0];
      int i = 0;
      while(i < n_missing){ // Send up to 255 requested packets
        bzero(&filebuf, BUFSIZE);
        // Read from specific place in file and send that packet
        packet_id = missing[i];
        if (fseek(fp, packet_id*DATASIZE, SEEK_SET) <0)
          error("ERROR in fseek");
        n_read = fread(&filebuf.data[0], 1, DATASIZE, fp);
        filebuf.id = packet_id;
        n_sent = send_when_available(sockfd, &filebuf, n_read+4, addr, addrlen);
        printf(". %i .", packet_id);
        i++;
      }
    }
  } 
}

int receive_file(char *fname, int sockfd, struct sockaddr_in *clientaddr, socklen_t *clientlen){
  struct packet filebuf;
  FILE *fp;
  int n_read;
  int n_sent;
  int n;
  uint32_t packet_id;
  uint32_t npackets; // Can count to 4,294,967,295
  uint32_t n_missing;
  uint32_t *missing;
  char debug_buffer[20];

  // create file
  fp = fopen(fname, "w+b");
  if (fp == NULL){
    printf("Error opening file %s for writing\n", fname);
    return -1;
  }

  // receive header
  // TODO: What if header is dropped?
  while(1){
    bzero(&filebuf, BUFSIZE);
    n = recvfrom(sockfd, &filebuf, BUFSIZE, 0, (struct sockaddr *)clientaddr, clientlen);
    if (n < 0)
      error("ERROR in recvfrom");
    //printf("Receive_file: %i byte header received\n", n);
    if(strncmp(filebuf.data, "!!HEADER_INFO!!", 15) == 0){
      npackets = filebuf.id;
      printf("Received header: %u packets expected\n", npackets);
      break;
    }
  }

  uint32_t recvmap_len = (npackets+32-1)/32; // 32 bits per int; round up
  uint32_t recvmap[recvmap_len]; // array of bits to track which packets arrived
  memset(recvmap, 0, recvmap_len * sizeof(uint32_t));
  uint32_t all_ones[recvmap_len];
  memset(all_ones, ~0, recvmap_len * sizeof(uint32_t));

  // Set extra bits in recvmap to 1
  // Don't do this if there were no extra bits
  if(recvmap_len != npackets/32){
    int extra = 32-(npackets%32);
    while(extra > 0){
      SetBit(recvmap, (recvmap_len*32)-extra);
      extra--;
    }
  }

  // Until we receive "EOF" signal and file is complete:
  while(1){
    bzero(&filebuf, BUFSIZE);
    n_read = recvfrom(sockfd, &filebuf, BUFSIZE, 0, (struct sockaddr *)clientaddr, clientlen);
    if (n_read < 0)
      error("ERROR in recvfrom");
    packet_id = filebuf.id;
    printf(". %u .", packet_id);

    // EOF received
    if(packet_id == MAX_ID){
      printf("EOF detected\n");

      // Check if file is complete
      if(memcmp(recvmap, all_ones, recvmap_len*4) == 0){
        printf("File complete\n");

        // Send confirmation packet
        bzero(&filebuf, BUFSIZE);
        strncpy(filebuf.data, fname, strlen(fname));
        n_sent = sendto(sockfd, &filebuf, BUFSIZE, 0, (struct sockaddr *)clientaddr, *clientlen);
        if (n_sent < 0) 
          error("ERROR in sendto");
        printf("Server sent completion acknowledgment\n");

        //Close and exit
        fclose(fp);
        return 1;
      }

      // Prepare missing packets list
      n_missing = 0;
      missing = (uint32_t *)&filebuf.data[0];
      // Loop through bit array
      packet_id = 0;
      while((packet_id < npackets) && (n_missing < 255)){ // Cap length of missing packets list)
        // Make an array of missing packet IDs
        if(!TestBit(recvmap, packet_id)){
          missing[n_missing] = packet_id;
          n_missing++;
        }
        packet_id++;
      }
      // Send list
      filebuf.id = n_missing;
      n_sent = sendto(sockfd, &filebuf, BUFSIZE, 0, (struct sockaddr *)clientaddr, *clientlen);
      if (n_sent < 0) 
        error("ERROR in sendto");
      printf("Server sent missing packet info\n");
      continue;
    }

    // Mark received and write to file
    SetBit(recvmap, packet_id);

    //printf("Writing %.20s...\n", filebuf.data);
    //printf("Seeking to position %u\n", packet_id * DATASIZE);
    if ((n = fseek(fp, packet_id*DATASIZE, SEEK_SET)) < 0)
      perror("Invalid seek");
    n_sent = fwrite(&filebuf.data[0], 1, n_read-4, fp);
    //printf("Wrote %i bytes at loc %u\n", n_read-4, packet_id*DATASIZE);
  }

  return 0;
}

int main(int argc, char **argv) {
  int sockfd; /* socket */
  int portno; /* port to listen on */
  char *port;
  socklen_t clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  struct packet buf; /* message buf */
  char *filename; /* filename pointer */
  char hostaddrp[INET_ADDRSTRLEN];
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */
  char fnamebuf[128];

  /* 
   * check command line arguments 
   */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  port = argv[1];
  portno = atoi(argv[1]);

  // BEEJ p. 22
  int status;
  struct addrinfo hints;
  struct addrinfo *servinfo;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  if ((status = getaddrinfo(NULL, port, &hints, &servinfo)) != 0){
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    exit(1);
  }

  /* 
   * socket: create the parent socket 
   */
  sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
  if (sockfd < 0) 
    error("ERROR opening socket");

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
	    &optval, sizeof(optval));

  /* 
   * bind: associate the parent socket with a port 
   */
  if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) 
    error("ERROR on binding");

  /* 
   * main loop: wait for a datagram, then echo it
   */
  clientlen = sizeof(clientaddr);
  while (1) {

    /*
     * recvfrom: receive a UDP datagram from a client
     */
    bzero(&buf, BUFSIZE);
    n = recvfrom(sockfd, &buf, BUFSIZE, 0, (struct sockaddr *)&clientaddr, &clientlen);
    if (n < 0)
      error("ERROR in recvfrom");

    /* 
     * getnameinfo: determine who sent the datagram
     */
    char host[1024];
    char service[20];
    if(getnameinfo((const struct sockaddr *)&clientaddr, clientlen, host, sizeof(host), 
      service, sizeof(service), 0) != 0)
        error("ERROR in getnameinfo");
    
    bzero(fnamebuf, 128);
    if (buf.id != MAX_ID-1){ // Command packets must have this ID; ignore packets leftover from other commands
      continue;
    } else if(strncmp(buf.data, "get", 3) == 0){
      filename = &buf.data[4];
      printf("Get file %s\n", filename);
      sprintf(fnamebuf, "files/%s", filename);
      send_file(&fnamebuf[0], sockfd, (struct sockaddr *)&clientaddr, clientlen);
    } else if (strncmp(buf.data, "put", 3) == 0){
      filename = &buf.data[4];
      printf("Put file %s\n", filename);
      sprintf(fnamebuf, "files/%s", filename);
      receive_file(&fnamebuf[0], sockfd, &clientaddr, &clientlen);
    } else if (strncmp(buf.data, "delete", 6) == 0){
      filename = &buf.data[7];
      printf("Delete file %s\n", filename);
      sprintf(fnamebuf, "files/%s", filename);
      remove(fnamebuf);
    } else if (strncmp(buf.data, "ls", 2) == 0){
      printf("List files\n");
      bzero(&buf, BUFSIZE);
      ls(&buf.data[0]);
      //printf("%s", buf.data);
      send_when_available(sockfd, &buf, BUFSIZE, (struct sockaddr *)&clientaddr, clientlen);
    } else if (strncmp(buf.data, "exit", 4) == 0){
      printf("Exit\n");
      freeaddrinfo(servinfo);
      exit(0);
    } else {
      printf("Not understood: %s\n", buf.data);
    }

  }
}
