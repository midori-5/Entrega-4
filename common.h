#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"

#define CTRL 0
#define DATA 1

#define REQUEST 0
#define REPLY 1
#define ACK 2
#define END 3

#define DEFAULT_IP   "127.0.0.1"
#define DEFAULT_PORT 4950

#define HEADER_SIZE (sizeof (uint8_t) + sizeof (uint8_t) + sizeof (uint16_t))
#define PAYLOAD_SIZE 32768
#define MAX_MSGLEN (sizeof (struct msg))

#pragma pack(push, 1)
struct hdr {
    uint8_t  nseq;
    uint8_t  type;
    uint16_t len; 
};
#pragma pack(pop)

struct msg {
    struct hdr h;
    char payload[PAYLOAD_SIZE];
};

extern const char *program_name;

int parse_port(const char *s, uint16_t *out);
void usage(FILE *stream);


#endif
