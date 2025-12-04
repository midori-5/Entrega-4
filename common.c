#include "common.h"

int parse_port(const char *s, uint16_t *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (!s || *s=='\0' || (end && *end!='\0')) 
    	return -1;
    if (v < 0 || v > 65535) 
    	return -1;
    *out = (uint16_t)v;

    return 0;
}

void usage(FILE *stream)
{
    fprintf(stream, "Uso: %s [opciones]\n", program_name);
    fprintf(stream,
        " -i <X.X.X.X>     Dirección IPv4 (default: " DEFAULT_IP ")\n"
        " -p <0-65535>     Puerto UDP (default: %d)\n"
        " -h               Muestra esta ayuda\n"
        " -v               Modo detallado\n", DEFAULT_PORT);
}
