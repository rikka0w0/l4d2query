#ifndef _WIN32
#include <unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<sys/ioctl.h>
#include<fcntl.h>
#include<errno.h>
#include<netdb.h>

#define SOCKET_FLAG MSG_NOSIGNAL

#else
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <WinSock2.h>
#pragma comment(lib, "WSock32.lib")
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#include <Windows.h>
#include <BaseTsd.h>

typedef SSIZE_T ssize_t;
typedef int socklen_t;

#define close(s) closesocket(s)
#define ioctl ioctlsocket

#define SOCKET_FLAG 0
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "l4d2query.h"

// UDP communication constraints
#define MAX_RETRY_COUNT 3
#define TIMEOUT_SECONDS 3

#define UDPEX_IS_DATA_READY(r) (r > 0)

#define QUERY_BUFFER_LEN 512
// Request payloads
#define REQ_QUERYSVRINFO_LEN 25
static const char REQ_QUERYSVRINFO[] = { 0xff, 0xff, 0xff, 0xff, 0x54, 0x53, 0x6f,
		0x75, 0x72, 0x63, 0x65, 0x20, 0x45, 0x6e, 0x67, 0x69, 0x6e, 0x65, 0x20,
		0x51, 0x75, 0x65, 0x72, 0x79, 0x00 };
#define REQ_GETPLAYERLIST_LEN 9
static const char REQ_GETPLAYERLIST[] = { 0xff, 0xff, 0xff, 0xff, 0x55, 0x00, 0x00, 0x00, 0x00 };

// Game name strings
static const char* GAME_NAMES[] = {"Unknown game", "Left 4 Dead 2", "Counter Strike 1.6"};

static const char* ERRSTR[] = {
	"OK",
	"Unknown Error",
	"Invalid Hostname",
	"Socket Error",
	"UDP Retry Count Exceed",
	"Server Challenge Failed",
	"Invalid Response",
	"Unknown Game"
};

const char* L4D2_GetErrorDesc(int code) {
	return (L4D2REP_OK >= code && code > L4D2REP_MIN) ? ERRSTR[-code] : "Unknown";
}

// Remove UTF-8 BOM, if it presents
static char* remove_utf8_bom(char* input) {
	if ((input[0] & 0xff) == 0xEF &&
		(input[1] & 0xff) == 0xBB &&
		(input[2] & 0xff) == 0xBF) {
		return input + 3;
	}
	else {
		return input;
	}
}

/**
 * Send a UDP packet to the server, return the response if success.
 *
 * @param socket_handler
 * @param dest_addr
 * @param addrlen
 * @param payload payload bytes to be sent to the sever
 * @param payload_length length of the payload buffer
 * @param recv_buf the buffer to place the response
 * @param recv_buf_len the capacity of the response buffer
 * @return the length of the actual received response, negative code if error
 */
static ssize_t udp_tx_rx(int socket_handler, const struct sockaddr *dest_addr, socklen_t addrlen,
	const char* payload, size_t payload_length, char* recv_buf, size_t recv_buf_len) {
	for (int retry_cnt = 0; retry_cnt < MAX_RETRY_COUNT; ++retry_cnt) {
		// Send the message
		if (sendto(socket_handler, payload, payload_length,
			SOCKET_FLAG, dest_addr, addrlen) == -1) {
			fprintf(stderr, "Failed to send UDP packet to server\n");
			continue;
		}

		// Wait for packet or timeout
		fd_set read_socket_list;
		fd_set err_socket_list;
		FD_ZERO(&read_socket_list);
		FD_ZERO(&err_socket_list);
		FD_SET(socket_handler, &read_socket_list);
		FD_SET(socket_handler, &err_socket_list);

		struct timeval timeout_val;
		timeout_val.tv_sec = TIMEOUT_SECONDS;
		timeout_val.tv_usec = 0;

		if (select(socket_handler + 1, &read_socket_list, NULL, &err_socket_list, &timeout_val)) {
			if (FD_ISSET(socket_handler, &read_socket_list)) {
				memset(recv_buf, 0, recv_buf_len);
				ssize_t actual_received = recvfrom(socket_handler, recv_buf, recv_buf_len, SOCKET_FLAG, NULL, NULL);
				// Try to receive some data, this will not blocking since select call ensure packet arrived
				if (actual_received < 0) {
					fprintf(stderr, "Failed to receive UDP packet from the server\n");
				} else {
					return actual_received;
				}
			}
			else if (FD_ISSET(socket_handler, &err_socket_list)) {
				fprintf(stderr, "Error occurred on socket.\n");
			}
		}
		else {
			fprintf(stderr, "Wait for UDP packet from server timeout.\n");
		}
	}

	fprintf(stderr, "Reached maximum retry count (%d), The server might be down.\n", MAX_RETRY_COUNT);
	return L4D2REP_UDP_TRIED;
}

