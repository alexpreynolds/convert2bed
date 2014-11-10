#ifndef C2B_H
#define C2B_H

#ifdef __cplusplus
#include <cstdio>
#include <cstdlib>
#include <cgetopt>
#include <cstring>
#else
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#endif
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#define C2B_VERSION "1.0"
#define LINE_LENGTH_VALUE 65536

typedef int boolean;
extern const boolean kTrue;
extern const boolean kFalse;
const boolean kTrue = 1;
const boolean kFalse = 0;

typedef enum format {
    BAM_FORMAT,
    GFF_FORMAT,
    GTF_FORMAT,
    PSL_FORMAT,
    SAM_FORMAT,
    VCF_FORMAT,
    WIG_FORMAT,
    UNDEFINED_FORMAT
} c2b_format;

static const char *name = "convert2bed";
static const char *version = C2B_VERSION;
static const char *authors = "Alex Reynolds";
static const char *usage = "\n" \
    "Usage: convert2bed --format=[format] < input > output\n" \
    "\n" \
    "  Convert common binary and text genomic formats to BED\n\n" \
    "  Required process flags:\n\n" \
    "  --format=[bam|gff|gtf|psl|sam|vcf|wig] | -f [bam|gff|gtf|psl|sam|vcf|wig]\n" \
    "                Genomic format of input file; one of specified keys\n\n" \
    "  Other process flags:\n\n" \
    "  --help | -h   Show help message\n";

static struct c2b_global_args_t {
    char *format;
    c2b_format format_idx;
} c2b_global_args;

static struct option c2b_client_long_options[] = {
    { "format",         required_argument,   NULL,    'f' },
    { "help",           no_argument,         NULL,    'h' },
    { NULL,             no_argument,         NULL,     0  }
};

static const char *c2b_client_opt_string = "f:h?";

#ifdef __cplusplus
extern "C" {
#endif
    
    void c2b_init_globals();
    void c2b_delete_globals();
    void c2b_init_command_line_options(int argc, char **argv);
    void c2b_print_usage(FILE *stream);
    char * c2b_tolower(char *src);
    c2b_format c2b_toformat(char *fmt);

#ifdef __cplusplus
}
#endif

#endif
