/*******************************************************************************
 *
 * Nombre:
 * 			Servidor de echo UDP.
 *
 * Descripcion:
 *			Recibe una cadena desde un cliente y le reenvía la cadena invertida.
 *
 * Compilación:
 *			gcc -DVERBOSE -Wall -Wextra -O2 ../common.c servidor.c -o servidor -lz
 *
 * Sintaxis:
 *		 	./servidor [-i <ip>] [-p <port>]
 *
 *******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <time.h>
#include <zlib.h>

#include "../common.h"

const char *program_name = "servidor";

int main(int argc, char **argv)
{
	// variables utilizadas
	const char *ip_address;
	uint16_t port;
	int sock;
	struct sockaddr_in server_addr;
	size_t ibuflen, obuflen;
	char *ibuffer, *obuffer;
	struct hdr *ihdr, *ohdr;
	char *ipayload, *opayload;
	// uint8_t seq;
	int opt;
	FILE *archivo;
	int bytes_leidos=0;

	//-------------
	const char *msg_existente = "archivo_existente";
	const char *msg_error = "archivo_inexistente";
	const char *eos = "EoS";
	int next_packet = 1;
	bool retry=false;
	long int timerstart;
	int timelimitsec=1;
	int flags;
	uLong *crc;

	struct sockaddr_in client_addr;
	socklen_t clilen;
	ssize_t nsnd, nrcv;

	program_name = argv[0];
	ip_address = DEFAULT_IP;
	port = DEFAULT_PORT;
	// seq = 0

	// opciones de liena de comandos
	while ((opt = getopt(argc, argv, "i:p:h:t:")) != -1)
	{
		switch (opt)
		{
		case 'i':
			ip_address = optarg;
			break;
		case 'p':
			if (parse_port(optarg, &port) != 0)
			{
				fprintf(stderr, "Puerto inválido: %s\n", optarg);
				return 1;
			}
			break;
		case 'h':
			usage(stdout);
			return 0;
		case 't':
			timelimitsec=atoi(optarg);
			//printf("%d, %d", timelimitsec, atoi(optarg));
			break;
		default:
			usage(stderr);
			return 1;
		}
	}

	// creacion del socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
	{
		perror("socket");
		return 1;
	}
	flags=fcntl(sock, F_GETFL, 0);
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) != 1)
	{
		fprintf(stderr, "IP inválida: %s\n", ip_address);
		return 1;
	}

	if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
	{
		perror("bind");
		return 1;
	}

		//aqui se crea el buffer de entrada y salida
	ibuflen = sizeof(struct hdr) + PAYLOAD_SIZE;
	obuflen = sizeof(struct hdr) + PAYLOAD_SIZE+sizeof(uLong);//header +payload+crc
	ibuffer = calloc(1, ibuflen);
	obuffer = calloc(1, obuflen);
	if (!ibuffer || !obuffer)
	{
		perror("calloc");
		return 1;
	}

	ihdr = (struct hdr *)ibuffer;
	ipayload = ibuffer + sizeof(struct hdr);
	ohdr = (struct hdr *)obuffer;
	opayload = obuffer + sizeof(struct hdr);
	crc=(uLong*)(obuffer+sizeof(struct hdr) + PAYLOAD_SIZE);

	fprintf(stdout, "Escuchando en %s:%u ...\n",
			ip_address, port);

	while (1)
	{
		memset(ibuffer, 0, ibuflen); // Limpiar el buffer de entrada
		memset(obuffer, 0, obuflen); // Y el de salida
		memset(&client_addr, 0, sizeof(client_addr));
		clilen = sizeof(struct sockaddr);

		// recibir datos
		if ((nrcv = recvfrom(sock, ibuffer, MAX_MSGLEN, 0, (struct sockaddr *)&client_addr, (socklen_t *)&clilen)) == -1)
		{
			perror("recvfrom");
			continue;
		}
		fprintf(stdout, KGRN "%zd bytes received from %s with source port number %d.\n" RESET, nrcv, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

// depuracion
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
		for (int i = 0; i < ihdr->len; i++)
			fprintf(stdout, "%02X ", ipayload[i]);
		fprintf(stdout, "\n--------------------------------------\n\n");
#endif

		// cadena recibida: este es el nombre del archivo
		ipayload[ihdr->len] = '\0';
		archivo = fopen(ipayload, "rb"); // abre el archivo en modo lectura binaria
		if (NULL == archivo)			 // si el archivo no existe:
		{
			printf("El archivo origen no existe\n");
			memcpy(opayload, msg_error, strlen(msg_error));
			ohdr->nseq = ihdr->nseq + 1;
			ohdr->type = REPLY;
			ohdr->len = strlen(msg_error);
			nsnd = sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
			if (nsnd == -1)
			{
				perror("confirmacion archivo inexistente");
				continue;
			}
			// return 3;
		}
		else
		{
			// enviar confirmacion

			memcpy(opayload, msg_existente, strlen(msg_existente));
			ohdr->nseq = ihdr->nseq + 1;
			ohdr->type = REPLY;
			ohdr->len = strlen(msg_existente);
			nsnd = sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
			if (nsnd == -1)
			{
				perror("confirmacion de archivo existente");
			}
			#ifdef VERBOSE
				printf("se confirmo que el archivo existe");
			#endif
			// enviar datos
			do
			{
				if(retry== false){//si no hay que reintentar lee datos
					memset(opayload, 0, PAYLOAD_SIZE); // Solo limpiar el payload
					bytes_leidos = fread(opayload, 1, PAYLOAD_SIZE, archivo);
					memset(crc, 0,sizeof(uLong));
					memset(crc,crc32(*crc,(const Bytef*)opayload,PAYLOAD_SIZE), sizeof(uLong));

				}
				if (0 < bytes_leidos)
				{ // si se leyeron mas de 0b se envia

					ohdr->nseq = next_packet-1;//el siguiente paquete menos 1 = paquete actual
					ohdr->type = DATA;
					ohdr->len = bytes_leidos;
					nsnd = sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
					if (nsnd == -1)
					{
						perror("envio de datos");
					}
					fprintf(stdout, KGRN "%d bytes sent to %s with source port number %d in packet:%d.\n" RESET, bytes_leidos, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), next_packet - 1); // imprime el paquete que se envio empezadno en 1

					memset(ibuffer, 0, ibuflen);//limpiar el buffer por si acaso
					retry=false;//por defecto no se debe reintentar.
					fcntl(sock, F_SETFL, flags | O_NONBLOCK);//desbloquear recvfrom.
					timerstart=time(NULL);//aca inicia el timer
					printf("esperando peticion, siguiente paquete\n");
					do // este ciclo se encarga de recibir la peticion del sigiuiente chunk
					{	
						nrcv = recvfrom(sock, ibuffer, MAX_MSGLEN, 0, (struct sockaddr *)&client_addr, (socklen_t *)&clilen);
						if((time(NULL)-timerstart)>=timelimitsec){//si se alcanzo el tiempo limite
							retry=true;	//retry
							printf("peticion no recibida\n");
							break;
						}
						//fprintf(stdout, KBLU "received confirmation %s  sent by %s with source port number %d.\n" RESET, ibuffer, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
					} while (((atoi(ipayload) != next_packet)&&nrcv==-1));
					if(retry==false){//si no hay que reintentar siguiente paquete
						next_packet++;
					}
					fcntl(sock, F_SETFL, flags);
				}
			} while (!(bytes_leidos < PAYLOAD_SIZE));

			// Enviar fin de transmisión
			next_packet = 1;
			memset(opayload, 0, PAYLOAD_SIZE); // Solo limpiar el payload
			memcpy(opayload, eos, strlen(eos));
			ohdr->nseq = ihdr->nseq + 1;
			ohdr->type = END;
			ohdr->len = strlen(eos);
			nsnd = sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
			if (nsnd == -1)
			{
				perror("End Of Stream");
			}
			#ifdef VERBOSE
				printf("End of stream");
			#endif

			// Confirmacion final del cliente
			printf("Esperando la confirmación final del cliente...\n");
			nrcv = recvfrom(sock, ibuffer, ibuflen, 0, NULL, NULL);
			if (nrcv > 0 && ihdr->type == ACK)
			{
				printf("Respuesta del cliente: %s\n", ipayload);
			}

			fclose(archivo);
			#ifdef VERBOSE
				printf("se cerro el archivo");
			#endif
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
		for (int i = 0; i < ohdr->len; i++)
			fprintf(stdout, "%02X ", opayload[i]);
		fprintf(stdout, "\n--------------------------------------\n\n");
#endif
		printf("Esperando peticion...");
	}

	free(ibuffer);
	free(obuffer);

	close(sock);

	return 0;
}