/**
 * Send A2S_INFO (0x54) to the server. The challenging scheme introduced since Dec 2020 is handled.
 *
 * @param socket_handler
 * @param dest_addr
 * @param addrlen
 * @param recv_buf the buffer to place the response
 * @param recv_buf_len the capacity of the response buffer
 * @return the length of the actual received response, negative code if error
 */
static ssize_t send_server_info_query(int socket_handler, const struct sockaddr* dest_addr, socklen_t addrlen, char* recv_buf, int recv_buf_len) {
	char query_payload[REQ_QUERYSVRINFO_LEN + 4];
	memcpy(query_payload, REQ_QUERYSVRINFO, REQ_QUERYSVRINFO_LEN);

	memset(recv_buf, '\0', recv_buf_len);
	ssize_t response_len = udp_tx_rx(socket_handler, dest_addr, addrlen,
		query_payload, REQ_QUERYSVRINFO_LEN, recv_buf, recv_buf_len);

	if (!UDPEX_IS_DATA_READY(response_len)) {
		return response_len;
	}

	if (response_len < 9) {
		// Insufficient byte received
		return L4D2REP_INVALID_RESPONSE;
	}

	if ((recv_buf[0] & 0xff) == 0xff &&
		(recv_buf[1] & 0xff) == 0xff &&
		(recv_buf[2] & 0xff) == 0xff &&
		(recv_buf[3] & 0xff) == 0xff &&
		(recv_buf[4] & 0xff) == 0x41) {
		// Server is challenging us

		// Append the secret to the next query
		memcpy(query_payload + REQ_QUERYSVRINFO_LEN, recv_buf + 5, 4);

		// Send the query with the secret again
		memset(recv_buf, '\0', recv_buf_len);
		response_len = udp_tx_rx(socket_handler, dest_addr, addrlen,
			query_payload, REQ_QUERYSVRINFO_LEN + 4, recv_buf, recv_buf_len);

		if (!UDPEX_IS_DATA_READY(response_len)) {
			return response_len;
		}
	}

	if ((recv_buf[0] & 0xff) == 0xff &&
		(recv_buf[1] & 0xff) == 0xff &&
		(recv_buf[2] & 0xff) == 0xff &&
		(recv_buf[3] & 0xff) == 0xff &&
		(recv_buf[4] & 0xff) == 0x41) {
		// Server refused our request again?
		return L4D2REP_CHALLENGE_REFUSED;
	}
	else {
		return response_len;
	}
}

static int parse_l4d2_response(char* recv_ptr, struct L4D2REP_QUERYSVRINFO* result) {
	result->version = L4D2REP_VER_L4D2;

	result->servername = recv_ptr;
	recv_ptr += strlen(result->servername) + 1;
	result->mapname = recv_ptr;
	recv_ptr += strlen(result->mapname) + 1;
	result->dir = recv_ptr;
	recv_ptr += strlen(result->dir) + 1;
	result->gametype = recv_ptr;
	recv_ptr += strlen(result->gametype) + 1;

	if (recv_ptr[0] != 0x26 ||
		recv_ptr[1] != 0x02) {
		return L4D2REP_INVALID_RESPONSE;
	}
	recv_ptr += 2;
	result->player_count = recv_ptr[0];
	result->slots = recv_ptr[1];

	result->servername = remove_utf8_bom(result->servername);
	result->mapname = remove_utf8_bom(result->mapname);

	return result->player_count;
}

