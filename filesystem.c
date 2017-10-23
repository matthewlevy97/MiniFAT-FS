#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include "support.h"
#include "structs.h"
#include "filesystem.h"

unsigned char * allocTable;
struct RootDir * rootDir;
char * map;

short * currentDirBlockStack;
short currentDirBlock;

/* Start of helper functions */
void writeFS(char * filename, int amount, char * data) {
	printf("Writing: <%s> to <%s> of <%d> bytes\n", data, filename, amount);

	// First check to see if a file with the specified filename exists
	struct Metadata * metadata = NULL;
	int next = currentDirBlockStack[currentDirBlock];
	int curr = 0;
	BOOL found = FALSE;
	do {
		metadata = (struct Metadata *)getBlock(next);

		// Check to see if the current metadata block has the same filename as the target
		if(!strncmp(metadata->filename, filename, MAX_FILENAME_SIZE)) {
			// They match!
			found = TRUE;
			break;
		}

		// Move to the next metadata block
		next = metadata->nextBlockNumber;
		curr = metadata->blockNumber;
		free(metadata);
	} while(next != -1);

	// The filename was not found, so we must create a new file
	if(!found) {
		/* Create a file */
		struct Metadata f;
		strncpy(f.filename, filename, MAX_FILENAME_SIZE);
		setFile(&f);

		// Save block
		int blockNumber = createBlock();
		saveBlock(&f, blockNumber);
		
		// Link to block
		struct Metadata * temp = NULL;
		int temp2 = currentDirBlockStack[currentDirBlock];
		do {
			temp = (struct Metadata *)getBlock(temp2);
			if(temp->nextBlockNumber == -1) {
				break;
			}
			temp2 = temp->nextBlockNumber;
			free(temp);
		} while(temp != NULL);

		temp->nextBlockNumber = blockNumber;
		saveBlock(temp, temp2);
		free(temp);
	}

	// Let's get the first block we can write to
	struct Block * block = NULL;
	int nextDataBlock = curr;

	// Itterate through every block we have saving data
	while(amount > 0) {
		block = (struct Block *)getBlock(nextDataBlock);

		// Start writing to the file
		int i, j;
		for(i = 0, j = strlen(data); i < MAX_BLOCK_DATA_SIZE && i < j && amount; i++) {
			block->data[i] = data[i];
			amount--;
		}

		// Save the block
		saveBlock(block, nextDataBlock);

		// Exit the loop if we are out of space
		if(block->nextBlockNumber <= 0) {
			break;
		}

		// Move to the next data block
		nextDataBlock = block->nextBlockNumber;
		free(block);
	}

	// We left the while loop early, so we must add more space
	if(amount > 0) {
		// This is nested to allow for the save/free below the while loop
		while(amount > 0) {
			// Allocate a new block
			next = createBlock();
			
			// Append its blockNumber to the previous block in the linked list
			block->nextBlockNumber = next;
			
			// Save the previous block
			saveBlock(block, nextDataBlock);
			
			// Free the previous block
			free(block);

			// Get the new block
			block = (struct Block *)getBlock(next);

			// Write data to the new block
			int i, j;
			for(i = 0, j = strlen(data); i < MAX_BLOCK_DATA_SIZE && i < j && amount; i++) {
				block->data[i] = data[i];
				amount--;
			}

			// Save the block number for the just created block
			nextDataBlock = next;
		}

		// Save the new block
		saveBlock(block, nextDataBlock);
		free(block);
	}
}

void dump(FILE * fd, int pageNumber) {
	char * page = (char*)getBlock(pageNumber);
	int i, rowLen, rowCt;
	for(i = 0, rowLen = 0, rowCt = 0; i < PAGE_SIZE; i++) {
		printf("%2hhx ", page[i]);
		rowLen++;

		if(rowLen >= 16) {
			rowLen = 0;
			rowCt++;
			printf("   ");
		}

		if(rowCt >= 2) {
			printf("\n");
			rowCt = 0;
		}
	}

	free(page);
}

void getpages(char * filename) {
	// Invalidate all data blocks
	struct Metadata * metadata = NULL;
	int next = currentDirBlockStack[currentDirBlock];
	BOOL found = FALSE, file = FALSE;
	do {
		// Get the next block
		metadata = (struct Metadata *)getBlock(next);

		if(metadata->filename[0] == DIRECTORY) {
			if(!strncmp(metadata->filename + 1, filename, MAX_FILENAME_SIZE)) {
				found = TRUE;
				break;
			}
		} else {
			if(!strncmp(metadata->filename, filename, MAX_FILENAME_SIZE)) {
				found = TRUE;
				file = TRUE;
				break;
			}
		}

		// Move to the next block
		next = metadata->nextBlockNumber;
		free(metadata);
	} while(next != -1);

	if(found) {
		// Print the found file's block number
		printf("%d", next);
		next = metadata->blockNumber;
		free(metadata);

		if(file) {
			// The target is a file
			struct Block * block = NULL;
			do {
				// Get the next block
				block = (struct Block *)getBlock(next);

				printf(", %d", next);

				// Move to the next block
				next = block->nextBlockNumber;
				free(block);
			} while(next > 0);
		} else {
			// The target is a directory
			do {
				// Get the next block
				metadata = (struct Metadata *)getBlock(next);

				printf(", %d", next);

				// Move to the next block
				next = metadata->nextBlockNumber;
				free(metadata);
			} while(next != -1);
		}

		printf("\n");
	} else {
		printf("File not found.\n");
	}
}

