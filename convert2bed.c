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
    char bam2sam_cmd[MAX_LINE_LENGTH_VALUE] = {0};
    const char *bam2sam_args = " view -h -";
    c2b_pipeline_stage bam2sam_stage;

    bam2sam_stage.pipeset = p;
    bam2sam_stage.line_functor = NULL;
    bam2sam_stage.src = -1; /* src is really stdin */
    bam2sam_stage.dest = 0;

    c2b_pipeline_stage sam2bed_unsorted_stage;
    void (*sam2bed_unsorted_line_functor)(char *, ssize_t *, char *, ssize_t) = c2b_line_convert_sam_to_bed_unsorted;

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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
    bam2sam_proc = c2b_popen4(bam2sam_cmd,
			      p->in[0],
			      p->out[0],
			      p->err[0],
			      POPEN4_FLAG_NONE);
#pragma GCC diagnostic pop

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
c2b_line_convert_sam_to_bed_unsorted(char *dest, ssize_t *dest_size, char *src, ssize_t src_size)
{
    /* 
       Scan the src buffer (all src_size bytes of it) to build a list of tab delimiter 
       offsets. Then write content to the dest buffer, using offsets taken from the reordered 
       tab-offset list to grab fields in the correct order.
    */

    ssize_t sam_field_offsets[MAX_FIELD_LENGTH_VALUE] = {-1};
    int sam_field_idx = 0;
    const char tab_delim = '\t';
    const char line_delim = '\n';
    const char sam_header_prefix = '@';
    ssize_t current_src_posn = -1;

    /* 
       Find offsets or process header line 
    */
    while (++current_src_posn < src_size) {
        if (src[current_src_posn] == sam_header_prefix) {
            if (!c2b_global_args.keep_header_flag) {
                /* skip header line */
                return;
            }
            else {
                /* copy header line to destination stream buffer */
                char src_header_line_str[MAX_LINE_LENGTH_VALUE] = {0};
                char dest_header_line_str[MAX_LINE_LENGTH_VALUE] = {0};
                memcpy(src_header_line_str, src, src_size);
                sprintf(dest_header_line_str, "%s\t%u\t%u\t%s\n", c2b_header_chr_name, c2b_global_args.header_line_idx, (c2b_global_args.header_line_idx + 1), src_header_line_str);
                memcpy(dest + *dest_size, dest_header_line_str, strlen(dest_header_line_str));
                *dest_size += strlen(dest_header_line_str);
                c2b_global_args.header_line_idx++;
                return;
            }
        }
        if ((src[current_src_posn] == tab_delim) || (src[current_src_posn] == line_delim)) {
            sam_field_offsets[sam_field_idx++] = current_src_posn;
        }
    }
    sam_field_offsets[sam_field_idx] = src_size;

    /*
       The SAM format is described at:

       http://samtools.github.io/hts-specs/SAMv1.pdf

       SAM fields are in the following ordering:

       Index   SAM field
       -------------------------------------------------------------------------
       0       QNAME
       1       FLAG
       2       RNAME
       3       POS
       4       MAPQ
       5       CIGAR
       6       RNEXT
       7       PNEXT
       8       TLEN
       9       SEQ
       10      QUAL
       11+     Optional alignment section fields (TAG:TYPE:VALUE)

       For SAM-formatted data, we use the mapping provided by BEDOPS convention described at: 

       http://bedops.readthedocs.org/en/latest/content/reference/file-management/conversion/sam2bed.html

       SAM field                 BED column index       BED field
       -------------------------------------------------------------------------
       RNAME                     1                      chromosome
       POS - 1                   2                      start
       POS + length(CIGAR) - 1   3                      stop
       QNAME                     4                      id
       FLAG                      5                      score
       16 & FLAG                 6                      strand

       If NOT (4 & FLAG) is true, then the read is mapped.

       The remaining SAM columns are mapped as-is, in same order, to adjacent BED columns:

       SAM field                 BED column index       BED field
       -------------------------------------------------------------------------
       MAPQ                      7                      -
       CIGAR                     8                      -
       RNEXT                     9                      -
       PNEXT                     10                     -
       TLEN                      11                     -
       SEQ                       12                     -
       QUAL                      13                     -

       If present:

       SAM field                 BED column index       BED field
       -------------------------------------------------------------------------
       Alignment fields          14+                    -
    */

    /* 
       Is read mapped? If not, and c2b_global_args.all_reads_flag is kFalse, we 
       skip over this line.
    */

    ssize_t flag_size = sam_field_offsets[1] - sam_field_offsets[0];
    char flag_src_str[MAX_FIELD_LENGTH_VALUE] = {0};
    memcpy(flag_src_str, src + sam_field_offsets[0] + 1, flag_size);
    int flag_val = (int) strtol(flag_src_str, NULL, 10);
    boolean is_mapped = (boolean) !(4 & flag_val);
    if ((!is_mapped) && (!c2b_global_args.all_reads_flag)) 
        return;

    /* Field 1 - RNAME */
    if (is_mapped) {
        ssize_t rname_size = sam_field_offsets[2] - sam_field_offsets[1];
        memcpy(dest + *dest_size, src + sam_field_offsets[1] + 1, rname_size);
        *dest_size += rname_size;
    }
    else {
        char unmapped_read_chr_str[MAX_FIELD_LENGTH_VALUE] = {0};
        memcpy(unmapped_read_chr_str, c2b_unmapped_read_chr_name, strlen(c2b_unmapped_read_chr_name));
        unmapped_read_chr_str[strlen(c2b_unmapped_read_chr_name)] = '\t';
        memcpy(dest + *dest_size, unmapped_read_chr_str, strlen(unmapped_read_chr_str));
        *dest_size += strlen(unmapped_read_chr_str);
    }

    /* Field 2 - POS - 1 */
    ssize_t pos_size = sam_field_offsets[3] - sam_field_offsets[2];
    char pos_src_str[MAX_FIELD_LENGTH_VALUE] = {0};
    memcpy(pos_src_str, src + sam_field_offsets[2] + 1, pos_size - 1);
    unsigned long long int pos_val = strtoull(pos_src_str, NULL, 10);
    char start_str[MAX_FIELD_LENGTH_VALUE] = {0};
    sprintf(start_str, "%llu\t", (is_mapped) ? pos_val - 1 : 0);
    memcpy(dest + *dest_size, start_str, strlen(start_str));
    *dest_size += strlen(start_str);

    /* Field 3 - POS + length(CIGAR) - 1 */
    ssize_t cigar_size = sam_field_offsets[5] - sam_field_offsets[4];
    ssize_t cigar_length = cigar_size - 1;
    char stop_str[MAX_FIELD_LENGTH_VALUE] = {0};
    sprintf(stop_str, "%llu\t", (is_mapped) ? pos_val + cigar_length - 1 : 1);
    memcpy(dest + *dest_size, stop_str, strlen(stop_str));
    *dest_size += strlen(stop_str);

    /* Field 4 - QNAME */
    ssize_t qname_size = sam_field_offsets[0] + 1;
    memcpy(dest + *dest_size, src, qname_size);
    *dest_size += qname_size;

    /* Field 5 - FLAG */
    memcpy(dest + *dest_size, src + sam_field_offsets[0] + 1, flag_size);
    *dest_size += flag_size;

    /* Field 6 - 16 & FLAG */
    int strand_val = 0x10 & flag_val;
    char strand_str[MAX_STRAND_LENGTH_VALUE] = {0};
    sprintf(strand_str, "%c\t", (strand_val == 0x10) ? '+' : '-');
    memcpy(dest + *dest_size, strand_str, strlen(strand_str));
    *dest_size += strlen(strand_str);

    /* Field 7 - MAPQ */
    ssize_t mapq_size = sam_field_offsets[4] - sam_field_offsets[3];
    memcpy(dest + *dest_size, src + sam_field_offsets[3] + 1, mapq_size);
    *dest_size += mapq_size;

    /* Field 8 - CIGAR */
    memcpy(dest + *dest_size, src + sam_field_offsets[4] + 1, cigar_size);
    *dest_size += cigar_size;

    /* Field 9 - RNEXT */
    ssize_t rnext_size = sam_field_offsets[6] - sam_field_offsets[5];
    memcpy(dest + *dest_size, src + sam_field_offsets[5] + 1, rnext_size);
    *dest_size += rnext_size;

    /* Field 10 - PNEXT */
    ssize_t pnext_size = sam_field_offsets[7] - sam_field_offsets[6];
    memcpy(dest + *dest_size, src + sam_field_offsets[6] + 1, pnext_size);
    *dest_size += pnext_size;

    /* Field 11 - TLEN */
    ssize_t tlen_size = sam_field_offsets[8] - sam_field_offsets[7];
    memcpy(dest + *dest_size, src + sam_field_offsets[7] + 1, tlen_size);
    *dest_size += tlen_size;

    /* Field 12 - SEQ */
    ssize_t seq_size = sam_field_offsets[9] - sam_field_offsets[8];
    memcpy(dest + *dest_size, src + sam_field_offsets[8] + 1, seq_size);
    *dest_size += seq_size;

    /* Field 13 - QUAL */
    ssize_t qual_size = sam_field_offsets[10] - sam_field_offsets[9];
    memcpy(dest + *dest_size, src + sam_field_offsets[9] + 1, qual_size);
    *dest_size += qual_size;

    /* Field 14+ - Optional fields */
    if (sam_field_offsets[11] == -1)
        return;

    int field_idx;
    for (field_idx = 11; field_idx <= sam_field_idx; field_idx++) {
        ssize_t opt_size = sam_field_offsets[field_idx] - sam_field_offsets[field_idx - 1];
        memcpy(dest + *dest_size, src + sam_field_offsets[field_idx - 1] + 1, opt_size);
        *dest_size += opt_size;
    }
}

