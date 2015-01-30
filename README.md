convert2bed
===========

The `convert2bed` tool converts common binary and text genomic formats ([BAM](http://samtools.github.io/hts-specs/SAMv1.pdf), [GFF](http://www.sequenceontology.org/gff3.shtml), [GTF](http://mblab.wustl.edu/GTF22.html), [GVF](http://www.sequenceontology.org/resources/gvf.html#summary), [PSL](http://genome.ucsc.edu/FAQ/FAQformat.html#format2), [RepeatMasker annotation output](http://www.repeatmasker.org/webrepeatmaskerhelp.html), [SAM](http://samtools.github.io/hts-specs/SAMv1.pdf), [VCF](http://samtools.github.io/hts-specs/VCFv4.2.pdf) and [WIG](http://genome.ucsc.edu/goldenpath/help/wiggle.html)) to unsorted or [sorted, extended BED](http://bedops.readthedocs.org/en/latest/content/reference/file-management/sorting/sort-bed.html) or [BEDOPS Starch](http://bedops.readthedocs.org/en/latest/content/reference/file-management/compression/starch.html) (compressed BED) with additional per-format options. 

Convenience wrapper `bash` scripts are provided for each format that convert standard input to unsorted or sorted BED, or to BEDOPS Starch (compressed BED). Scripts expose format-specific ``convert2bed`` options. 

We also provide ``bam2bed_sge``, ``bam2bed_gnuParallel``, ``bam2starch_sge`` and ``bam2starch_gnuParallel`` convenience scripts, which parallelize the conversion of indexed BAM to BED or to BEDOPS Starch via a [Sun Grid Engine](http://en.wikipedia.org/wiki/Oracle_Grid_Engine)-based computational cluster or local [GNU Parallel](http://en.wikipedia.org/wiki/GNU_parallel) installation.

Installation
------------

The following compiles `convert2bed` and copies the binary and wrappers to `/usr/local/bin`:

    $ make && make install

Usage
-----

Generally, to convert data in format `xyz` to sorted BED:

    $ convert2bed -i xyz < input.xyz > output.bed

Add the `-o starch` option to write a BEDOPS Starch file, which stores compressed BED data and feature metadata:

    $ convert2bed -i xyz -o starch < input.xyz > output.starch

Wrappers are available for each of the supported formats to convert to BED or Starch, *e.g.*:

    $ bam2bed < reads.bam > reads.bed
    $ bam2starch < reads.bam > reads.starch

Format-specific options are available for each wrapper; use `--help` with a wrapper script or `--help-bam`, `--help-gff` etc. with `convert2bed` to get a format-specific description of the conversion procedure and options.

Dependencies
------------

This tool is dependent upon [`samtools`](https://github.com/samtools/samtools) to handle BAM conversion, and BEDOPS [`sort-bed`](http://bedops.readthedocs.org/en/latest/content/reference/file-management/sorting/sort-bed.html) and [`starch`](http://bedops.readthedocs.org/en/latest/content/reference/file-management/compression/starch.html) to generate sorted BED and Starch (compressed BED) output. The directory containing these binaries should be present in the end user's `PATH` environment variable. 

If the `samtools` binary is not present, BAM conversion will fail. If the `sort-bed` binary is not installed, all format conversions will fail with default sort rules applied. If the `starch` binary is not installed, the `starch` output format option will be unavailable.