void usage() {
	printf("File System\t\tSize\tUsed\tAvailable\n");

	int i, used, fsUsed, available;
	char c;

	// Init variables
	used = 0;
	fsUsed = ALLOCATION_BITMAP_PAGES;
	available = 0;
	
	// Get files used in root sector
	for(i = 0; i < ROOT_SECTOR_ENTRIES; i++) {
		c = rootDir->metadata[i].filename[0];
		if(c != 0 && c != FILE_DELETED) {
			++fsUsed;
		}
	}
	fsUsed *= PAGE_SIZE;

	available = (ALLOCATION_BITMAP_PAGES + ROOT_SECTOR_ENTRIES) * PAGE_SIZE;
	printf("%s\t\t%d\t%d\t%d\n", "Root Sector", available, fsUsed, available - fsUsed);

	// Get files used in storage
	for(i = 0; i < ALLOCATION_BITMAP_PAGES * PAGE_SIZE; i++) {
		if(allocTable[i]) {
			++used;
		}
	}
	used *= PAGE_SIZE;

	available = FILESIZE - fsUsed - used;
	printf("%s\t\t\t%d\t%d\t%d\n", "Files", FILESIZE - fsUsed, used, available);
}

void pwd() {
	/*
	Used to save the filenames as they are resolved in reverse order
	*/
	char output[currentDirBlock + 1][MAX_FILENAME_SIZE];
	memset(output, 0, MAX_FILENAME_SIZE * (currentDirBlock + 1) - 1);

	// Loop to the bottom of the directory stack
	struct Metadata * data = NULL;
	int i, next;
	for(i = currentDirBlock; i > 0; i--) {
		// Itterate through the directory above current to find the handle pointing here
		next = currentDirBlockStack[i - 1];
		do {
			// And get the next one
			data = (struct Metadata *)getBlock(next);

			// If the matching block is found save its name
			if(data->blockNumber == currentDirBlockStack[i]) {
				strncpy(output[i], data->filename + 1, MAX_FILENAME_SIZE - 1);
				break;
			}
			// Clean up this block
			next = data->nextBlockNumber;
			free(data);
		} while(next != -1);
	}

	// Print the correct output
	for(i = 0; i <= currentDirBlock; i++) {
		printf("%s/", output[i]);
	}
	printf("\n");
}

void cd(char * path) {
	// Itterate through linked list array to find a matching directory name
	int block = currentDirBlockStack[currentDirBlock];
	char * temp = (char*) malloc(MAX_FILENAME_SIZE + 1);
	struct Metadata * data = NULL;
	BOOL found = FALSE;
	do {
		data = (struct Metadata *)getBlock(block);
		
		memset(temp, 0, MAX_FILENAME_SIZE);
		strncpy(temp, data->filename, MAX_FILENAME_SIZE - 1);

		if (temp[0] == DIRECTORY) {
			if(!strcmp(temp + 1, path)) {

				/* Modify the current directory stack*/
				if(*(temp + 1) == '.') {
					if(*(temp + 2) == '.') {
						// Go back a directory
						if(currentDirBlock > 0) {
							currentDirBlockStack[currentDirBlock--] = -1;
						}
					}
				} else if(currentDirBlock + 1 < MAX_DIRECTORY_DEPTH) {
					currentDirBlockStack[++currentDirBlock] = data->blockNumber;
				}

				block = data->blockNumber;
				found = TRUE;
			}
		}

		if(!found) {
			block = data->nextBlockNumber;
		}

		free(data);
	} while(block != -1 && !found);	
	free(temp);

	if(!found) {
		printf("No such directory\n");
	}
}

void ls() {
	// Get the root for the current directory
	int next = currentDirBlockStack[currentDirBlock];

	struct Metadata * data = NULL;
	do {
		data = (struct Metadata *)getBlock(next);
		if(data->filename[0] != FILE_DELETED) {
			// Print if its a directory or not
			if(data->filename[0] == DIRECTORY) {
				printf("d %d %s\n", data->fileSize, data->filename + 1);
			} else {
				printf("f %d %s\n", data->fileSize - PAGE_SIZE, data->filename);
			}
		}

		next = data->nextBlockNumber;
		free(data);
	} while(next != -1);
}