static void *
c2b_read_bytes_from_stdin(void *arg)
{
    c2b_pipeline_stage *stage = (c2b_pipeline_stage *) arg;
    c2b_pipeset *pipes = stage->pipeset;
    char buffer[MAX_LINE_LENGTH_VALUE];
    ssize_t bytes_read;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    while ((bytes_read = read(STDIN_FILENO, buffer, MAX_LINE_LENGTH_VALUE)) > 0) {
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
    char *src_buffer = NULL;
    ssize_t src_buffer_size = MAX_LINE_LENGTH_VALUE;
    ssize_t src_bytes_read = 0;
    ssize_t remainder_length = 0;
    ssize_t remainder_offset = 0;
    char line_delim = '\n';
    ssize_t lines_offset = 0;
    ssize_t start_offset = 0;
    ssize_t end_offset = 0;
    char *dest_buffer = NULL;
    ssize_t dest_buffer_size = MAX_LINE_LENGTH_VALUE * MAX_LINES_VALUE;
    ssize_t dest_bytes_written = 0;
    void (*line_functor)(char *, ssize_t *, char *, ssize_t) = stage->line_functor;

    /* 
       We read from the src out pipe, then write to the dest in pipe 
    */
    
    src_buffer = malloc(src_buffer_size);
    if (!src_buffer) {
        fprintf(stderr, "Error: Could not allocate space for intermediate source buffer.\n");
        exit(EXIT_FAILURE);
    }

    dest_buffer = malloc(dest_buffer_size);
    if (!dest_buffer) {
        fprintf(stderr, "Error: Could not allocate space for intermediate destination buffer.\n");
        exit(EXIT_FAILURE);
    }

    while ((src_bytes_read = read(pipes->out[stage->src][PIPE_READ],
				  src_buffer + remainder_length,
				  src_buffer_size - remainder_length)) > 0) {

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

           If remainder_offset is -1 and we have read src_buffer_size bytes, then we know there 
           are no newlines anywhere in the src_buffer and so we terminate early with an error state.
           This would suggest either src_buffer_size is too small to hold a whole intermediate 
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
           and read up to, at most, (src_buffer_size - remainder_length) bytes:
           
           read(byte_source, 
                src_buffer + remainder_length,
                src_buffer_size - remainder_length)

           Second and subsequent reads should reduce the maximum number of src_bytes_read from 
           src_buffer_size to something smaller.

           Note: We should look into doing a final pass through the line_functor, once we grab the 
           last buffer, after the last read().
        */

        c2b_memrchr_offset(&remainder_offset, src_buffer, src_buffer_size, src_bytes_read + remainder_length, line_delim);

        if (remainder_offset == -1) {
            if (src_bytes_read + remainder_length == src_buffer_size) {
                fprintf(stderr, "Error: Could not find newline in intermediate buffer; check input\n");
                exit(EXIT_FAILURE);
            }
            remainder_offset = 0;
        }

        /* 
           We next want to process bytes from index [0] to index [remainder_offset - 1] for all
           lines contained within. We basically build a buffer containing all translated 
           lines to write downstream.
        */

        lines_offset = 0;
        start_offset = 0;
        dest_bytes_written = 0;
        while (lines_offset < remainder_offset) {
            if (src_buffer[lines_offset] == line_delim) {
                end_offset = lines_offset;
                /* for a given line from src, we write dest_bytes_written number of bytes to dest_buffer (plus written offset) */
                (*line_functor)(dest_buffer, &dest_bytes_written, src_buffer + start_offset, end_offset - start_offset);
                start_offset = end_offset + 1;
            }
            lines_offset++;            
        }
        
        /* 
           We have filled up dest_buffer with translated bytes (dest_bytes_written of them)
           and can now write() this buffer to the in-pipe of the destination stage
        */
        
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
        write(pipes->in[stage->dest][PIPE_WRITE], dest_buffer, dest_bytes_written);
#pragma GCC diagnostic pop

        remainder_length = src_bytes_read + remainder_length - remainder_offset;
        memcpy(src_buffer, src_buffer + remainder_offset, remainder_length);
    }

    close(pipes->in[stage->dest][PIPE_WRITE]);

    if (src_buffer) 
        free(src_buffer), src_buffer = NULL;

    if (dest_buffer)
        free(dest_buffer), dest_buffer = NULL;

    pthread_exit(NULL);
}

static void *
c2b_write_bytes_to_stdout(void *arg)
{
    c2b_pipeline_stage *stage = (c2b_pipeline_stage *) arg;
    c2b_pipeset *pipes = stage->pipeset;
    char buffer[MAX_LINE_LENGTH_VALUE];
    ssize_t bytes_read;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    while ((bytes_read = read(pipes->in[stage->src][PIPE_READ], buffer, MAX_LINE_LENGTH_VALUE)) > 0) {
        write(STDOUT_FILENO, buffer, bytes_read);
    }
#pragma GCC diagnostic pop

    pthread_exit(NULL);
}

static void
c2b_memrchr_offset(ssize_t *offset, char *buf, ssize_t buf_size, ssize_t len, char delim)
{
    ssize_t left = len;
    
    *offset = -1;
    if (len > buf_size)
        return;

    while (left > 0) {
        if (buf[left - 1] == delim) {
            *offset = left;
            return;
        }
        left--;
    }
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
    c2b_global_args.all_reads_flag = kFalse;
    c2b_global_args.keep_header_flag = kFalse;
    c2b_global_args.header_line_idx = 0U;

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
    c2b_global_args.all_reads_flag = kFalse;
    c2b_global_args.keep_header_flag = kFalse;
    c2b_global_args.header_line_idx = 0U;

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
        switch (client_opt) 
            {
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
            case 'a':
                c2b_global_args.all_reads_flag = kTrue;
                break;
            case 'k':
                c2b_global_args.keep_header_flag = kTrue;
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
