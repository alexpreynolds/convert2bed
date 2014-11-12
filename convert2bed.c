#include "convert2bed.h"

int
main(int argc, char **argv)
{
#ifdef DEBUG
    fprintf (stderr, "--- convert2bed main() - enter ---\n");
#endif

    c2b_pipeset pipes;

    /* setup */
    c2b_init_globals();
    c2b_init_command_line_options(argc, argv);
    c2b_test_dependencies();
    c2b_init_pipeset(&pipes, MAX_PIPES);

    /* convert */
    c2b_init_conversion(&pipes);

    /* clean-up */
    c2b_delete_pipeset(&pipes);
    c2b_delete_globals();

#ifdef DEBUG
    fprintf (stderr, "--- convert2bed main() - exit  ---\n");
#endif
    return EXIT_SUCCESS;
}

static void
c2b_init_conversion(c2b_pipeset *p)
{
    if (c2b_global_args.input_format_idx == BAM_FORMAT)
        c2b_init_bam_conversion(p);
    else {
        fprintf(stderr, "Error: Unsupported format\n");
        exit(EXIT_FAILURE);
    }
}

static void
c2b_init_bam_conversion(c2b_pipeset *p)
{
    pthread_t bam2sam_thread, sam2bed_unsorted_thread, bed_unsorted2stdout_thread;
    pid_t bam2sam_proc;
    char bam2sam_cmd[LINE_LENGTH_VALUE] = {0};
    const char *bam2sam_args = " view -h -";
    c2b_pipeline_stage bam2sam_stage;

    bam2sam_stage.pipeset = p;
    bam2sam_stage.line_functor = NULL;
    bam2sam_stage.src = -1; /* src is really stdin */
    bam2sam_stage.dest = 0;

    c2b_pipeline_stage sam2bed_unsorted_stage;
    void (*sam2bed_unsorted_line_functor)(char *, char *) = c2b_line_convert_sam_bed;

    sam2bed_unsorted_stage.pipeset = p;
    sam2bed_unsorted_stage.line_functor = sam2bed_unsorted_line_functor;
    sam2bed_unsorted_stage.src = 0;
    sam2bed_unsorted_stage.dest = 1;

    c2b_pipeline_stage bed_unsorted2stdout_stage;

    bed_unsorted2stdout_stage.pipeset = p;
    bed_unsorted2stdout_stage.line_functor = NULL;
    bed_unsorted2stdout_stage.src = 1;
    bed_unsorted2stdout_stage.dest = -1; /* dest is stdout */

    /*
       We open pid_t (process) instances to handle data in a specified order. 
    */
    memcpy(bam2sam_cmd, c2b_global_args.samtools_path, strlen(c2b_global_args.samtools_path));
    memcpy(bam2sam_cmd + strlen(c2b_global_args.samtools_path), bam2sam_args, strlen(bam2sam_args));
    bam2sam_proc = c2b_popen4(bam2sam_cmd,
			      p->in[0],
			      p->out[0],
			      p->err[0],
			      POPEN4_FLAG_NONE);

    /*
       Once we have the desired process instances, we create and join
       threads for their ordered execution.
    */

    pthread_create(&bam2sam_thread,
		   NULL,
		   c2b_read_bytes_from_stdin,
		   &bam2sam_stage);

    pthread_create(&sam2bed_unsorted_thread,
		   NULL,
		   c2b_process_intermediate_bytes_by_lines,
		   &sam2bed_unsorted_stage);

    pthread_create(&bed_unsorted2stdout_thread,
		   NULL,
		   c2b_write_bytes_to_stdout,
		   &bed_unsorted2stdout_stage);

    pthread_join(bam2sam_thread, (void **) NULL);
    pthread_join(sam2bed_unsorted_thread, (void **) NULL);
    pthread_join(bed_unsorted2stdout_thread, (void **) NULL);
}

static void
c2b_line_convert_sam_bed(char *dest, char *src)
{
}