void mkdir(char * dirname) {
	// Check to see if the current directory name already exists here
	struct Metadata * temp = NULL;
	struct Metadata * previous = NULL;
	int prevBlockNumber = -1;
	int next = currentDirBlockStack[currentDirBlock];
	do {
		temp = (struct Metadata *)getBlock(next);

		if(!strcmp(temp->filename + 1, dirname)) {
			printf("Directory already exists.\n");
			free(temp);
			return;
		}

		next = temp->nextBlockNumber;
		if(previous != NULL) {
			prevBlockNumber = previous->nextBlockNumber;
		}
		free(previous);
		previous = temp;
	} while(next != -1);

	// Create directory
	struct Metadata * dir = (struct Metadata *)malloc(PAGE_SIZE + 1);
	memset(dir, 0, PAGE_SIZE);
	strncpy(dir->filename, dirname, MAX_FILENAME_SIZE - 1);
	setDirectory(dir);
	
	// Get the current directory '.' file
	dir->blockNumber = currentDirBlockStack[currentDirBlock];

	/* Create internal directory structure and write to disk */
	createDirectoryStruct(dir);
	
	if(currentDirBlock) {
		/* Must save outside of the root block */
		int blockSpace = createBlock();

		if(blockSpace >= 0) {
			dir->nextBlockNumber = previous->nextBlockNumber;
			saveBlock(dir, blockSpace);
			
			/* Correct linked list */
			previous->nextBlockNumber = blockSpace;
			saveBlock(previous, prevBlockNumber);
		} else {
			printf("Could not create new directory. Not enough space.\n");
		}
	} else {
		/* Add directory to root directory listing */
		saveMetadataToRootBlock(*dir);
	}
	free(dir);
	free(previous);
}

void cat(char * filename) {
	// Do not let the user cat a directory
	if(filename[0] == DIRECTORY) {
		printf("Cannot cat a directory.\n");
		return;
	}

	struct Metadata * file = NULL;
	int next = currentDirBlockStack[currentDirBlock];
	BOOL found = FALSE;
	do {
		file = (struct Metadata *)getBlock(next);

		if(!strcmp(file->filename, filename)) {
			found = TRUE;
			break;
		}

		next = file->nextBlockNumber;
		free(file);
	} while(next != -1);

	if(found) {
		// Get the file contents' first block number
		int blockNumber = file->blockNumber;
		free(file);

		// Itterate and output each block until no more to be printed
		struct Block * block = NULL;
		do {
			block = (struct Block *)getBlock(blockNumber);

			printf("%s", block->data);

			blockNumber = block->nextBlockNumber;
			free(block);
		} while(blockNumber != 0);
	}
}

/*
Had to rename due to conflicting function defintions
*/
void rmdir2(char * dirName) {
	// Do not allow the user to remove the '.' and '..' files
	if(!strcmp(".", dirName) || !strcmp("..", dirName)) {
		printf("No directory found with the given name\n");
		return;
	}

	struct Metadata * dir = NULL;
	struct Metadata * previous = NULL;
	int next = currentDirBlockStack[currentDirBlock];
	int previousBlockNumber = -1;
	BOOL found = FALSE;
	do {
		dir = (struct Metadata *)getBlock(next);
		if(dir->filename[0] == DIRECTORY && !strcmp(dir->filename + 1, dirName)) {
			found = TRUE;
			break;
		}

		next = dir->nextBlockNumber;
		if(previous != NULL) {
			previousBlockNumber = previous->nextBlockNumber;
		}
		free(previous);

		previous = dir;
	} while(next != -1);

	if(found) {
		// Check to see if the directory is empty
		struct Metadata * temp = NULL;
		int temp2 = dir->blockNumber;
		int counter = 0;
		do {
			temp = (struct Metadata *)getBlock(temp2);
			temp2 = temp->nextBlockNumber;
			free(temp);
			counter++;
		} while(temp2 != -1 && counter < 3); // Short circuit if loop counter finds too many files
											 //    (Anything besides '.' and '..')
		
		if(counter <= 2) {
			// Set the file to be deleted
			dir->filename[0] = FILE_DELETED;

			// Invalidate the '.' and '..' blocks
			temp = (struct Metadata *)getBlock(dir->blockNumber);
			temp2 = temp->nextBlockNumber;
			
			invalidateBlock(dir->blockNumber);
			invalidateBlock(temp2);
			free(temp);

			// For removing a file not in the root sector
			if(previous->nextBlockNumber >= ALLOCATION_BITMAP_PAGES + ROOT_SECTOR_ENTRIES) {
				saveBlock(dir, previous->nextBlockNumber);
				invalidateBlock(previous->nextBlockNumber);

				// Remove the deleted block from the linked list
				previous->nextBlockNumber = dir->nextBlockNumber;
				saveBlock(previous, previousBlockNumber);
			} else {
				// If the file we are deleting belongs to the root sector,
				//  we must handle it slightly differently
				int blockNumber = previous->nextBlockNumber - ALLOCATION_BITMAP_PAGES;

				/* Correct linked list */
				previous->nextBlockNumber = dir->nextBlockNumber;
				rootDir->metadata[blockNumber] = *dir;
				rootDir->metadata[blockNumber - 1] = *previous;

				syncFilesystem();
			}
		} else {
			printf("Directory not empty.\n");
		}

		free(dir);
	} else {
		printf("Cannot find directory with provided name.\n");
	}

	free(previous);
}