static int parse_cs16_response(char* recv_ptr, struct L4D2REP_QUERYSVRINFO* result) {
	result->version = L4D2REP_VER_CS16;

	result->servername = recv_ptr;
	recv_ptr += strlen(result->servername) + 1;
	result->servername = recv_ptr;
	recv_ptr += strlen(result->servername) + 1;
	result->mapname = recv_ptr;
	recv_ptr += strlen(result->mapname) + 1;
	result->dir = recv_ptr;
	recv_ptr += strlen(result->dir) + 1;
	result->gametype = recv_ptr;
	recv_ptr += strlen(result->gametype) + 1;

	result->player_count = recv_ptr[0];
	result->slots = recv_ptr[1];

	result->servername = remove_utf8_bom(result->servername);
	result->mapname = remove_utf8_bom(result->mapname);

	return result->player_count;
}

/**
 * Send A2S_INFO (0x54) to the server. The challenging scheme introduced since Dec 2020 is handled.
 *
 * @param socket_handler
 * @param dest_addr
 * @param addrlen
 * @param recv_buf the working buffer
 * @param recv_buf_len the capacity of the working buffer
 * @param result a pointer to a L4D2REP_QUERYSVRINFO struct, must be either statically or dynamically allocated
 * @return the number of online players, or negative code on error
 */
static int query_server_info(int socket_handler, const struct sockaddr *dest_addr, socklen_t addrlen, char* recv_buf, int recv_buf_len, struct L4D2REP_QUERYSVRINFO* result) {
	memset(result, '\0', sizeof(struct L4D2REP_QUERYSVRINFO));
	result->version = L4D2REP_VER_UNKNOWN;

	ssize_t recv_actual_length = send_server_info_query(socket_handler, dest_addr, addrlen, recv_buf, recv_buf_len);

	if (!UDPEX_IS_DATA_READY(recv_actual_length)) {
		return recv_actual_length;
	}

	if (recv_actual_length < 5) {
		// Insufficient byte received
		return L4D2REP_INVALID_RESPONSE;
	}

	int ret = L4D2REP_UNKNOWN_GAME;
	// Check magic number and header
	if ((recv_buf[0] & 0xff) == 0xff &&
		(recv_buf[1] & 0xff) == 0xff &&
		(recv_buf[2] & 0xff) == 0xff &&
		(recv_buf[3] & 0xff) == 0xff) {
		if (
			(recv_buf[4] & 0xff) == 0x49 &&
			(recv_buf[5] & 0xff) == 0x11) {
			ret = parse_l4d2_response(recv_buf+6, result);
		} else if ((recv_buf[4] & 0xff) == 0x6D) {
			ret = parse_cs16_response(recv_buf+5, result);
		}
	}

	return ret;
}

/**
 * Send A2S_PLAYER (0x55) to the server.
 *
 * @param socket_handler
 * @param dest_addr
 * @param addrlen
 * @param recv_buf the working buffer
 * @param recv_buf_len the capacity of the working buffer
 * @param player_names a pointer to a string array, make sure to free it in order to prevent memory leak.
 * @return the number of items in the list, or negative code on error
 */
