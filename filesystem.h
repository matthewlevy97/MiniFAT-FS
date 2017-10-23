#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#define DEBUG_MODE

/*  Time shift constants   */
#define HOUR_SHIFT 11
#define MINUTE_SHIFT 5
#define MINUTE_MASK 0x3F
#define SECOND_MASK 0x1F

/*  Date shift constants   */
#define YEAR_SHIFT 9
#define MONTH_SHIFT 5
#define MONTH_MASK 0xF
#define DAY_MASK 0x1F

/*  Macros for getting time   */
#define GET_HOUR(x) (x >> HOUR_SHIFT)
#define GET_MINUTE(x) ((x >> MINUTE_SHIFT) & MINUTE_MASK)
#define GET_SECOND(x) (x & SECOND_MASK)

/*   Macros for getting date   */
#define GET_YEAR(x) (x >> YEAR_SHIFT)
#define GET_MONTH(x) ((x >> MONTH_SHIFT) & MONTH_MASK)
#define GET_DAY(x) (x & DAY_MASK)

/*   Constants for the file system  */
#define FILESIZE 4000000
#define PAGE_SIZE 512
#define ROOT_SECTOR_ENTRIES 20
#define ALLOCATION_BITMAP_PAGES 4

/*  FILE NAME FIRST CHARACTERS   */
#define FILE_DELETED      -27//0xE5
// This is used if the first character of the filename is really 0xE5
#define REAL_E5           0x05
#define DIRECTORY         0x2E

/*  FILE ATTRIBUTES   */
#define FILE_ATTRIB       0x01
#define DIRECTORY_ATTRIB  0x02
#define SYSTEM            0x04
#define DISK_VOLUME_LABEL 0x08
#define SUBDIRECTORY      0x10
#define ARCHIVE           0x20

/* Used for tracking the directory path */
#define MAX_DIRECTORY_DEPTH 255

/*
 *	Prototypes for our filesystem functions.
 *
 *
 */

void dump(FILE * fp, int blockNumber);
//void dumpBinary(char * filename, int blockNumber);
void usage();
void pwd();
void cd(char * path);
void ls();
void mkdir(char * dirname);
void cat(char * filename);
void writeFS(char * filename, int amount, char * data);
//void append(char * filename, int amount, char * data);
//void remove(char * filename, int start, int end);
void rmdir2(char * dirName);
void rm(char * filename);
void rmForce(char * filename);
void getpages(char * filename);
//void get(char * filename, int start, int end);
void scandisk();
//void undelete(char * filename);
void clearDirectory(struct Metadata * metadata);

//Help dialog
void help(char *progname);

//Main filesystem loop
void filesystem(char *file);

//Converts source data into appropriate binary data.
//User must free the returned pointer
char* generateData(char *source, size_t size);

#ifdef DEBUG_MODE
void printMetadata(struct Metadata meta);
void treePrint(struct Metadata metadata);
#endif

struct Metadata * getMetadata();

void * getBlock(int blockNumber);
int createBlock();

void createDirectoryStruct(struct Metadata * parentDir);

void setDirectory(struct Metadata * metadata);
void setFile(struct Metadata * metadata);

int saveMetadataToRootBlock(struct Metadata metadata);
void saveBlock(void * b, int blockNumber);
void setModifyTime(struct Metadata * metadata);

BOOL invalidateBlock(int blockNumber);

void syncFilesystem();

#endif
