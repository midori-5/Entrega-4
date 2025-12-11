/***************************
 *
 * Nombre:
 * Cliente de transferencia de archivos UDP.
 *
 * Descripcion:
 * Envía el nombre de un archivo al servidor y recibe su contenido.
 *
 * Compilación:
 * gcc -DVERBOSE -Wall -Wextra -O2 ../common.c cliente.c -o cliente -lz
 *
 * Sintaxis:
 * ./cliente [-i <ip>] [-p <port>]
 *
 ******************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <zconf.h>
#include <zlib.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../common.h"

const char *program_name = "cliente";

int main(int argc, char **argv)
{
    const char *ip_address;
    uint16_t port;
    int sock;
    struct sockaddr_in server_addr;
    size_t ibuflen, obuflen;
    char *ibuffer, *obuffer;
    struct hdr *ihdr, *ohdr;
    char *ipayload, *opayload;
    uint8_t seq;
    int opt;

    size_t len;
    ssize_t nsnd, nrcv;
    struct sockaddr_in client_addr;
    socklen_t clilen;
    uLong crc_local;
    uLong crc_remoto;

    // Variables para guardar el nombre y contar ACKs
    char requested_filename[PAYLOAD_SIZE];
    uint32_t next_ack_to_send = 1;
    char ack_payload_str[12]; // Buffer para 1,2,3...

    program_name = argv[0];
    ip_address = DEFAULT_IP;
    port = DEFAULT_PORT;
    seq = 0;

    while ((opt = getopt(argc, argv, "i:p:h")) != -1)
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
        default:
            usage(stderr);
            return 1;
        }
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) != 1)
    {
        fprintf(stderr, "IP inválida: %s\n", ip_address);
        return 1;
    }

    obuflen = sizeof(struct hdr) + PAYLOAD_SIZE;
    // se agrego espacio al final para el crc32
    ibuflen = sizeof(struct hdr) + PAYLOAD_SIZE + sizeof(uLong);
    obuffer = calloc(1, obuflen);
    ibuffer = calloc(1, ibuflen);
    if (!obuffer || !ibuffer)
    {
        perror("calloc");
        return 1;
    }

    ohdr = (struct hdr *)obuffer;
    opayload = obuffer + sizeof(struct hdr);
    ihdr = (struct hdr *)ibuffer;
    ipayload = ibuffer + sizeof(struct hdr);
    //crc_remoto=(uLong *)(ibuffer+sizeof(struct hdr) + PAYLOAD_SIZE); 

    while (1)
        {
            seq = 0;
            printf("Nombre del archivo a solicitar (\"salir\" para terminar): ");
            fflush(stdout);

            if (!fgets(opayload, PAYLOAD_SIZE, stdin))
                break;
            len = strcspn(opayload, "\r\n");
            opayload[len] = '\0';

            // Guardar el nombre del archivo
            strncpy(requested_filename, opayload, PAYLOAD_SIZE);

            if (strcmp(opayload, "salir") == 0)
                break;

            // Debo enviar el nombre del archivo
            ohdr->nseq = seq++;
            ohdr->type = REQUEST;
            ohdr->len = (uint16_t)len;

            nsnd = sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0,
                        (struct sockaddr *)&server_addr, sizeof(server_addr));
            if (nsnd == -1)
            {
                perror("sendto");
                break;
            }

            printf("Solicitud enviada del archivo: %s\n", requested_filename);

            // Espero por la confirmacion del servidor
            memset(ibuffer, 0, ibuflen);
            nrcv = recvfrom(sock, ibuffer, ibuflen, 0, NULL, NULL);
            if (nrcv <= 0)
            {
                perror("recvfrom: no se recibió confirmación del servidor");
                continue; // reintenta
            }

            // proceso el paquete recibido
            if (strstr(ipayload, "archivo_inexistente") != NULL) 
            {
                printf("Error del servidor. El archivo no existe o no se puede leer.\n\n");
                continue; 
            }

            if (ihdr->type != REPLY)
            {
                fprintf(stderr, "Ha ocurrido una respuesta inesperada \n");
                continue;
            }

            printf("Confirmacion recibida\n");

            // Abro archivo local en modo escritura binaria
            FILE *fp = fopen(requested_filename, "wb");
            if (!fp)
            {
                perror("fopen");
                break;
            }

            // Establecer el siguiente paquete que esperamos
            next_ack_to_send = 1; 
            
            memset(opayload, 0, PAYLOAD_SIZE);
            // ciclo para recibir archivo en bloques de datos hasta fin de cadena
            while(ihdr->type!=END)
            {
                memset(ibuffer, 0, ibuflen);
                nrcv = recvfrom(sock, ibuffer, ibuflen, 0, NULL, NULL);
                if (nrcv <= 0)
                {   
                    perror("recvfrom: se perdió la conexion durante la transferencia");
                    break;
                }

                    // Calcular CRC sobre los bytes recibidos
                    crc_local = crc32(0L, (const Bytef*)ipayload, ihdr->len); // solo calcular sobre los bytes recibidos realmente

                    //OBTENER CRC REMOTO
                    memcpy(&crc_remoto, ibuffer + sizeof(struct hdr) + ihdr->len, sizeof(uLong));
                if (crc_local!=crc_remoto){                        
                    fflush(stderr);
                    fprintf(stderr,KRED"***Error. archivo corrupto en paquete %u\n" KNRM, ihdr->nseq);
                    fprintf(stderr,"crc calculado=%lx crc recibido=%lx \n" KNRM, crc_local, crc_remoto);
                    continue; 
                }
                

                else if (ihdr->type == DATA) // DATA 1, DATA 2...
                {   

                    // Verificar que el data que llego sea el esperado
                    if (ihdr->nseq == next_ack_to_send-1)
                    {   

                        // Cuando se recibe el paquete esperado
                        fwrite(ipayload, 1, ihdr->len, fp);
                        printf("Se ha escrito DATA %u (%u bytes).\n", ihdr->nseq, ihdr->len);

                        // Enviar ACK del siguiente paquete
                        ohdr->nseq = seq++;
                        ohdr->type = ACK;
                        ohdr->len = 0;

                        //Envio de ACK al servidor                        
                        nsnd = sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0,
                                    (struct sockaddr *)&server_addr, sizeof(server_addr));
                        if (nsnd == -1)
                        {
                            perror("sendto (ACK de datos)");
                            break; // Salir del bucle de recepcion
                        }
                        //printf("confirmacion enviada");

                        printf("Enviando ACK %u.\n", next_ack_to_send);
                        next_ack_to_send++; // Incrementar el paquete que esperamos
                    }
                    else
                    {
                        // cuando se recibe un paquete que no se esperaba
                        fprintf(stderr, "Paquete DATA %u, se esperaba %u.\n", ihdr->nseq, next_ack_to_send-1);
                    }
                }

                //Al recibir tipo END
                else if( ihdr->type != END)
                {
                    fprintf(stderr, "Se recibio un paquete inesperado durante la transferencia:%d.\n", ihdr->type);
                    break;
                }
                if(next_ack_to_send == 256)
                next_ack_to_send=1;

            }

        fclose(fp);
        printf("Archivo recibido y guardado como %s\n", requested_filename);

        // envio confirmacion final al server
        ohdr->nseq = seq++;
        ohdr->type = END;
        strcpy(opayload, "Se ha recibido con exito.");
        ohdr->len = (uint16_t)strlen(opayload);

        nsnd = sendto(sock, obuffer, sizeof(struct hdr) + ohdr->len, 0,
                      (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (nsnd == -1)
        {
            perror("sendto: no se pudo enviar la confirmación final");
        }
        printf("Confirmación final enviada al servidor.\n\n");
    }

    free(obuffer);
    free(ibuffer);

    close(sock);

    return 0;
}