static void *
c2b_read_bytes_from_stdin(void *arg)
{
    c2b_pipeline_stage *stage = (c2b_pipeline_stage *) arg;
    c2b_pipeset *pipes = stage->pipeset;
    char buffer[LINE_LENGTH_VALUE];
    ssize_t bytes_read;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    while ((bytes_read = read(STDIN_FILENO, buffer, LINE_LENGTH_VALUE)) > 0) {
        write(pipes->in[stage->dest][PIPE_WRITE], buffer, bytes_read);
    }
#pragma GCC diagnostic pop
    close(pipes->in[stage->dest][PIPE_WRITE]);

    pthread_exit(NULL);
}

static void *
c2b_process_intermediate_bytes_by_lines(void *arg)
{
    c2b_pipeline_stage *stage = (c2b_pipeline_stage *) arg;
    c2b_pipeset *pipes = stage->pipeset;
    char src_buffer[LINE_LENGTH_VALUE];
    ssize_t src_bytes_read = 0;
    ssize_t remainder_length = 0;
    ssize_t remainder_offset = 0;
    char line_delim = '\n';
    void (*line_functor)(char *, char *) = stage->line_functor;

    /* 
       We read from the src out pipe, then write to the dest in pipe 
    */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    while ((src_bytes_read = read(pipes->out[stage->src][PIPE_READ],
				  src_buffer + remainder_length,
				  LINE_LENGTH_VALUE - remainder_length)) > 0) {

        /* 
           So here's what src_buffer looks like initially; basically, some stuff separated by
           newlines. The src_buffer will probably not terminate with a newline. So we first use 
           a custom memrchr() call to find the remainder_offset index value:
           
           src_buffer  [  .  .  .  \n  .  .  .  \n  .  .  .  \n  .  .  .  .  .  .  ]
           index        0 1 2 ...                            ^                    ^
                                                             |                    |
                                                             |   src_bytes_read --
                                                             | 
                                         remainder_offset --- 

           In other words, everything at and after index [remainder_offset + 1] to index
           [src_bytes_read - 1] is a remainder byte ("R"):

           src_buffer  [  .  .  .  \n  .  .  .  \n  .  .  .  \n R R R R R ]
           
           Assuming this failed:

           If remainder_offset is -1 and we have read LINE_LENGTH_VALUE bytes, then we know there 
           are no newlines anywhere in the src_buffer and so we terminate early with an error state.
           This would suggest either LINE_LENGTH_VALUE is too small to hold a whole intermediate 
           line or the input data is perhaps corrupt. Basically, something went wrong and we need 
           to investigate.
           
           Asumming this worked:
           
           We can now parse byte indices {[0 .. remainder_offset]} into lines, which are fed one
           by one to the line_functor. This functor parses out tab offsets and writes out a 
           reordered string based on the rules for the format (see BEDOPS docs for reordering 
           table).
           
           Finally, we write bytes from index [remainder_offset + 1] to [src_bytes_read - 1] 
           back to src_buffer. We are writing remainder_length bytes:
           
           new_remainder_length = current_src_bytes_read + old_remainder_length - new_remainder_offset
           
           Note that we can leave the rest of the buffer untouched:
           
           src_buffer  [ R R R R R \n  .  .  .  \n  .  .  .  \n  .  .  .  ]
           
           On the subsequent read, we want to read() into src_buffer at position remainder_length
           and read up to, at most, (LINE_LENGTH_VALUE - remainder_length) bytes:
           
           read(byte_source, 
                src_buffer + remainder_length,
                LINE_LENGTH_VALUE - remainder_length)

           Second and subsequent reads should reduce the maximum number of src_bytes_read from 
           LINE_LENGTH_VALUE to something smaller.

           Note: We should look into doing a final pass through the line_functor, once we grab the 
           last buffer, after the last read().
        */

        write(pipes->in[stage->dest][PIPE_WRITE], src_buffer + remainder_length, src_bytes_read);

        c2b_memrchr_offset(&remainder_offset, src_buffer, src_bytes_read + remainder_length, line_delim);

        if ((remainder_offset == -1) && (src_bytes_read + remainder_length == LINE_LENGTH_VALUE)) {
            fprintf(stderr, "Error: Could not find newline in intermediate buffer; check input\n");
            exit(EXIT_FAILURE);
        }

        remainder_length = src_bytes_read + remainder_length - remainder_offset;
        memcpy(src_buffer, src_buffer + remainder_offset, remainder_length);
    }
#pragma GCC diagnostic pop
    close(pipes->in[stage->dest][PIPE_WRITE]);

    pthread_exit(NULL);
}