static int query_player_list(int socket_handler, const struct sockaddr *dest_addr, socklen_t addrlen, char* recv_buf, int recv_buf_len, char*** player_names) {
	*player_names = NULL;
	memset(recv_buf, '\0', recv_buf_len);
	ssize_t recv_actual_length = udp_tx_rx(socket_handler, dest_addr, addrlen,
		REQ_GETPLAYERLIST, REQ_GETPLAYERLIST_LEN,
		recv_buf, recv_buf_len);

	if (!UDPEX_IS_DATA_READY(recv_actual_length)) {
		return L4D2REP_INVALID_RESPONSE;
	}

	// Check magic number and header
	if ((recv_buf[0] & 0xff) != 0xff ||
		(recv_buf[1] & 0xff) != 0xff ||
		(recv_buf[2] & 0xff) != 0xff ||
		(recv_buf[3] & 0xff) != 0xff ||
		(recv_buf[4] & 0xff) != 0x41) {
		return L4D2REP_INVALID_RESPONSE;
	}

	char second_req[REQ_GETPLAYERLIST_LEN];
	memcpy(second_req, REQ_GETPLAYERLIST, REQ_GETPLAYERLIST_LEN);
	// Attach signature
	second_req[5] = recv_buf[5];
	second_req[6] = recv_buf[6];
	second_req[7] = recv_buf[7];
	second_req[8] = recv_buf[8];


	recv_actual_length = udp_tx_rx(socket_handler, dest_addr, addrlen,
		second_req, REQ_GETPLAYERLIST_LEN,
		recv_buf, recv_buf_len);

	if (recv_actual_length < 1) {
		return L4D2REP_INVALID_RESPONSE;
	}

	// Check magic number and header
	if ((recv_buf[0] & 0xff) != 0xff ||
		(recv_buf[1] & 0xff) != 0xff ||
		(recv_buf[2] & 0xff) != 0xff ||
		(recv_buf[3] & 0xff) != 0xff ||
		(recv_buf[4] & 0xff) != 0x44) {
		return L4D2REP_INVALID_RESPONSE;
	}

	int count = recv_buf[5];
	char** result = malloc(sizeof(char*) * (count));
	char* recv_ptr = recv_buf + 7;

	int i;
	for (i = 0; i<count; i++) {
		result[i] = remove_utf8_bom(recv_ptr);
		recv_ptr += strlen(recv_ptr) + 10;
	}

	*player_names = result;
	return count;
}

// Create an struct sockaddr_in from url string (hostname:port)
// If succeed, the result will be stored into the struct sockaddr_in pointed by ipaddr
// Otherwise ipaddr will be left untouched and the function returns L4D2REP_INVALID_HOSTNAME
int parse_hostname(const char* hostname, struct sockaddr_in* ipaddr) {
	if (hostname == NULL)
		return L4D2REP_INVALID_HOSTNAME;

	// Make a copy of hostname
	char* server_hostname = strdup(hostname);

	int server_port;
	char* server_port_str = strchr(hostname, ':');
	if (server_port_str == NULL) {
		server_port = 27015;
	}
	else {
		server_port = atoi(server_port_str + 1);
		// Trim the hostname string
		server_hostname[server_port_str - hostname] = 0;
	}

	struct addrinfo hints, * res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;      // Srcds is IPv4 only
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags |= AI_CANONNAME;

	int errcode = getaddrinfo(server_hostname, NULL, &hints, &res);
	if (errcode != 0) {
		fprintf(stderr, "getaddrinfo() throws an exception! (Code %d)\n", errcode);
		return L4D2REP_INVALID_HOSTNAME;
	}

	// No IP was found
	if (res == NULL) {
		return L4D2REP_INVALID_HOSTNAME;
	}

	memcpy(ipaddr, (struct sockaddr_in *) res->ai_addr, sizeof(struct sockaddr_in));
	ipaddr->sin_port = htons(server_port);
	free(server_hostname);

	return L4D2REP_OK;
}

