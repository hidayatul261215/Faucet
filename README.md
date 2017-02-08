<!-- # Getting Recycler
You can download Recycler [here](https://github.com/Shamir-Lab/Recycler/releases/download/Recycler-v0.6/Recycler-0.6.zip) or clone it via the link below. In case you download the zip, unzip the file before following the instructions below (ignoring the 'git clone' line)
 -->
# Getting Faucet
    git clone https://github.com/rozovr/Faucet.git
    cd Faucet/readScanAssembler
	make depend
	make    
 
# Introduction
Faucet is a streaming algorithm for constructing de novo assembly graphs from metagenomes. 


# Running Faucet on reads that are on a local hard drive
Example usage:
./mink -read_load_file interlaced_reads.fq -read_scan_file interlaced_reads.fq -size_kmer 31 -max_read_length 100 -estimated_kmers 1000000000 -singletons 200000000 -file_prefix faucet_outputs --fastq --paired_ends

The above command takes as input the file interlaced_reads.fq (where entries alternate between mates 1 and 2 of a paired end library), and the input format is fastq. Faucet does not accept separate mate files, but can accept fasta format and files composed of read sequences alone.


# Requirements
Faucet was implemented in C++ 11, so requires a compiler that is not too ancient to support it, and has been tested only on Linux so far. 

# Detailed usage

Usage:
./mink -read_load_file <filename> -read_scan_file <filename> -size_kmer <k> -max_read_length <length> -estimated_kmers <num_kmers> -singletons <num_kmers> -file_prefix <prefix>
Optional arguments: --fastq -max_spacer_dist <dist> -fp rate <rate> -j <int> --two_hash -bloom_file <filename> -junctions_file <filename> --paired_ends --no_cleaning

### required arguments:
 
	-read_load_file <filename>, a file name string 
	-read_scan_file <filename> , a file name string
	-size_kmer <k> , and odd integer <= 31
	-max_read_length <length>, the longest read length in the data (e.g., if the reads were trimmed to different sizes)
	-estimated_kmers <num_kmers> 
	-singletons <num_kmers> 
	-file_prefix <prefix>, the desired prefix string or directory path for output files 
 
	we recommend applying <a href="https://github.com/bcgsc/ntCard">ntCard</a> to extract the number estimated k-mers (F0) and singletons (f1) in the dataset.

<!-- ### optional arguments:
	--fastq 
	-max_spacer_dist <dist> 
	-fp <rate> 
	-j <int> 
	--two_hash 
	-bloom_file <filename> 
	-junctions_file <filename> 
	--paired_ends --no_cleaning
 -->
<!-- # <a name="bam-prep">Preparing the BAM input: -->

<!-- # Outputs: -->