static void
c2b_memrchr_offset(ssize_t *offset, char *buf, ssize_t len, char delim)
{
    ssize_t left = len;

    /* fprintf(stderr, "[------------------]\n"); */
    while (left > 0) {
        /* fprintf(stderr, "[%zu : %c]\n", left, buf[left - 1]); */
        if (buf[left - 1] == delim) {
            *offset = left;
            return;
        }
        left--;
    }
    *offset = -1;
}

static void *
c2b_write_bytes_to_stdout(void *arg)
{
    c2b_pipeline_stage *stage = (c2b_pipeline_stage *) arg;
    c2b_pipeset *pipes = stage->pipeset;
    char buffer[LINE_LENGTH_VALUE];
    ssize_t bytes_read;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    while ((bytes_read = read(pipes->in[stage->src][PIPE_READ], buffer, LINE_LENGTH_VALUE)) > 0) {
        write(STDOUT_FILENO, buffer, bytes_read);
    }
#pragma GCC diagnostic pop

    pthread_exit(NULL);
}

static void
c2b_init_pipeset(c2b_pipeset *p, const size_t num)
{
    int **ins = NULL;
    int **outs = NULL;
    int **errs = NULL;
    size_t n;

    ins = malloc(num * sizeof(int *));
    outs = malloc(num * sizeof(int *));
    errs = malloc(num * sizeof(int *));
    if ((!ins) || (!outs) || (!errs)) {
        fprintf(stderr, "Error: Could not allocate space to temporary pipe arrays\n");
        exit(EXIT_FAILURE);
    }

    p->in = ins;
    p->out = outs;
    p->err = errs;

    for (n = 0; n < num; n++) {
	p->in[n] = NULL;
	p->in[n] = malloc(PIPE_STREAMS * sizeof(int));
	if (!p->in[n]) {
	    fprintf(stderr, "Error: Could not allocate space to temporary internal pipe array\n");
	    exit(EXIT_FAILURE);
	}
	p->out[n] = NULL;
	p->out[n] = malloc(PIPE_STREAMS * sizeof(int));
	if (!p->out[n]) {
	    fprintf(stderr, "Error: Could not allocate space to temporary internal pipe array\n");
	    exit(EXIT_FAILURE);
	}
	p->err[n] = NULL;
	p->err[n] = malloc(PIPE_STREAMS * sizeof(int));
	if (!p->err[n]) {
	    fprintf(stderr, "Error: Could not allocate space to temporary internal pipe array\n");
	    exit(EXIT_FAILURE);
	}

	/* set close-on-exec flag for each pipe */
	c2b_pipe4_cloexec(p->in[n]);
	c2b_pipe4_cloexec(p->out[n]);
	c2b_pipe4_cloexec(p->err[n]);
    }

    p->num = num;
}

static void
c2b_delete_pipeset(c2b_pipeset *p)
{
    size_t n;

    for (n = 0; n < p->num; n++) {
        free(p->in[n]), p->in[n] = NULL;
        free(p->out[n]), p->out[n] = NULL;
        free(p->err[n]), p->err[n] = NULL;
    }
    free(p->in), p->in = NULL;
    free(p->out), p->out = NULL;
    free(p->err), p->err = NULL;

    p->num = 0;
}

