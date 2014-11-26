#ifndef C2B_H
#define C2B_H

#ifdef __cplusplus
#include <cstdio>
#include <cstdlib>
#include <cgetopt>
#include <cstring>
#include <cassert>
#include <cctype>
#include <cinttypes>
#else
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#endif
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>

#define C2B_VERSION "1.0"

#define MAX_FIELD_LENGTH_VALUE 1024
#define MAX_OPERATION_FIELD_LENGTH_VALUE 24
#define MAX_STRAND_LENGTH_VALUE 3
#define MAX_LINE_LENGTH_VALUE 65536
#define MAX_LINES_VALUE 128
#define MAX_OPERATIONS_VALUE 128

extern const char *c2b_samtools;
const char *c2b_samtools = "samtools";
extern const char *c2b_sortbed;
const char *c2b_sortbed = "sort-bed";
extern const char *c2b_starch;
const char *c2b_starch = "starch";

extern const char *c2b_default_output_format;
const char *c2b_default_output_format = "bed";

extern const char *c2b_unmapped_read_chr_name;
const char *c2b_unmapped_read_chr_name = "_unmapped";

extern const char *c2b_header_chr_name;
const char *c2b_header_chr_name = "_header";

extern const char *sortbed_maxmem_arg;
const char *sortbed_maxmem_arg = " --max-mem ";

extern const char *sortbed_maxmem_default_arg;
const char *sortbed_maxmem_default_arg = " --max-mem 2G ";

extern const char *sortbed_tmpdir_arg;
const char *sortbed_tmpdir_arg = " --tmpdir ";

extern const char *sortbed_stdin;
const char *sortbed_stdin = " - ";

extern const char *starch_bzip2_arg;
const char *starch_bzip2_arg = " --bzip2 ";

extern const char *starch_gzip_arg;
const char *starch_gzip_arg = " --gzip ";

extern const char *starch_note_prefix_arg;
const char *starch_note_prefix_arg = " --note=\"";

extern const char *starch_note_suffix_arg;
const char *starch_note_suffix_arg = "\" ";

extern const char *starch_stdin_arg;
const char *starch_stdin_arg = " - ";

typedef int boolean;
extern const boolean kTrue;
extern const boolean kFalse;
const boolean kTrue = 1;
const boolean kFalse = 0;

/* 
   Legal input and output formats
*/

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
} c2b_format_t;

/* 
   BAM/SAM CIGAR operations
   
   Allowed ops: \*|([0-9]+[MIDNSHPX=])+
*/

typedef struct cigar_op {
    unsigned int bases;
    char operation;
} c2b_cigar_op_t;

typedef struct cigar {
    c2b_cigar_op_t *ops;
    ssize_t size;
    ssize_t length;
} c2b_cigar_t;

extern const unsigned int default_cigar_op_bases;
const unsigned int default_cigar_op_bases = 0;

extern const char default_cigar_op_operation;
const char default_cigar_op_operation = '-';

/* 
   SAM struct
*/

typedef struct sam {
    char *rname;
    unsigned long long int start;
    unsigned long long int stop;
    char *qname;
    int flag;
    char *strand;
    char *mapq;
    char *cigar;
    char *rnext;
    char *pnext;
    char *tlen;
    char *seq;
    char *qual;
    char *opt;
} c2b_sam_t;

/* 
   At most, we need 4 pipes to handle the most complex conversion
   pipeline: 
   
   BAM -> Starch
   -----------------------------------------------------------------
   BAM -> SAM -> BED (unsorted) -> BED (sorted) -> Starch
   
   Here, each arrow represents a unidirectional path between
   processing steps. 
   
   Other possibilities are:
   
   BAM -> BED (sorted), BED (unsorted)
   -----------------------------------------------------------------
   BAM -> SAM -> BED (unsorted) -> BED (sorted)
   BAM -> SAM -> BED (unsorted)
   
   GFF -> Starch, BED (sorted), BED (unsorted)
   -----------------------------------------------------------------
   GFF -> BED (unsorted) -> BED (sorted) -> Starch
   GFF -> BED (unsorted) -> BED (sorted)
   GFF -> BED (unsorted)
   
   GTF -> Starch, BED (sorted), BED (unsorted)
   -----------------------------------------------------------------
   GTF -> BED (unsorted) -> BED (sorted) -> Starch
   GTF -> BED (unsorted) -> BED (sorted)
   GTF -> BED (unsorted)
   
   PSL -> Starch, BED (sorted), BED (unsorted)
   -----------------------------------------------------------------
   PSL -> BED (unsorted) -> BED (sorted) -> Starch
   PSL -> BED (unsorted) -> BED (sorted)
   PSL -> BED (unsorted)
   
   SAM -> Starch, BED (sorted), BED (unsorted)
   -----------------------------------------------------------------
   SAM -> BED (unsorted) -> BED (sorted) -> Starch
   SAM -> BED (unsorted) -> BED (sorted)
   SAM -> BED (unsorted)
   
   VCF -> Starch, BED (sorted), BED (unsorted)
   -----------------------------------------------------------------
   VCF -> BED (unsorted) -> BED (sorted) -> Starch
   VCF -> BED (unsorted) -> BED (sorted)
   VCF -> BED (unsorted)
   
   WIG -> Starch, BED (sorted), BED (unsorted)
   -----------------------------------------------------------------
   WIG -> BED (unsorted) -> BED (sorted) -> Starch
   WIG -> BED (unsorted) -> BED (sorted)
   WIG -> BED (unsorted)
   
   If a more complex pipeline arises, we can just increase the value
   of MAX_PIPES.
*/

