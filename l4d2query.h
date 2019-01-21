#include <stdlib.h>

struct L4D2REP_QUERYSVRINFO {
	char* servername;
	char* mapname;
	char* dir;
	char* gametype;
	char  player_count;
	char  slots;
};

// Return values
#define L4D2REP_OK 0
#define L4D2REP_INVALIDHOST 1
#define L4D2REP_SOCKETERR 2
#define L4D2REP_QUERYFAILED 3

/**
 *	Query the server information. Pointers in result points to somewhere in the buffer.
 */
int L4D2_QueryServerInfo(const char* hostname, struct L4D2REP_QUERYSVRINFO* result, char* buffer, size_t buflen);

/**
 *	Get the player list, caller should free players after use, if it is non-null
 */
int L4D2_GetPlayerList(const char* hostname, char*** players, int* count);

/**
 *	Simulate a call to l4d2query, the results will be printed on the console
 */
int l4d2query_run(int argc, char *argv[]);