void rm(char * filename) {
	struct Metadata * file = NULL;
	struct Metadata * previous = NULL;
	int next = currentDirBlockStack[currentDirBlock];
	int previousBlockNumber = -1;
	BOOL found = FALSE;
	do {
		file = (struct Metadata *)getBlock(next);

		if(file->filename[0] != DIRECTORY && !strcmp(file->filename, filename)) {
			found = TRUE;
			break;
		}

		next = file->nextBlockNumber;
		if(previous != NULL) {
			previousBlockNumber = previous->nextBlockNumber;
		}
		free(previous);

		previous = file;
	} while(next != -1);

	if(found) {
		// Invalidate all data blocks
		struct Block * temp = NULL;
		int nextDataBlock = file->blockNumber;
		do {
			// Get the next block
			temp = (struct Block *)getBlock(nextDataBlock);

			// Invalidate the data block
			invalidateBlock(nextDataBlock);

			// Move to the next block
			nextDataBlock = temp->nextBlockNumber;
			free(temp);
		} while(nextDataBlock > 0);

		// Delete file handle
		if(previous->nextBlockNumber >= ALLOCATION_BITMAP_PAGES + ROOT_SECTOR_ENTRIES) {
			saveBlock(file, previous->nextBlockNumber);
			invalidateBlock(previous->nextBlockNumber);

			// Remove the deleted block from the linked list
			previous->nextBlockNumber = file->nextBlockNumber;
			saveBlock(previous, previousBlockNumber);
		} else {
			// Must delete it differently because this file is in the root block
			int blockNumber = previous->nextBlockNumber - ALLOCATION_BITMAP_PAGES;

			/* Correct linked list */
			previous->nextBlockNumber = file->nextBlockNumber;
			memcpy(&(rootDir->metadata[blockNumber]), file, sizeof(struct Metadata));
			memcpy(&(rootDir->metadata[blockNumber - 1]), previous, sizeof(struct Metadata));
		}
		free(file);
	} else {
		printf("Cannot find file with provided name.\n");
	}

	free(previous);
}

void rmForce(char * filename) {
	// We have no idea if we are deleting a file or a directory
	struct Metadata * file = NULL;
	struct Metadata * previous = NULL;
	int next = currentDirBlockStack[currentDirBlock];
	BOOL found = FALSE;
	do {
		file = (struct Metadata *)getBlock(next);

		if(file->filename[0] == DIRECTORY) {
			// If the target is a directory, we must do other stuff to remove it
			if(!strcmp(file->filename + 1, filename)) {
				found = TRUE;
				break;
			}
		} else if(!strcmp(file->filename, filename)) {
			// If the target is a file, just delete it and be done
			rm(filename);
			free(file);
			free(previous);
			return;
		}

		next = file->nextBlockNumber;
		free(previous);

		previous = file;
	} while(next != -1);

	if(found) {
		// To remove a directory, remove every file and directory inside of it nested
		clearDirectory(previous);
	}

	free(previous);
}

void clearDirectory(struct Metadata * metadata) {
	// Itterate through every block inside the directory
	struct Metadata * meta = NULL;
	int next = metadata->blockNumber;
	do {
		meta = (struct Metadata *)getBlock(next);
		
		if(meta->filename[0] == DIRECTORY) {
			// Used to prevent including the '.' and '..' directories
			if(!(meta->fileAttrib & SUBDIRECTORY)) {
				//printf("Directory: %s\n", meta->filename);

				// In order to correctly do this, we must add to the currentDirBlockStack
				if(currentDirBlock + 1 < MAX_DIRECTORY_DEPTH) {
					currentDirBlockStack[++currentDirBlock] = meta->blockNumber;

					// Remove that directory recursively
					struct Metadata * temp = (struct Metadata *)getBlock(meta->blockNumber);
					clearDirectory(temp);
					free(temp);

					// Pop the top off of the currentDirBlockStack now that we returned
					currentDirBlockStack[currentDirBlock--] = -1;
				} else {
					printf("Max depth achieved.\n");
				}

				// Delete the directory
				rmdir2(meta->filename + 1);
			}
		} else {
			//printf("File: %s\n", meta->filename);
			rm(meta->filename);
		}

		next = meta->nextBlockNumber;
		free(meta);
	} while(next != -1);
}

