#include "convert2bed.h"

int 
main(int argc, char **argv)
{
#ifdef DEBUG
    fprintf (stderr, "--- convert2bed main() - enter ---\n");
#endif

    c2b_init_globals();
    c2b_init_command_line_options(argc, argv);
    c2b_delete_globals();

#ifdef DEBUG
    fprintf (stderr, "--- convert2bed main() - exit  ---\n");
#endif
    return EXIT_SUCCESS;
}

static void 
c2b_init_globals()
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_init_globals() - enter ---\n");
#endif

    c2b_global_args.format = NULL;
    c2b_global_args.format_idx = UNDEFINED_FORMAT;

#ifdef DEBUG
    fprintf(stderr, "--- c2b_init_globals() - exit  ---\n");
#endif
}

static void
c2b_delete_globals()
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_delete_globals() - enter ---\n");
#endif

    if (c2b_global_args.format) free(c2b_global_args.format), c2b_global_args.format = NULL;
    c2b_global_args.format_idx = UNDEFINED_FORMAT;

#ifdef DEBUG
    fprintf(stderr, "--- c2b_delete_globals() - exit  ---\n");
#endif
}

static void 
c2b_init_command_line_options(int argc, char **argv)
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_init_command_line_options() - enter ---\n");
#endif

    char *format = NULL;
    int client_long_index;
    int client_opt = getopt_long(argc, 
                                 argv, 
                                 c2b_client_opt_string, 
                                 c2b_client_long_options, 
                                 &client_long_index);

    opterr = 0; /* disable error reporting by GNU getopt */

    while (client_opt != -1) {
	switch (client_opt) {
	case 'f':
	    format = malloc(strlen(optarg) + 1);
	    if (!format) {
		fprintf(stderr, "Error: Could not allocate space for format argument\n");
		exit(EXIT_FAILURE);
	    }
	    strcpy(format, optarg);
	    c2b_global_args.format = c2b_to_lowercase(format);
	    c2b_global_args.format_idx = c2b_to_input_format(c2b_global_args.format);
	    free(format), format = NULL;
	    break;
	case 'h':
	    c2b_print_usage(stdout);
	    exit(EXIT_SUCCESS);
	case '?':
	    c2b_print_usage(stderr);
	    exit(EXIT_SUCCESS);
	default:
	    break;
	}
	client_opt = getopt_long(argc,
                                 argv,
                                 c2b_client_opt_string,
                                 c2b_client_long_options,
                                 &client_long_index);
    }

    if ((!c2b_global_args.format) || (c2b_global_args.format_idx == UNDEFINED_FORMAT)) {
	c2b_print_usage(stderr);
	exit(EXIT_FAILURE);
    }

#ifdef DEBUG
    fprintf(stderr, "--- c2b_init_command_line_options() - exit  ---\n");
#endif
}

static void 
c2b_print_usage(FILE *stream)
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_print_usage() - enter ---\n");
#endif

    fprintf(stream, 
            "%s\n" \
            "  version: %s\n" \
            "  author:  %s\n" \
            "%s\n", 
            name, 
            version,
            authors,
            usage);

#ifdef DEBUG
    fprintf(stderr, "--- c2b_print_usage() - exit  ---\n");
#endif
}

static char *
c2b_to_lowercase(char *src) 
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_to_lowercase() - enter ---\n");
#endif

    char *dest = NULL;
    char *p = NULL;
    
    if (!src) return dest;

    p = malloc(strlen(src) + 1);
    if (!p) {
	fprintf(stderr, "Error: Could not allocate space for lowercase translation\n");
	exit(EXIT_FAILURE);
    }
    strcpy(p, src);
    dest = p;
    for ( ; *p; ++p) *p = (*p >= 'A' && *p <= 'Z') ? (*p | 0x60) : *p;

#ifdef DEBUG
    fprintf(stderr, "--- c2b_to_lowercase() - exit  ---\n");
#endif
    return dest;
}

static c2b_format
c2b_to_input_format(char *fmt)
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_to_input_format() - enter ---\n");
    fprintf(stderr, "--- c2b_to_input_format() - exit  ---\n");
#endif

    return 
	(strcmp(fmt, "bam") == 0) ? BAM_FORMAT :
	(strcmp(fmt, "gff") == 0) ? GFF_FORMAT :
	(strcmp(fmt, "gtf") == 0) ? GTF_FORMAT :
	(strcmp(fmt, "psl") == 0) ? PSL_FORMAT :
	(strcmp(fmt, "sam") == 0) ? SAM_FORMAT :
	(strcmp(fmt, "vcf") == 0) ? VCF_FORMAT :
	(strcmp(fmt, "wig") == 0) ? WIG_FORMAT :
	UNDEFINED_FORMAT;
}