#define PIPE_READ 0
#define PIPE_WRITE 1
#define PIPE_STREAMS 2
#define MAX_PIPES 4

typedef struct pipeset {
    int **in;
    int **out;
    int **err;
    size_t num;
} c2b_pipeset_t;

typedef struct pipeline_stage {
    c2b_pipeset_t *pipeset;
    void (*line_functor)();
    unsigned int src;
    unsigned int dest;
} c2b_pipeline_stage_t;

#define PIPE4_FLAG_NONE       (0U)
#define PIPE4_FLAG_RD_CLOEXEC (1U << 0)
#define PIPE4_FLAG_WR_CLOEXEC (1U << 1)

#define c2b_pipe4_cloexec(fd) c2b_pipe4((fd), PIPE4_FLAG_RD_CLOEXEC | PIPE4_FLAG_WR_CLOEXEC)

#define POPEN4_FLAG_NONE                        (0U)
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
    "Usage: convert2bed --input=fmt [--output=fmt] [options] < input > output\n" \
    "\n" \
    "  Convert common binary and text genomic formats to BED or BEDOPS Starch (compressed BED)\n\n" \
    "  Input can be a regular file or standard input piped in using the hyphen character ('-')\n\n" \
    "  Required process flags:\n\n" \
    "  --input=[bam|gff|gtf|psl|sam|vcf|wig] | -i [bam|gff|gtf|psl|sam|vcf|wig]\n" \
    "      Genomic format of input file (required)\n\n" \
    "  Format-specific options:\n\n" \
    "  BAM/SAM\n" \
    "  -----------------------------------------------------------------------\n" \
    "  --all-reads | -a\n" \
    "      Include both unmapped and mapped reads in output\n" \
    "  --keep-header | -k\n" \
    "      Preserve header section as pseudo-BED elements\n" \
    "  --split | -s\n" \
    "      Split reads with 'N' CIGAR operations into separate BED elements\n\n" \
    "  GFF\n" \
    "  -----------------------------------------------------------------------\n" \
    "  --keep-header | -k\n" \
    "      Preserve header section as pseudo-BED elements\n\n" \
    "  PSL\n" \
    "  -----------------------------------------------------------------------\n" \
    "  --headered | -p\n" \
    "      Convert headered PSL input to BED (default is headerless)\n" \
    "  --keep-header | -k\n" \
    "      Preserve header section as pseudo-BED elements (requires --headered)\n\n" \
    "  VCF\n" \
    "  -----------------------------------------------------------------------\n" \
    "  --snvs | -v\n" \
    "      Report only single nucleotide variants\n" \
    "  --insertions | -t\n" \
    "      Report only insertion variants\n" \
    "  --deletions | -n\n" \
    "      Report only deletion variants\n" \
    "  --keep-header | -k\n" \
    "      Preserve header section as pseudo-BED elements\n\n" \
    "  WIG\n" \
    "  -----------------------------------------------------------------------\n" \
    "  --multisplit=[basename] | -b [basename]\n" \
    "      A single input file may have multiple WIG sections; a user may pass in\n" \
    "      more than one file, or both may occur. With this option, every separate\n" \
    "      input goes to a separate output, starting with [basename].1, then\n" \
    "      [basename].2, and so on\n\n" \
    "  Other processing options:\n\n" \
    "  --output=[bed|starch] | -o [bed|starch]\n" \
    "      Format of output file (optional, default is BED)\n" \
    "  --do-not-sort | -d\n" \
    "      Do not sort BED output with sort-bed (not compatible with --output=starch)\n" \
    "  --max-mem=[value] | -m [value]\n" \
    "      Sets aside [value] memory for sorting BED output. For example, [value] can\n" \
    "      be 8G, 8000M or 8000000000 to specify 8 GB of memory (default is 2G)\n"
    "  --sort-tmpdir=[dir] | -r [dir]\n" \
    "      Optionally sets [dir] as temporary directory for sort data, when used in\n" \
    "      conjunction with --max-mem=[value], instead of the host's operating system\n" \
    "      default temporary directory\n" \
    "  --starch-bzip2 | -z\n" \
    "      Used with --output=starch, the compressed output explicitly applies the bzip2\n" \
    "      algorithm to compress intermediate data (default is bzip2)\n" \
    "  --starch-gzip | -g\n" \
    "      Used with --output=starch, the compressed output applies gzip compression on\n" \
    "      intermediate data\n" \
    "  --starch-note=\"xyz...\" | -e\n" \
    "      Used with --output=starch, this adds a note to the Starch archive metadata\n" \
    "  --help | -h\n" \
    "      Show help message\n";