void scandisk() {
	unsigned char * allocTable2 = (unsigned char *) malloc(ALLOCATION_BITMAP_PAGES * PAGE_SIZE + 1);
	memcpy(allocTable2, allocTable, ALLOCATION_BITMAP_PAGES * PAGE_SIZE);

	struct Metadata * metadata = NULL;
	int i, j;
	for(i = ALLOCATION_BITMAP_PAGES, j = FILESIZE / PAGE_SIZE - ALLOCATION_BITMAP_PAGES; i < j; i++) {
		metadata = (struct Metadata *)getBlock(i);

		if(metadata->fileAttrib == FILE_ATTRIB || metadata->fileAttrib == DIRECTORY_ATTRIB) {
			// We have metadata
			int k = metadata->nextBlockNumber;
			int l = metadata->blockNumber;
			
			if(k >= 0) {
				allocTable2[i]++;
			}
			if(l >= 0) {
				allocTable2[i]++;
			}
		}

		free(metadata);
	}

	for(i = ALLOCATION_BITMAP_PAGES; i < j; i++) {
		if(allocTable2[i] <= 1) {
			invalidateBlock(i);
		}
	}
}
/* End of helper functions */

/* Start of Debugging code*/
#ifdef DEBUG_MODE
// Print metadata information
void printMetadata(struct Metadata meta) {
	printf("Filename:\t%s\n", meta.filename);
	printf("Block Number:\t%d\n", meta.blockNumber);
	printf("Next Block:\t%d\n", meta.nextBlockNumber);
	printf("Time:\t\t%d\n", meta.lastTimeUpdate);
	printf("Date:\t\t%d\n", meta.lastDateUpdate);
	printf("File Attrib:\t%d\n", meta.fileAttrib);
	printf("File Size:\t%d\n", meta.fileSize);
	
	printf("Hour: %d\n", GET_HOUR(meta.lastTimeUpdate));
	printf("Minute: %d\n", GET_MINUTE(meta.lastTimeUpdate));
	printf("Second: %d\n", GET_SECOND(meta.lastTimeUpdate));
	printf("Year: %d\n", GET_YEAR(meta.lastDateUpdate));
	printf("Month: %d\n", GET_MONTH(meta.lastDateUpdate));
	printf("Day: %d\n", GET_DAY(meta.lastDateUpdate));
	printf("\n------\n");
}

// Print all files in a directory
void treePrint(struct Metadata metadata) {
	printMetadata(metadata);
	struct Metadata * temp = (struct Metadata *)getBlock(metadata.nextBlockNumber);
	if(temp != NULL) {
		treePrint(*temp);
	}
	free(temp);

}
#endif
/* End of Debugging code */

int saveMetadataToRootBlock(struct Metadata metadata) {
	int i;
	BOOL found = FALSE, ret = FALSE;
	for(i = 0; i < ROOT_SECTOR_ENTRIES; i++) {
		struct Metadata meta = rootDir->metadata[i];

		if (meta.filename[0] == 0 || meta.filename[0] == FILE_DELETED) {
			found = TRUE;
			break;
		}
	}
	
	if(found) {
		// Found an open space
		metadata.nextBlockNumber = (rootDir->metadata[i - 1]).nextBlockNumber;
		rootDir->metadata[i] = metadata;
		/* Correct linked list */
		(rootDir->metadata[i - 1]).nextBlockNumber = ALLOCATION_BITMAP_PAGES + i;

		ret = TRUE;
	}
#ifdef DEBUG_MODE
	else {
		printf("ERROR: saveMetadataToRootBlock block not found.\n");
	}
#endif
	return ret;
}

/**
 * Get a pointer to block, can be cast to either metadata or block structures
 */
void * getBlock(int blockNumber) {
	if (blockNumber <= 0) {
		return NULL;
	}

	char * block = (char *) malloc(PAGE_SIZE);
	memcpy(block, map + blockNumber * PAGE_SIZE, PAGE_SIZE);
	return (void*)block;
}

/**
 * Get the next valid block number
 */
int createBlock() {
	int i;
	for(i = 0; i < ALLOCATION_BITMAP_PAGES * PAGE_SIZE; i++) {
		if (!allocTable[i]) {
			allocTable[i] = 1;
			return i + ALLOCATION_BITMAP_PAGES + ROOT_SECTOR_ENTRIES;
		}
	}
	return -1;
}

