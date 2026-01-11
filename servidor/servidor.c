/*******************************************************************************
 *
 * Nombre:
 * 			Servidor de transferencia de archivos TCP.
 *
 * Descripcion:
 *			Envia un archivo solicitado.
 *
 * Compilación:
 *			gcc -DVERBOSE -Wall -Wextra -O2 ../common.c servidor.c -o servidor -lz
 *
 * Sintaxis:
 *		 	./servidor [-i <ip>] [-p <port>] [-t <microseconds>]
 *
 *******************************************************************************/

// librerias
#include <arpa/inet.h>
#include <bits/getopt_core.h>
#include <bits/types/struct_timeval.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/time.h>
#include <zconf.h>
#include <zlib.h>

// archivos
#include "../common.h"

const char *program_name = "servidor";

int main(int argc, char **argv) {
  const char *ip_address;
  uint16_t port;
  int sock;
  struct sockaddr_in server_addr;
  size_t ibuflen, obuflen;
  char *ibuffer, *obuffer;
  struct hdr *ihdr, *ohdr;
  char *ipayload, *opayload;
  // uint8_t seq;
  int i, opt;

  struct sockaddr_in client_addr;
  socklen_t clilen;
  ssize_t nsnd, nrcv;

  program_name = argv[0];
  ip_address = DEFAULT_IP;
  port = DEFAULT_PORT;
  // seq = 0

  // variables de datos
  FILE *archivo;
  int bytes_leidos; // cuantos bytes se leyeron del archivo
  uLong crc;        // suma crc
                    //  cadenas de mensajes
  const char *msg_existente = "archivo_existente";
  const char *msg_error = "archivo_inexistente";
  const char *eos = "EoS";
  // variables para el control
  bool retry = false; // variable para especificar si un paquete se tiene que
                      // reenviar
  struct timeval timer_Start;
  struct timeval cur_time;
  int time_limit=50000;        // cantidad de tiempo a esperar en microsegundos
  uint8_t num_Paq; // numero de paquete
  int flags; // variable para guardar el estado de la funcion antes de hacerla
             // no bloqueante

  while ((opt = getopt(argc, argv, "i:p:t:h")) != -1) {
    switch (opt) {
    case 'i':
      ip_address = optarg;
      break;
    case 'p':
      if (parse_port(optarg, &port) != 0) {
        fprintf(stderr, "Puerto inválido: %s\n", optarg);
        return 1;
      }
      break;
    case 'h':
      usage(stdout);
      return 0;
    case 't':
      time_limit = atoi(optarg);
      break;
    default:
      usage(stderr);
      return 1;
    }
  }

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock == -1) {
    perror("socket");
    return 1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) != 1) {
    fprintf(stderr, "IP inválida: %s\n", ip_address);
    return 1;
  }

  if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
    perror("bind");
    return 1;
  }

  ibuflen = sizeof(struct hdr) + PAYLOAD_SIZE;
  obuflen = sizeof(struct hdr) + PAYLOAD_SIZE +
            sizeof(uLong); // tam. del header + buffer + crc
  ibuffer = calloc(1, ibuflen);
  obuffer = calloc(1, obuflen);
  if (!ibuffer || !obuffer) {
    perror("calloc");
    return 1;
  }

  ihdr = (struct hdr *)ibuffer;
  ipayload = ibuffer + sizeof(struct hdr);
  ohdr = (struct hdr *)obuffer;
  opayload = obuffer + sizeof(struct hdr);

  fprintf(stdout, "Escuchando en %s:%u ...\n", ip_address, port);

  while (1) {
    memset(ibuffer, 0, MAX_MSGLEN);
    memset(&client_addr, 0, sizeof(client_addr));
    clilen = sizeof(struct sockaddr);

    if ((nrcv = recvfrom(sock, ibuffer, MAX_MSGLEN, 0,
                         (struct sockaddr *)&client_addr,
                         (socklen_t *)&clilen)) == -1) {
      perror("recvfrom");
      continue;
    }
    fprintf(stdout,
            KGRN
            "%zd bytes received from %s with source port number %d.\n" RESET,
            nrcv, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

#ifdef VERBOSE
    fprintf(stdout, "--------------------------------------\n");
    fprintf(stdout, KYEL "Header:\n" RESET);
    fprintf(stdout, "Seq num: %u\n", ihdr->nseq);
    fprintf(stdout, "Type: ");
    if (ihdr->type == 0)
      fprintf(stdout, "REQUEST\n");
    else if (ihdr->type == 1)
      fprintf(stdout, "REPLY\n");
    fprintf(stdout, "Len: %u\n", ihdr->len);

    fprintf(stdout, "--------------------------------------\n");
    fprintf(stdout, KYEL "Payload: " RESET);
    fprintf(stdout, "(%s)\n", ipayload);
    for (i = 0; i < ihdr->len; i++)
      fprintf(stdout, "%02X ", ipayload[i]);
    fprintf(stdout, "\n--------------------------------------\n\n");
#endif

    // apertura del archivo.
    ipayload[ihdr->len] = '\0';
    archivo = fopen(ipayload, "rb");
    if (NULL == archivo) {
      /*
       *caso en el que el archivo no existe
       */
      printf(KRED"el archivo solicitado no existe\n"RESET);
      // enviar mensaje de error.
      memcpy(opayload, msg_error, strlen(msg_error));
      ohdr->nseq = ihdr->nseq + 1;
      ohdr->type = REPLY;
      ohdr->len = strlen(msg_error);
      nsnd =
          sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0,
                 (struct sockaddr *)&client_addr, sizeof(struct sockaddr)); //
      if (-1 == nsnd) {
        perror("No se envio la confirmacion al cliente\n");
      }

    } else {
      /*
       *Caso en el que el archivo si existe.
       */
      memcpy(opayload, msg_existente, strlen(msg_existente));
      ohdr->nseq = ihdr->nseq + 1;
      ohdr->type = REPLY;
      ohdr->len = strlen(msg_existente);
      nsnd=sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0,
             (struct sockaddr *)&client_addr, sizeof(struct sockaddr)); //
      if (-1 == nsnd) {
        perror("No se envio la confirmacion al cliente\n");
		break;
      }
      /*
       *Envio de datos
       */
      // guardar estado de la funcion y luego desbloquearla.
      flags = fcntl(sock, F_GETFL, 0);
      fcntl(sock, F_SETFL, flags | O_NONBLOCK);
      do {
        if (retry == false) {
          	// se lee un pedazo del archivo solo si no se indica enviar el que se
          	// leyo antes
        	bytes_leidos = fread(opayload, 1, PAYLOAD_SIZE, archivo);
        }
		if (bytes_leidos>0){//solo se envia el paquete si se leyeron datos.
			//primero preparo el paquete
			ohdr->nseq=num_Paq;
			ohdr->type=DATA;
			ohdr->len=bytes_leidos;
			//luego calculo la suma de verificacion
			crc=crc32(0L, (const Bytef *)obuffer, sizeof(struct hdr)+ohdr->len);
			memcpy(opayload+bytes_leidos, &crc, sizeof(crc));
			nsnd = sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len + sizeof(crc), 0,(struct sockaddr *)&client_addr, sizeof(struct sockaddr));//
			if (nsnd==-1){
				perror("No se enviaron datos al cliente");
				continue;
			}
			#ifdef VERBOSE
			printf(KGRN"se enviaron %d bytes a %s con puerto %d\n\tpaquete:%d con crc:%lx\n"RESET, bytes_leidos, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),num_Paq, crc);
			#endif
			//timer para esperar la respuesta del cliente;
			gettimeofday(&timer_Start, NULL);
			do{
				nrcv =recvfrom(sock, ibuffer, MAX_MSGLEN, 0,(struct sockaddr *)&client_addr, (socklen_t *)&clilen);
				gettimeofday(&cur_time, NULL);
				if(((cur_time.tv_sec-timer_Start.tv_sec)*1000000L+(cur_time.tv_usec-timer_Start.tv_usec))>=time_limit){
					retry=true;
					#ifdef VERBOSE
					printf(KRED"No se recibio respuesta del cliente.\n"RESET);
					#endif
					break;
				}
			}while(nrcv==-1);
			//verificar que se haya pedido el siguiente paquete
			num_Paq++;
			if (ihdr->nseq!=num_Paq){
				retry=true;
				#ifdef VERBOSE
					printf(KRED"El cliente y el servidor se desincronizaron.\nServidor:%d  cliente:%d.\n"RESET, num_Paq, ihdr->nseq);
				#endif
			}
			//no hay que avanzar al siguiente paquete si se tiene que volver a enviar el actual
			if (retry){
				num_Paq--;
			}
		}
      } while (0 < bytes_leidos);
	  //restaurar el estado de la funcion bloqueante
	  fcntl(sock, F_SETFL, flags);

	  //enviar fin de transmision
	  memcpy(opayload, eos, strlen(eos));
	  ohdr->nseq=ihdr->nseq+1;
	  ohdr->type=END;
	  ohdr->len=strlen(eos);
	  crc = crc32(0L, (Bytef *)obuffer, sizeof(struct hdr) + ohdr->len);
	  memcpy(opayload + ohdr->len, &crc, sizeof((crc)));
	  nsnd =sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len + sizeof(crc), 0,(struct sockaddr *)&client_addr, sizeof(struct sockaddr));
	  if (nsnd == -1) {
        perror("No se envio el fin de archivo");
      }
	  //confirmacion del cliente
	  nrcv = recvfrom(sock, ibuffer, ibuflen, 0, NULL, NULL);
      if (nrcv > 0 && ihdr->type == END) {
        printf("Respuesta del cliente: %s\n", ipayload);
      }

	  fclose(archivo);
	  num_Paq=0;
	  retry=false;
	  memset(opayload, 0, PAYLOAD_SIZE);
    }

#ifdef VERBOSE
    fprintf(stdout, "--------------------------------------\n");
    fprintf(stdout, KYEL "Header:\n" RESET);
    fprintf(stdout, "Seq num: %u\n", ohdr->nseq);
    fprintf(stdout, "Type: ");
    if (ohdr->type == 0)
      fprintf(stdout, "REQUEST\n");
    else if (ohdr->type == 1)
      fprintf(stdout, "REPLY\n");
    fprintf(stdout, "Len: %u\n", ohdr->len);

    fprintf(stdout, "--------------------------------------\n");
    fprintf(stdout, KYEL "Payload: " RESET);
    fprintf(stdout, "(%s)\n", opayload);
    for (i = 0; i < ohdr->len; i++)
      fprintf(stdout, "%02X ", opayload[i]);
    fprintf(stdout, "\n--------------------------------------\n\n");
#endif
  }

  free(ibuffer);
  free(obuffer);

  close(sock);

  return 0;
}
