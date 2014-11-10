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
#include <sys/stat.h>
#include <sys/param.h>

#define C2B_VERSION "1.0"
#define LINE_LENGTH_VALUE 65536

extern const char *c2b_samtools;
const char *c2b_samtools = "samtools";
extern const char *c2b_sortbed;
const char *c2b_sortbed = "sort-bed";
extern const char *c2b_starch;
const char *c2b_starch = "starch";

extern const char *c2b_default_output_format;
const char *c2b_default_output_format = "bed";

typedef int boolean;
extern const boolean kTrue;
extern const boolean kFalse;
const boolean kTrue = 1;
const boolean kFalse = 0;

typedef enum format {
    BED_FORMAT,
    STARCH_FORMAT,
    BAM_FORMAT,
    GFF_FORMAT,
    GTF_FORMAT,
    PSL_FORMAT,
    SAM_FORMAT,
    VCF_FORMAT,
    WIG_FORMAT,
    UNDEFINED_FORMAT
} c2b_format;

#define PIPERD 0
#define PIPEWR 1

typedef struct pipeset {
    int Ain[2];
    int Aout[2];
    int Aerr[2];
} c2b_pipeset;

#define PIPE4_FLAG_NONE       (0U)
#define PIPE4_FLAG_RD_CLOEXEC (1U << 0)
#define PIPE4_FLAG_WR_CLOEXEC (1U << 1)

#define c2b_pipe4_cloexec(fd) c2b_pipe4((fd), PIPE4_FLAG_RD_CLOEXEC | PIPE4_FLAG_WR_CLOEXEC)

#define POPEN4_FLAG_NONE       (0U)
#define POPEN4_FLAG_NOCLOSE_PARENT_STDIN        (1U << 0)
#define POPEN4_FLAG_NOCLOSE_PARENT_STDOUT       (1U << 1)
#define POPEN4_FLAG_NOCLOSE_PARENT_STDERR       (1U << 2)
#define POPEN4_FLAG_CLOSE_CHILD_STDIN           (1U << 3)
#define POPEN4_FLAG_CLOSE_CHILD_STDOUT          (1U << 4)
#define POPEN4_FLAG_CLOSE_CHILD_STDERR          (1U << 5)

static const char *name = "convert2bed";
static const char *version = C2B_VERSION;
static const char *authors = "Alex Reynolds";
static const char *usage = "\n" \
    "Usage: convert2bed --input=[format] < input > output\n" \
    "\n" \
    "  Convert common binary and text genomic formats to BED\n\n" \
    "  Required process flags:\n\n" \
    "  --input=[bam|gff|gtf|psl|sam|vcf|wig] | -i [bam|gff|gtf|psl|sam|vcf|wig]\n" \
    "                Genomic format of input file; one of specified keys\n\n" \
    "  Other process flags:\n\n" \
    "  --output=[bed|starch] | -o [bed|starch]\n" \
    "                Format of output file; one of specified keys\n\n" \
    "  --help | -h   Show help message\n";

static struct c2b_global_args_t {
    char *input_format;
    c2b_format input_format_idx;
    char *output_format;
    c2b_format output_format_idx;
    char *samtools_path;
    char *sortbed_path;
    char *starch_path;
} c2b_global_args;

static struct option c2b_client_long_options[] = {
    { "input",          required_argument,   NULL,    'i' },
    { "output",         required_argument,   NULL,    'o' },
    { "help",           no_argument,         NULL,    'h' },
    { NULL,             no_argument,         NULL,     0  }
};

static const char *c2b_client_opt_string = "f:h?";

#ifdef __cplusplus
extern "C" {
#endif

    void * c2b_srcThreadMain(void *arg);
    void * c2b_destThreadMain(void *arg);
    void c2b_setCloseExecFlag(int fd);
    void c2b_unsetCloseExecFlag(int fd);
    int c2b_pipe4(int fd[2], int flags);
    pid_t c2b_popen4(const char* cmd, int pin[2], int pout[2], int perr[2], int flags);
    static void c2b_test_dependencies();
    static boolean c2b_print_matches(char *path, char *fn);
    static boolean c2b_is_there(char *candidate);
    static void c2b_init_globals();
    static void c2b_delete_globals();
    static void c2b_init_command_line_options(int argc, char **argv);
    static void c2b_print_usage(FILE *stream);
    static char * c2b_to_lowercase(const char *src);
    static c2b_format c2b_to_input_format(const char *input_format);
    static c2b_format c2b_to_output_format(const char *output_format);

#ifdef __cplusplus
}
#endif

#endif