/**
 * Saves a block back to the file system
 */
void saveBlock(void * b, int blockNumber) {
	if(blockNumber > 0) {
		memcpy(map + blockNumber * PAGE_SIZE, b, PAGE_SIZE);
	}
}

/**
 * Invalidates a block to allow it to be overwritten
 */
BOOL invalidateBlock(int blockNumber) {
	blockNumber -= (ALLOCATION_BITMAP_PAGES + ROOT_SECTOR_ENTRIES);
	if (blockNumber >= 0 && blockNumber < ALLOCATION_BITMAP_PAGES * PAGE_SIZE) {
		allocTable[blockNumber] = 0;
		return TRUE;
	}
	return FALSE;
}

/**
 * Write all changes to the file system to disk
 */
void syncFilesystem() {
	memcpy(map, allocTable, ALLOCATION_BITMAP_PAGES * PAGE_SIZE);
	memcpy(map + ALLOCATION_BITMAP_PAGES * PAGE_SIZE, rootDir->metadata, sizeof (struct Metadata) * ROOT_SECTOR_ENTRIES);

	if (msync(map, FILESIZE, MS_SYNC) < 0) {
		perror("Could not sync filesystem");
	}
}

/**
 * Set metadata to be a directory
 */
void setDirectory(struct Metadata * metadata) {
	// Probably not the most efficent, but it works
	char * temp = (char*)malloc(MAX_FILENAME_SIZE + 1);
	memset(temp, 0, MAX_FILENAME_SIZE);
	memcpy(temp, metadata->filename, MAX_FILENAME_SIZE - 1);
	
	memset(metadata->filename, 0, MAX_FILENAME_SIZE); // 0 out memory
	metadata->filename[0] = DIRECTORY;
	strncat(metadata->filename, temp, MAX_FILENAME_SIZE - 1);
	
	free(temp);

	metadata->fileAttrib = 0;
	metadata->fileSize = sizeof (*metadata);
	metadata->nextBlockNumber = -1;
	setModifyTime(metadata);
}

/**
 * Set metadata to be a file and attach a block to it
 */
void setFile(struct Metadata * metadata) {
	metadata->blockNumber = createBlock();
	// Set page size to metadata + first block size
	metadata->fileSize = sizeof(*metadata) + PAGE_SIZE;
	metadata->nextBlockNumber = -1;
	metadata->fileAttrib = 0;
	
	setModifyTime(metadata);

	// Initialize block
	struct Block * block = (struct Block *) malloc(sizeof (struct Block));
	memset((char*)block, 0, sizeof (struct Block));
	free(block);
}

/**
 * Used to create a new directory
 * i.e. - Once a directory file is created call this method to create the . and ..
 * links which are instantly written to disk and linked up
 */
void createDirectoryStruct(struct Metadata * parentDir) {
	struct Metadata * meta = (struct Metadata *)malloc(sizeof (struct Metadata) * 2);
	memset(meta, 0, sizeof (struct Metadata) * 2);

	// Create . block
	meta[0].filename[0] = '.';
	setDirectory(&meta[0]);
	meta[0].fileAttrib |= SUBDIRECTORY;
	
	// Create .. block
	strcpy(meta[1].filename, "..\0");
	setDirectory(&meta[1]);
	meta[1].fileAttrib |= SUBDIRECTORY;
	meta[1].nextBlockNumber = -1;
	
	if (parentDir != NULL) {
		int block = createBlock();
		int block2 = createBlock();
		
		meta[0].blockNumber     = block;
		meta[0].nextBlockNumber = block2;
		meta[1].blockNumber     = parentDir->blockNumber;

		memcpy(map + block * PAGE_SIZE, &meta[0], sizeof (struct Metadata));
		memcpy(map + block2 * PAGE_SIZE, &meta[1], sizeof (struct Metadata));
		
		parentDir->blockNumber = meta[0].blockNumber;
	} else {
		meta[0].blockNumber     = ALLOCATION_BITMAP_PAGES;
		meta[0].nextBlockNumber = ALLOCATION_BITMAP_PAGES + 1;
		meta[1].blockNumber     = ALLOCATION_BITMAP_PAGES;
		memcpy(map + ALLOCATION_BITMAP_PAGES * PAGE_SIZE, meta, sizeof(struct Metadata) * 2);
	}
	
	free(meta);
}

/**
 * Sets the last modified time and date to the current
 */
void setModifyTime(struct Metadata * metadata) {
	time_t t;
	struct tm * info;
	time(&t);
	info = localtime(&t);
	metadata->lastTimeUpdate = ((info->tm_hour) << 11) | ((info->tm_min) << 5) | (info->tm_sec);
	metadata->lastDateUpdate = ((1900 + info->tm_year) << 9) | ((info->tm_mon) << 5) | info->tm_mday;
}

