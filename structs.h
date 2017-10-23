#ifndef STRUCTS_H
#define STRUCTS_H

#define BOOL char
#define TRUE 1
#define FALSE 0

#define MAX_FILENAME_SIZE 492
#define MAX_BLOCK_DATA_SIZE 508

/*
 *
 * Define page/sector structures here as well as utility structures
 * such as directory entries.
 *
 * Sectors/Pages are 512 bytes
 * The filesystem is 4 megabytes in size.
 * You will have 8K pages total.
 *
 */

struct AllocationTable {
	char * bitmap;
};

struct RootDir {
	struct Metadata * metadata;
};

struct Metadata {
	char filename[MAX_FILENAME_SIZE];
	unsigned int fileSize;
	unsigned int lastTimeUpdate;
	unsigned int lastDateUpdate;
	unsigned short fileAttrib;
	// Points to the '.' directory for the target dir if a directory
	// Points to the first block of a file is a file
	unsigned short blockNumber;
	// Points to the next file in the current directory
	short nextBlockNumber;
};

struct Block {
	int nextBlockNumber;
	char data[MAX_BLOCK_DATA_SIZE];
};


#endif