static void
c2b_set_close_exec_flag(int fd)
{
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
}

static void
c2b_unset_close_exec_flag(int fd)
{
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) & ~FD_CLOEXEC);
}

static int
c2b_pipe4(int fd[2], int flags)
{
    int ret = pipe(fd);
    if (flags & PIPE4_FLAG_RD_CLOEXEC) {
        c2b_set_close_exec_flag(fd[PIPE_READ]);
    }
    if (flags & PIPE4_FLAG_WR_CLOEXEC) {
        c2b_set_close_exec_flag(fd[PIPE_WRITE]);
    }
    return ret;
}

static pid_t
c2b_popen4(const char* cmd, int pin[2], int pout[2], int perr[2], int flags)
{
    pid_t ret = fork();

    if (ret < 0) {
        fprintf(stderr, "fork() failed!\n");
        return ret;
    } else if (ret == 0) {
        /**
         * Child-side of fork
         */
        if (flags & POPEN4_FLAG_CLOSE_CHILD_STDIN) {
            close(STDIN_FILENO);
        }
        else {
            c2b_unset_close_exec_flag(pin[PIPE_READ]);
            dup2(pin[PIPE_READ], STDIN_FILENO);
        }
        if (flags & POPEN4_FLAG_CLOSE_CHILD_STDOUT) {
            close(STDOUT_FILENO);
        }
        else {
            c2b_unset_close_exec_flag(pout[PIPE_WRITE]);
            dup2(pout[PIPE_WRITE], STDOUT_FILENO);
        }
        if (flags & POPEN4_FLAG_CLOSE_CHILD_STDERR) {
            close(STDERR_FILENO);
        }
        else {
            c2b_unset_close_exec_flag(perr[PIPE_WRITE]);
            dup2(perr[PIPE_WRITE], STDERR_FILENO);
        }
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        fprintf(stderr, "exec() failed!\n");
        exit(-1);
    }
    else {
        /**
         * Parent-side of fork
         */
        if (~flags & POPEN4_FLAG_NOCLOSE_PARENT_STDIN && ~flags & POPEN4_FLAG_CLOSE_CHILD_STDIN) {
            close(pin[PIPE_READ]);
        }
        if (~flags & POPEN4_FLAG_NOCLOSE_PARENT_STDOUT && ~flags & POPEN4_FLAG_CLOSE_CHILD_STDOUT) {
            close(pout[PIPE_WRITE]);
        }
        if (~flags & POPEN4_FLAG_NOCLOSE_PARENT_STDERR && ~flags & POPEN4_FLAG_CLOSE_CHILD_STDERR) {
            close(perr[PIPE_WRITE]);
        }
        return ret;
    }

    /* Unreachable */
    return ret;
}