/*
 * generateData() - Converts source from hex digits to
 * binary data. Returns allocated pointer to data
 * of size amt/2.
 */
char* generateData(char *source, size_t size)
{
	char *retval = (char *)malloc((size >> 1) * sizeof(char));
	size_t i;
	for(i = 0; i<(size-1); i+=2) {
		sscanf(&source[i], "%2hhx", &retval[i>>1]);
	}
	return retval;
}

/*
 * filesystem() - loads in the filesystem and accepts commands
 */
void filesystem(char * file) {
	BOOL createFile = FALSE;

	/* pointer to the memory-mapped filesystem */
	map = NULL;
	
	/*
	 * open file, handle errors, create it if necessary.
	 * should end up with map referring to the filesystem.
	 */
	int fd;
	fd = open(file, O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		if(errno == ENOENT) {
			// Create file and retry
			fd = open(file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
			if (fd < 0) {
				perror("Error creating file");
				exit(-1);
			} else {
				// Setup file system
				if(lseek(fd, FILESIZE-1, SEEK_END) < 0) {
					perror("Error increasing file size");
					exit(-1);
				}
				if(write(fd, "\0", 1) < 0) {
					perror("Error enlarging file size");
					exit(-1);
				}

				// Create root Directory
				if(lseek(fd, ALLOCATION_BITMAP_PAGES * PAGE_SIZE, SEEK_SET) < 0) {
					perror("Error adding root directory");
					exit(-1);
				}
				createFile = TRUE;
			}
		} else {
			perror("Error opening file for writing");
			exit(-1);
		}
	}
	
	map = (char*) mmap(0, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		perror("Error mapping file");
		exit(-1);
	}
	
	if(createFile) {
		createDirectoryStruct(NULL);
	}

	/* Load file system structures */	
	memcpy(allocTable, map, ALLOCATION_BITMAP_PAGES * PAGE_SIZE);
	memcpy((char *)rootDir->metadata, map + ALLOCATION_BITMAP_PAGES * PAGE_SIZE, ROOT_SECTOR_ENTRIES * PAGE_SIZE);
	currentDirBlockStack[0] = ALLOCATION_BITMAP_PAGES;

	#ifdef DEBUG_MODE
	/* Create the Parent directory */
	struct Metadata dir;
	strcpy(dir.filename, "Dir1");
	setDirectory(&dir);
	dir.blockNumber = ALLOCATION_BITMAP_PAGES;
	
	/* Create internal directory structure and write to disk */
	createDirectoryStruct(&dir);
	
	/* Add directory to root directory listing */
	saveMetadataToRootBlock(dir);
	
	/* Directory created */
	
	/* Create a file */
	struct Metadata f;
	strcpy(f.filename, "File1.txt");
	setFile(&f);

	// Save block
	int blockNumber = createBlock();
	saveBlock(&f, blockNumber);
	
	// Link to block
	struct Metadata * temp = NULL;
	int temp2 = dir.blockNumber;
	do {
		temp = (struct Metadata *)getBlock(temp2);
		if(temp->nextBlockNumber == -1) {
			break;
		}
		temp2 = temp->nextBlockNumber;
		free(temp);
	} while(temp != NULL);

	temp->nextBlockNumber = blockNumber;
	saveBlock(temp, temp2);
	free(temp);

	/* Write to file */
	struct Block * b = (struct Block *)getBlock(f.blockNumber);
	strcpy(b->data, "This is a test string");
	saveBlock(b, f.blockNumber);
	free(b);
	
	/* File created */

	// Sync filesystem
	syncFilesystem();
	
	/* Print our the metadata blocks */
	treePrint(rootDir->metadata[0]);
	
	/* Read from created file */
	b = (struct Block *) getBlock(f.blockNumber);
	printf("File Contents: <%s>\n", b->data);
	free(b);
	
	#endif
	
	/*
	 * Accept commands, calling accessory functions unless
	 * user enters "quit"
	 * Commands will be well-formatted.
	 */
	char *buffer = NULL;
	size_t size = 0;
	while(getline(&buffer, &size, stdin) != -1)
	{
		/* Basic checks and newline removal */
		size_t length = strlen(buffer);
		if(length == 0)
		{
			continue;
		}
		if(buffer[length-1] == '\n')
		{
			buffer[length-1] = '\0';
		}

		/* TODO: Complete this function */
		/* You do not have to use the functions as commented (and probably can not)
		 *	They are notes for you on what you ultimately need to do.
		 */

		if(!strcmp(buffer, "quit"))
		{
			break;
		}
		else if(!strncmp(buffer, "dump ", 5))
		{
			if(isdigit(buffer[5]))
			{
				dump(stdout, atoi(buffer + 5));
			}
			else
			{
				char *filename = buffer + 5;
				char *space = strstr(buffer+5, " ");
				*space = '\0';
				//open and validate filename
				//dumpBinary(file, atoi(space + 1));
			}
		}
		else if(!strncmp(buffer, "usage", 5))
		{
			usage();
		}
		else if(!strncmp(buffer, "pwd", 3))
		{
			pwd();
		}
		else if(!strncmp(buffer, "cd ", 3))
		{
			cd(buffer+3);
		}
		else if(!strncmp(buffer, "ls", 2))
		{
			ls();
		}
		else if(!strncmp(buffer, "mkdir ", 6))
		{
			mkdir(buffer+6);
		}
		else if(!strncmp(buffer, "cat ", 4))
		{
			cat(buffer + 4);
		}
		else if(!strncmp(buffer, "write ", 6))
		{
			char *filename = buffer + 6;
			char *space = strstr(buffer+6, " ");
			*space = '\0';
			size_t amt = atoi(space + 1);
			space = strstr(space+1, " ");

			char *data = generateData(space+1, amt<<1);
			writeFS(filename, amt, data);
			free(data);
		}
		else if(!strncmp(buffer, "append ", 7))
		{
			char *filename = buffer + 7;
			char *space = strstr(buffer+7, " ");
			*space = '\0';
			size_t amt = atoi(space + 1);
			space = strstr(space+1, " ");

			char *data = generateData(space+1, amt<<1);
			//append(filename, amt, data);
			free(data);
		}
		else if(!strncmp(buffer, "getpages ", 9))
		{
			getpages(buffer + 9);
		}
		else if(!strncmp(buffer, "get ", 4))
		{
			char *filename = buffer + 4;
			char *space = strstr(buffer+4, " ");
			*space = '\0';
			size_t start = atoi(space + 1);
			space = strstr(space+1, " ");
			size_t end = atoi(space + 1);
			//get(filename, start, end);
		}
		else if(!strncmp(buffer, "rmdir ", 6))
		{
			rmdir2(buffer + 6);
		}
		else if(!strncmp(buffer, "rm -rf ", 7))
		{
			rmForce(buffer + 7);
		}
		else if(!strncmp(buffer, "rm ", 3))
		{
			rm(buffer + 3);
		}
		else if(!strncmp(buffer, "scandisk", 8))
		{
			scandisk();
		}
		else if(!strncmp(buffer, "undelete ", 9))
		{
			//undelete(buffer + 9);
		}

		// Sync filesystem
		syncFilesystem();

		free(buffer);
		buffer = NULL;
	}
	free(buffer);
	buffer = NULL;
	
	if (munmap(map, FILESIZE) < 0) {
		perror("Error un-mmaping file");
	}
	close(fd);
}

/*
 * help() - Print a help message.
 */
void help(char *progname)
{
	printf("Usage: %s [FILE]...\n", progname);
	printf("Loads FILE as a filesystem. Creates FILE if it does not exist\n");
	exit(0);
}

/*
 * main() - The main routine parses arguments and dispatches to the
 * task-specific code.
 */
int main(int argc, char **argv)
{
	/* for getopt */
	long opt;

	/* run a student name check */
	check_student(argv[0]);

	/* parse the command-line options. For this program, we only support */
	/* the parameterless 'h' option, for getting help on program usage. */
	while((opt = getopt(argc, argv, "h")) != -1)
	{
		switch(opt)
		{
		case 'h':
			help(argv[0]);
			break;
		}
	}

	if(argv[1] == NULL)
	{
		fprintf(stderr, "No filename provided, try -h for help.\n");
		return 1;
	}

	/* Initialize static variables */
	allocTable = (unsigned char *) malloc(ALLOCATION_BITMAP_PAGES * PAGE_SIZE + 1);
	memset(allocTable, 0, ALLOCATION_BITMAP_PAGES * PAGE_SIZE);

	rootDir = (struct RootDir *) malloc(sizeof (struct RootDir) + 1);
	memset(rootDir, 0, sizeof(struct RootDir));
	rootDir->metadata = (struct Metadata *) malloc((sizeof (struct Metadata)) * ROOT_SECTOR_ENTRIES + 1);
	memset(rootDir->metadata, 0, (sizeof (struct Metadata)) * ROOT_SECTOR_ENTRIES);

	currentDirBlockStack = (short*) malloc(MAX_DIRECTORY_DEPTH);
	memset(currentDirBlockStack, -1, MAX_DIRECTORY_DEPTH);

	currentDirBlock = 0;

	filesystem(argv[1]);
	
	free(allocTable);
	free(rootDir->metadata);
	free(rootDir);
	free(currentDirBlockStack);
	return 0;
}
