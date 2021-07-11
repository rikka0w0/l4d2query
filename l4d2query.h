#include <stddef.h>

struct L4D2REP_QUERYSVRINFO {
	char* servername;
	char* mapname;
	char* dir;
	char* gametype;
	char  player_count;
	char  slots;
	char  version;
};

// Return value (ssize_t or int) and error codes (must be negative)
#define L4D2REP_IS_OK(r) (r>=0)
#define L4D2REP_OK 0
#define L4D2REP_ERR -1
#define L4D2REP_INVALID_HOSTNAME -2
#define L4D2REP_SOCKET_ERR -3
#define L4D2REP_UDP_TRIED -4
#define L4D2REP_CHALLENGE_REFUSED -5
#define L4D2REP_INVALID_RESPONSE -6
#define L4D2REP_UNKNOWN_GAME -7
#define L4D2REP_MIN -8

// Versions
#define L4D2REP_VER_UNKNOWN 0
#define L4D2REP_VER_L4D2 1
#define L4D2REP_VER_CS16 2

/***
 * Get the description string of the error code
 */
const char* L4D2_GetErrorDesc(int code);

/**
 *	Query the server information. Return the number of online players, or negative code on error.
 */
int L4D2_QueryServerInfo(const char* hostname, struct L4D2REP_QUERYSVRINFO* result);

/**
 *	Get the player list. Return the number of items in the list, or negative code on error.
 *  Caller should free the player list after use.
 */
int L4D2_GetPlayerList(const char* hostname, char*** players);

/**
 *	Simulate a call to l4d2query, the results will be printed on the console
 */
int l4d2query_run(int argc, char *argv[]);