static void
c2b_test_dependencies()
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_test_dependencies() - enter ---\n");
#endif

    char *p = NULL;
    char *path = NULL;

    if ((p = getenv("PATH")) == NULL) {
        fprintf(stderr, "Error: Cannot retrieve environment PATH variable\n");
        exit(EXIT_FAILURE);
    }
    path = malloc(strlen(p) + 1);
    if (!path) {
        fprintf(stderr, "Error: Cannot allocate space for path variable copy\n");
        exit(EXIT_FAILURE);
    }
    memcpy(path, p, strlen(p) + 1);

    if ((c2b_global_args.input_format_idx == BAM_FORMAT) || (c2b_global_args.input_format_idx == SAM_FORMAT)) {
        char *samtools = NULL;
        samtools = malloc(strlen(c2b_samtools) + 1);
        if (!samtools) {
            fprintf(stderr, "Error: Cannot allocate space for samtools variable copy\n");
            exit(EXIT_FAILURE);
        }
        memcpy(samtools, c2b_samtools, strlen(c2b_samtools) + 1);

        char *path_samtools = NULL;
        path_samtools = malloc(strlen(path) + 1);
        if (!path_samtools) {
            fprintf(stderr, "Error: Cannot allocate space for path (samtools) copy\n");
            exit(EXIT_FAILURE);
        }
        memcpy(path_samtools, path, strlen(path) + 1);

#ifdef DEBUG
        fprintf(stderr, "Debug: Searching [%s] for samtools\n", path_samtools);
#endif

        if (c2b_print_matches(path_samtools, samtools) != kTrue) {
            fprintf(stderr, "Error: Cannot find samtools binary required for conversion of BAM and SAM format\n");
            exit(EXIT_FAILURE);
        }
        free(path_samtools), path_samtools = NULL;
        free(samtools), samtools = NULL;
    }

    if (c2b_global_args.sort_flag) {
        char *sortbed = NULL;
        sortbed = malloc(strlen(c2b_sortbed) + 1);
        if (!sortbed) {
            fprintf(stderr, "Error: Cannot allocate space for sort-bed variable copy\n");
            exit(EXIT_FAILURE);
        }
        memcpy(sortbed, c2b_sortbed, strlen(c2b_sortbed) + 1);

        char *path_sortbed = NULL;
        path_sortbed = malloc(strlen(path) + 1);
        if (!path_sortbed) {
            fprintf(stderr, "Error: Cannot allocate space for path (samtools) copy\n");
            exit(EXIT_FAILURE);
        }
        memcpy(path_sortbed, path, strlen(path) + 1);

#ifdef DEBUG
        fprintf(stderr, "Debug: Searching [%s] for samtools\n", path_sortbed);
#endif

        if (c2b_print_matches(path_sortbed, sortbed) != kTrue) {
            fprintf(stderr, "Error: Cannot find sort-bed binary required for sorting BED output\n");
            exit(EXIT_FAILURE);
        }
        free(path_sortbed), path_sortbed = NULL;
        free(sortbed), sortbed = NULL;
    }

    if (c2b_global_args.output_format_idx == STARCH_FORMAT) {
        char *starch = NULL;
        starch = malloc(strlen(c2b_starch) + 1);
        if (!starch) {
            fprintf(stderr, "Error: Cannot allocate space for starch variable copy\n");
            exit(EXIT_FAILURE);
        }
        memcpy(starch, c2b_starch, strlen(c2b_starch) + 1);

        char *path_starch = NULL;
        path_starch = malloc(strlen(path) + 1);
        if (!path_starch) {
            fprintf(stderr, "Error: Cannot allocate space for path (starch) copy\n");
            exit(EXIT_FAILURE);
        }
        memcpy(path_starch, path, strlen(path) + 1);

#ifdef DEBUG
        fprintf(stderr, "Debug: Searching [%s] for starch\n", path_starch);
#endif

        if (c2b_print_matches(path_starch, starch) != kTrue) {
            fprintf(stderr, "Error: Cannot find starch binary required for compression of BED output\n");
            exit(EXIT_FAILURE);
        }
        free(path_starch), path_starch = NULL;
        free(starch), starch = NULL;
    }

    free(path), path = NULL;

#ifdef DEBUG
    fprintf(stderr, "--- c2b_test_dependencies() - exit  ---\n");
#endif
}