int L4D2_QueryServerInfo(const char* hostname, struct L4D2REP_QUERYSVRINFO* result) {
	char buffer[QUERY_BUFFER_LEN];
	struct sockaddr_in si_other;
	int slen = sizeof(si_other);

	int ret = parse_hostname(hostname, &si_other);
	if (ret != L4D2REP_OK)
		return ret;

	int socket_handler = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_handler == -1)
		return L4D2REP_SOCKET_ERR;

	long one = 1L;
	ioctl(socket_handler, (int)FIONBIO, &one);

	if (si_other.sin_addr.s_addr == INADDR_NONE || si_other.sin_addr.s_addr == INADDR_ANY) {
		close(socket_handler);
		return L4D2REP_SOCKET_ERR;
	}

	ret = query_server_info(socket_handler, (struct sockaddr *) &si_other, slen, buffer, QUERY_BUFFER_LEN, result);

	if (!L4D2REP_IS_OK(ret)) {
		close(socket_handler);
		return ret;
	}

	close(socket_handler);
	return L4D2REP_OK;
}

/**
 *	Get the player list, caller should free players after use, if it is non-null
 */
int L4D2_GetPlayerList(const char* hostname, char*** players) {
	char buffer[QUERY_BUFFER_LEN];
	struct sockaddr_in si_other;
	int slen = sizeof(si_other);

	int ret = parse_hostname(hostname, &si_other);
	if (ret != L4D2REP_OK)
		return ret;

	int socket_handler = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_handler == -1)
		return L4D2REP_SOCKET_ERR;

	long one = 1L;
	ioctl(socket_handler, (int)FIONBIO, &one);

	if (si_other.sin_addr.s_addr == INADDR_NONE || si_other.sin_addr.s_addr == INADDR_ANY) {
		close(socket_handler);
		return L4D2REP_SOCKET_ERR;
	}

	ret = query_player_list(socket_handler, (struct sockaddr *) &si_other, slen, buffer, QUERY_BUFFER_LEN, players);
	if (!L4D2REP_IS_OK(ret)) {
		close(socket_handler);
		return ret;
	}

	close(socket_handler);
	return ret;
}

int l4d2query_run(int argc, char *argv[]) {
	int exit_code = EXIT_SUCCESS;

#ifdef _WIN32
	WSADATA wd = { 0 };
	if (WSAStartup(MAKEWORD(1, 1), &wd) != 0)
	{
		fprintf(stderr, "Unable to initailize Winsock DLL.\n");
		goto on_error;
	}

	SetConsoleOutputCP(CP_UTF8);
#endif

	if (argc != 2) {
		printf("Parameter: hostname[:port]\nBy Rikka0w0, source code available on Github\n");
		exit_code = EXIT_FAILURE;
		goto on_error;
	}

	struct sockaddr_in server;
	if (parse_hostname(argv[1], &server) != L4D2REP_OK) {
		printf("Unable to resolve the host!\n");
		exit_code = EXIT_FAILURE;
		goto on_error;
	} else {
		printf("Querying: %s:%d\n", inet_ntoa(server.sin_addr), ntohs(server.sin_port));
	}

	struct L4D2REP_QUERYSVRINFO query_server_result;
	int ret = L4D2_QueryServerInfo(argv[1], &query_server_result);
	if (L4D2REP_IS_OK(ret)) {
		printf("%s\n", GAME_NAMES[query_server_result.version]);
		printf("%s: %s (%d/%d)\n",
			query_server_result.servername, query_server_result.mapname,
			query_server_result.player_count, query_server_result.slots);
	} else {
		fprintf(stderr, "Failed to get server info, error %d (%s)\n", ret, L4D2_GetErrorDesc(ret));
	}

	char** player_list = NULL;
	ret = L4D2_GetPlayerList(argv[1], &player_list);
	if (L4D2REP_IS_OK(ret)) {
		printf("Players (%d):\n", ret);
		for (int j = 0; j < ret; j++)
			printf("%i. %s\n", j+1, player_list[j]);
	} else {
		fprintf(stderr, "Failed to get player list, error %d (%s)\n", ret, L4D2_GetErrorDesc(ret));
	}
	free(player_list);

on_error:

#ifdef _WIN32
	WSACleanup();
#endif

	return exit_code;
}