static struct globals {
    char *input_format;
    c2b_format_t input_format_idx;
    char *output_format;
    c2b_format_t output_format_idx;
    char *samtools_path;
    char *sortbed_path;
    char *starch_path;
    boolean sort_flag;
    boolean all_reads_flag;
    boolean keep_header_flag;
    boolean split_flag;
    boolean headered_flag;
    boolean vcf_snvs_flag;
    boolean vcf_insertions_flag;
    boolean vcf_deletions_flag;
    boolean starch_bzip2_flag;
    boolean starch_gzip_flag;
    char *starch_note;
    char *max_mem_value;
    char *sort_tmpdir_path;
    char *wig_basename;
    unsigned int header_line_idx;
    c2b_cigar_t *cigar;
} c2b_globals;

static struct option c2b_client_long_options[] = {
    { "input",          required_argument,   NULL,    'i' },
    { "output",         required_argument,   NULL,    'o' },
    { "do-not-sort",    no_argument,         NULL,    'd' },
    { "all-reads",      no_argument,         NULL,    'a' },
    { "keep-header",    no_argument,         NULL,    'k' },
    { "split",          no_argument,         NULL,    's' },
    { "headered",       no_argument,         NULL,    'p' },
    { "snvs",           no_argument,         NULL,    'v' },
    { "insertions",     no_argument,         NULL,    't' },
    { "deletions",      no_argument,         NULL,    'n' },
    { "starch-bzip2",   no_argument,         NULL,    'z' },
    { "starch-gzip",    no_argument,         NULL,    'g' },
    { "starch-note",    required_argument,   NULL,    'e' },
    { "max-mem",        required_argument,   NULL,    'm' },
    { "sort-tmpdir",    required_argument,   NULL,    'r' },
    { "basename",       required_argument,   NULL,    'b' },
    { "help",           no_argument,         NULL,    'h' },
    { NULL,             no_argument,         NULL,     0  }
};

static const char *c2b_client_opt_string = "i:o:dakspvtnzge:m:r:b:h?";

#ifdef __cplusplus
extern "C" {
#endif

    static void              c2b_init_conversion(c2b_pipeset_t *p);
    static void              c2b_init_bam_conversion(c2b_pipeset_t *p);
    static void              c2b_line_convert_sam_to_bed_unsorted_without_split_operation(char *dest, ssize_t *dest_size, char *src, ssize_t src_size);
    static void              c2b_line_convert_sam_to_bed_unsorted_with_split_operation(char *dest, ssize_t *dest_size, char *src, ssize_t src_size); 
    static void              c2b_sam_to_bed(c2b_sam_t s, char **dest, ssize_t **dest_size);
    static void              c2b_cigar_str_to_ops(char *s);
    static void              c2b_init_cigar_ops(c2b_cigar_t **c, const ssize_t size);
    static void              c2b_debug_cigar_ops(c2b_cigar_t *c);
    static void              c2b_delete_cigar_ops(c2b_cigar_t *c);
    static void *            c2b_read_bytes_from_stdin(void *arg);
    static void *            c2b_process_intermediate_bytes_by_lines(void *arg);
    static void *            c2b_write_in_bytes_to_in_process(void *arg);
    static void *            c2b_write_out_bytes_to_in_process(void *arg);
    static void *            c2b_write_in_bytes_to_stdout(void *arg);
    static void *            c2b_write_out_bytes_to_stdout(void *arg);
    static void              c2b_memrchr_offset(ssize_t *offset, char *buf, ssize_t buf_size, ssize_t len, char delim);
    static void              c2b_init_pipeset(c2b_pipeset_t *p, const size_t num);
    static void              c2b_debug_pipeset(c2b_pipeset_t *p, const size_t num);
    static void              c2b_delete_pipeset(c2b_pipeset_t *p);
    static void              c2b_set_close_exec_flag(int fd);
    static void              c2b_unset_close_exec_flag(int fd);
    static int               c2b_pipe4(int fd[2], int flags);
    static pid_t             c2b_popen4(const char* cmd, int pin[2], int pout[2], int perr[2], int flags);
    static void              c2b_test_dependencies();
    static boolean           c2b_print_matches(char *path, char *fn);
    static char *            c2b_strsep(char **stringp, const char *delim);
    static boolean           c2b_is_there(char *candidate);
    static void              c2b_init_globals();
    static void              c2b_delete_globals();
    static void              c2b_init_command_line_options(int argc, char **argv);
    static void              c2b_print_usage(FILE *stream);
    static char *            c2b_to_lowercase(const char *src);
    static c2b_format_t      c2b_to_input_format(const char *input_format);
    static c2b_format_t      c2b_to_output_format(const char *output_format);

#ifdef __cplusplus
}
#endif

#endif