static boolean
c2b_print_matches(char *path, char *fn)
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_print_matches() - enter ---\n");
#endif

    char candidate[PATH_MAX];
    const char *d;
    boolean found = kFalse;

    if (strchr(fn, '/') != NULL) {
        return (c2b_is_there(fn) ? kTrue : kFalse);
    }
    while ((d = c2b_strsep(&path, ":")) != NULL) {
        if (*d == '\0') {
            d = ".";
        }
        if (snprintf(candidate, sizeof(candidate), "%s/%s", d, fn) >= (int) sizeof(candidate)) {
            continue;
        }
        if (c2b_is_there(candidate)) {
            found = kTrue;
            if (strcmp(fn, c2b_samtools) == 0) {
                c2b_global_args.samtools_path = malloc(strlen(candidate) + 1);
                if (!c2b_global_args.samtools_path) {
                    fprintf(stderr, "Error: Could not allocate space for storing samtools path global\n");
                    exit(EXIT_FAILURE);
                }
                memcpy(c2b_global_args.samtools_path, candidate, strlen(candidate) + 1);
            }
            else if (strcmp(fn, c2b_sortbed) == 0) {
                c2b_global_args.sortbed_path = malloc(strlen(candidate) + 1);
                if (!c2b_global_args.sortbed_path) {
                    fprintf(stderr, "Error: Could not allocate space for storing sortbed path global\n");
                    exit(EXIT_FAILURE);
                }
                memcpy(c2b_global_args.sortbed_path, candidate, strlen(candidate) + 1);
            }
            else if (strcmp(fn, c2b_starch) == 0) {
                c2b_global_args.starch_path = malloc(strlen(candidate) + 1);
                if (!c2b_global_args.starch_path) {
                    fprintf(stderr, "Error: Could not allocate space for storing starch path global\n");
                    exit(EXIT_FAILURE);
                }
                memcpy(c2b_global_args.starch_path, candidate, strlen(candidate) + 1);
            }
            break;
        }
    }

#ifdef DEBUG
    fprintf(stderr, "--- c2b_print_matches() - exit  ---\n");
#endif

    return found;
}

static char *
c2b_strsep(char **stringp, const char *delim)
{
    char *s;
    const char *spanp;
    int c, sc;
    char *tok;

    if ((s = *stringp) == NULL)
        return NULL;

    for (tok = s;;) {
        c = *s++;
        spanp = delim;
        do {
            if ((sc = *spanp++) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                *stringp = s;
                return tok;
            }
        } while (sc != 0);
    }

    return NULL;
}

static boolean
c2b_is_there(char *candidate)
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_is_there() - enter ---\n");
#endif

    struct stat fin;
    boolean found = kFalse;

    if (access(candidate, X_OK) == 0 &&
	    stat(candidate, &fin) == 0 &&
        S_ISREG(fin.st_mode) &&
        (getuid() != 0 || (fin.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)) {
#ifdef DEBUG
	    fprintf(stderr, "Debug: Found dependency [%s]\n", candidate);
#endif
        found = kTrue;
    }

#ifdef DEBUG
    fprintf(stderr, "--- c2b_is_there() - exit  ---\n");
#endif

    return found;
}

static void
c2b_init_globals()
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_init_globals() - enter ---\n");
#endif

    c2b_global_args.input_format = NULL;
    c2b_global_args.input_format_idx = UNDEFINED_FORMAT;

    c2b_global_args.output_format = NULL;
    c2b_global_args.output_format_idx = UNDEFINED_FORMAT;

    c2b_global_args.samtools_path = NULL;
    c2b_global_args.sortbed_path = NULL;
    c2b_global_args.starch_path = NULL;

    c2b_global_args.sort_flag = kTrue;

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

    if (c2b_global_args.input_format)
        free(c2b_global_args.input_format), c2b_global_args.input_format = NULL;
    if (c2b_global_args.samtools_path)
        free(c2b_global_args.samtools_path), c2b_global_args.samtools_path = NULL;
    if (c2b_global_args.sortbed_path)
        free(c2b_global_args.sortbed_path), c2b_global_args.sortbed_path = NULL;
    if (c2b_global_args.starch_path)
        free(c2b_global_args.starch_path), c2b_global_args.starch_path = NULL;

    c2b_global_args.input_format_idx = UNDEFINED_FORMAT;
    c2b_global_args.sort_flag = kTrue;

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

    char *input_format = NULL;
    char *output_format = NULL;
    int client_long_index;
    int client_opt = getopt_long(argc,
                                 argv,
                                 c2b_client_opt_string,
                                 c2b_client_long_options,
                                 &client_long_index);

    opterr = 0; /* disable error reporting by GNU getopt */

    while (client_opt != -1) {
        switch (client_opt) {
            case 'i':
                input_format = malloc(strlen(optarg) + 1);
                if (!input_format) {
                    fprintf(stderr, "Error: Could not allocate space for input format argument\n");
                    exit(EXIT_FAILURE);
                }
                memcpy(input_format, optarg, strlen(optarg) + 1);
                c2b_global_args.input_format = c2b_to_lowercase(input_format);
                c2b_global_args.input_format_idx = c2b_to_input_format(c2b_global_args.input_format);
                free(input_format), input_format = NULL;
                break;
            case 'o':
                output_format = malloc(strlen(optarg) + 1);
                if (!output_format) {
                    fprintf(stderr, "Error: Could not allocate space for output format argument\n");
                    exit(EXIT_FAILURE);
                }
                memcpy(output_format, optarg, strlen(optarg) + 1);
                c2b_global_args.output_format = c2b_to_lowercase(output_format);
                c2b_global_args.output_format_idx = c2b_to_output_format(c2b_global_args.output_format);
                free(output_format), output_format = NULL;
                break;
            case 'd':
                c2b_global_args.sort_flag = kFalse;
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

    if ((!c2b_global_args.input_format) || (c2b_global_args.input_format_idx == UNDEFINED_FORMAT)) {
        c2b_print_usage(stderr);
        exit(EXIT_FAILURE);
    }

    if ((!c2b_global_args.output_format) || (c2b_global_args.output_format_idx == UNDEFINED_FORMAT)) {
        c2b_global_args.output_format = malloc(strlen(c2b_default_output_format) + 1);
        if (!c2b_global_args.output_format) {
            fprintf(stderr, "Error: Could not allocate space for output format copy\n");
            exit(EXIT_FAILURE);
        }
        memcpy(c2b_global_args.output_format, c2b_default_output_format, strlen(c2b_default_output_format) + 1);
        c2b_global_args.output_format_idx = c2b_to_output_format(c2b_global_args.output_format);
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
c2b_to_lowercase(const char *src)
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_to_lowercase() - enter ---\n");
#endif

    char *dest = NULL;
    char *p = NULL;

    if (!src)
        return dest;

    p = malloc(strlen(src) + 1);
    if (!p) {
        fprintf(stderr, "Error: Could not allocate space for lowercase translation\n");
        exit(EXIT_FAILURE);
    }
    memcpy(p, src, strlen(src) + 1);
    dest = p;
    for ( ; *p; ++p)
        *p = (*p >= 'A' && *p <= 'Z') ? (*p | 0x60) : *p;

#ifdef DEBUG
    fprintf(stderr, "--- c2b_to_lowercase() - exit  ---\n");
#endif
    return dest;
}

static c2b_format
c2b_to_input_format(const char *input_format)
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_to_input_format() - enter ---\n");
    fprintf(stderr, "--- c2b_to_input_format() - exit  ---\n");
#endif

    return
        (strcmp(input_format, "bam") == 0) ? BAM_FORMAT :
        (strcmp(input_format, "gff") == 0) ? GFF_FORMAT :
        (strcmp(input_format, "gtf") == 0) ? GTF_FORMAT :
        (strcmp(input_format, "psl") == 0) ? PSL_FORMAT :
        (strcmp(input_format, "sam") == 0) ? SAM_FORMAT :
        (strcmp(input_format, "vcf") == 0) ? VCF_FORMAT :
        (strcmp(input_format, "wig") == 0) ? WIG_FORMAT :
        UNDEFINED_FORMAT;
}

static c2b_format
c2b_to_output_format(const char *output_format)
{
#ifdef DEBUG
    fprintf(stderr, "--- c2b_to_output_format() - enter ---\n");
    fprintf(stderr, "--- c2b_to_output_format() - exit  ---\n");
#endif

    return
        (strcmp(output_format, "bed") == 0) ? BED_FORMAT :
        (strcmp(output_format, "starch") == 0) ? STARCH_FORMAT :
        UNDEFINED_FORMAT;
}
