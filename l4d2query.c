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
#include <Windows.h>

#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
typedef int socklen_t;

#define SOCKET_FLAG 0

static int close(SOCKET s)
{
	return closesocket(s);
}

#define ioctl ioctlsocket

#endif

#include<stdio.h>
#include<string.h>
#include "l4d2query.h"

#define MAX_RETRY_COUNT 3
#define TIMEOUT_SECONDS 3

#define L4D2REQ_QUERYSVRINFO_LEN 25
const char L4D2REQ_QUERYSVRINFO[] = { 0xff, 0xff, 0xff, 0xff, 0x54, 0x53, 0x6f,
		0x75, 0x72, 0x63, 0x65, 0x20, 0x45, 0x6e, 0x67, 0x69, 0x6e, 0x65, 0x20,
		0x51, 0x75, 0x65, 0x72, 0x79, 0x00 };

#define L4D2REQ_GETPLAYERLIST_LEN 9
const char L4D2REQ_GETPLAYERLIST[] = { 0xff, 0xff, 0xff, 0xff, 0x55, 0x00, 0x00, 0x00, 0x00 };

#define BUFLEN 512  //Max length of buffer
char mybuf[BUFLEN];

// Resolve IP addresses for a given hostname then return the IP list 
// and set the int pointed by ip_count to the number of addresses
// Return 0 when encounter error
struct in_addr** IPListFromHostname(const char *hostname, int* ip_count) {
	struct hostent *he;
	struct in_addr **addr_list;
	int i = 0;

	if ((he = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, "gethostbyname() throws an exception!\n");
		return NULL;
	}

	addr_list = (struct in_addr **) he->h_addr_list;
	while (addr_list[++i] != NULL);
	*ip_count = i;

	return addr_list;
}

// Remove UTF-8 BOM, if it presents
char* RemoveUTF8Bom(char* input) {
	if ((input[0] & 0xff) == 0xEF &&
		(input[1] & 0xff) == 0xBB &&
		(input[2] & 0xff) == 0xBF) {
		return input + 3;
	}
	else {
		return input;
	}
}

// Send a UDP packet to server and receive its responce, then return the actual received length.
// Return negative value when encounter error.
ssize_t ExchangeUDPPacket(int socket_handler, const struct sockaddr *dest_addr, socklen_t addrlen,
	const char* payload, size_t payload_length, char* recv_buf, size_t recv_buf_len, int* retry_count) {
	
	ssize_t actual_received = -1;
	int retry_cnt;
	for (retry_cnt = 0; retry_cnt < MAX_RETRY_COUNT; ++retry_cnt) {
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

		if (select(socket_handler + 1, &read_socket_list, 0, &err_socket_list, &timeout_val)) {
			if (FD_ISSET(socket_handler, &read_socket_list)) {
				memset(recv_buf, 0, recv_buf_len);
				actual_received = recvfrom(socket_handler, recv_buf, recv_buf_len, SOCKET_FLAG, NULL, NULL);
				// Try to receive some data, this will not blocking since select call ensure packet arrived
				if (actual_received < 0) {
					fprintf(stderr, "Failed to receive UDP packet from the server\n");
					continue;
				}

				break;
			}
			else if (FD_ISSET(socket_handler, &err_socket_list)) {
				fprintf(stderr, "Error occurred on socket.\n");
			}
		}
		else {
			fprintf(stderr, "Wait for UDP packet from server timeout.\n");
		}
	}

	*retry_count = retry_cnt;
	return actual_received;
}

// Return 0 if succeded, otherwise return the error code
int L4D2_QueryServerInfo_Impl(int socket_handler, const struct sockaddr *dest_addr, socklen_t addrlen, char* recv_buf, int recv_buf_len, struct L4D2REP_QUERYSVRINFO* result) {
	
	int retry_count;

	memset(recv_buf, '\0', recv_buf_len);
	if (ExchangeUDPPacket(socket_handler, dest_addr, addrlen,
		L4D2REQ_QUERYSVRINFO, L4D2REQ_QUERYSVRINFO_LEN,
		recv_buf, recv_buf_len, &retry_count) < 1) {
		return 1;
	}

	if (retry_count == MAX_RETRY_COUNT) {
		fprintf(stderr, "Reached maximum retry count (%d), The server might be down.\n", MAX_RETRY_COUNT);
		return 1;
	}

	char* recv_ptr = recv_buf;

	// Check magic number and header
	if ((recv_ptr[0] & 0xff) != 0xff ||
		(recv_ptr[1] & 0xff) != 0xff ||
		(recv_ptr[2] & 0xff) != 0xff ||
		(recv_ptr[3] & 0xff) != 0xff ||
		(recv_ptr[4] & 0xff) != 0x49 ||
		(recv_ptr[5] & 0xff) != 0x11) {
		fprintf(stderr, "Invalid responce from server\n");
		return 1;
	}

	recv_ptr += 6;
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
		fprintf(stderr, "Invalid responce from server\n");
		return 1;
	}
	recv_ptr += 2;
	result->player_count = recv_ptr[0];
	result->slots = recv_ptr[1];

	result->servername = RemoveUTF8Bom(result->servername);
	result->mapname = RemoveUTF8Bom(result->mapname);
	return 0;
}

// Return an array of strings, make sure to free it in order to prevent memory leak. Return NULL if encounter error. 
char** L4D2_GetPlayerList_Impl(int socket_handler, const struct sockaddr *dest_addr, socklen_t addrlen, char* recv_buf, int recv_buf_len, int* count) {
	int retry_count;

	memset(recv_buf, '\0', recv_buf_len);
	ssize_t recv_actual_length = ExchangeUDPPacket(socket_handler, dest_addr, addrlen,
		L4D2REQ_GETPLAYERLIST, L4D2REQ_GETPLAYERLIST_LEN,
		recv_buf, recv_buf_len, &retry_count);

	if (recv_actual_length < 1) {
		return NULL;
	}

	if (retry_count == MAX_RETRY_COUNT) {
		fprintf(stderr, "Reached maximum retry count (%d), The server might be down.\n", MAX_RETRY_COUNT);
		return NULL;
	}
	
	// Check magic number and header
	if ((recv_buf[0] & 0xff) != 0xff ||
		(recv_buf[1] & 0xff) != 0xff ||
		(recv_buf[2] & 0xff) != 0xff ||
		(recv_buf[3] & 0xff) != 0xff ||
		(recv_buf[4] & 0xff) != 0x41) {
		fprintf(stderr, "Invalid responce from server\n");
		return NULL;
	}
	
	char second_req[L4D2REQ_GETPLAYERLIST_LEN];
	memcpy(second_req, L4D2REQ_GETPLAYERLIST, L4D2REQ_GETPLAYERLIST_LEN);
	// Attach signature
	second_req[5] = recv_buf[5];
	second_req[6] = recv_buf[6];
	second_req[7] = recv_buf[7];
	second_req[8] = recv_buf[8];

	
	recv_actual_length = ExchangeUDPPacket(socket_handler, dest_addr, addrlen,
		second_req, L4D2REQ_GETPLAYERLIST_LEN,
		recv_buf, recv_buf_len, &retry_count);

	if (recv_actual_length < 1) {
		return NULL;
	}

	if (retry_count == MAX_RETRY_COUNT) {
		fprintf(stderr, "Reached maximum retry count (%d), The server might be down.\n", MAX_RETRY_COUNT);
		return NULL;
	}

	// Check magic number and header
	if ((recv_buf[0] & 0xff) != 0xff ||
		(recv_buf[1] & 0xff) != 0xff ||
		(recv_buf[2] & 0xff) != 0xff ||
		(recv_buf[3] & 0xff) != 0xff ||
		(recv_buf[4] & 0xff) != 0x44) {
		fprintf(stderr, "Invalid responce from server\n");
		return NULL;
	}

	*count = recv_buf[5];
	char** result = malloc(sizeof(char*) * (*count));
	char* recv_ptr = recv_buf + 7;

	int i;
	for (i = 0; i<*count; i++) {
		result[i] = RemoveUTF8Bom(recv_ptr);
		recv_ptr += strlen(recv_ptr) + 10;
	}

	return result;
}

int parse_hostname(const char* hostname, struct in_addr** ipaddr, int* port) {
	if (hostname == NULL)
		return L4D2REP_INVALIDHOST;

	int server_port;
	char* server_hostname = 0;
	struct in_addr** server_ipaddr;

	char* server_port_str = strchr(hostname, ':');
	if (server_port_str == NULL) {
		server_port = 27015;
		server_hostname = strdup(hostname);
	}
	else {
		server_port = atoi(server_port_str + 1);
		size_t server_hostname_len = server_port_str - hostname;
		server_hostname = malloc(sizeof(char) * (server_hostname_len + 1));
		memcpy(server_hostname, hostname, server_hostname_len);
		server_hostname[server_hostname_len] = 0;
	}

	int server_ip_count;
	server_ipaddr = IPListFromHostname(server_hostname, &server_ip_count);
	if (server_hostname)
		free(server_hostname);

	if (server_ip_count == 0)
		return L4D2REP_INVALIDHOST;

	*ipaddr = server_ipaddr[0];
	*port = server_port;

	return L4D2REP_OK;
}

int L4D2_QueryServerInfo(const char* hostname, struct L4D2REP_QUERYSVRINFO* result, char* buffer, size_t buflen) {
	struct in_addr* server_ipaddr;
	int server_port;
	int ret = parse_hostname(hostname, &server_ipaddr, &server_port);
	if (ret != L4D2REP_OK)
		return ret;

	struct sockaddr_in si_other;
	int slen = sizeof(si_other);

	int socket_handler = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_handler == -1) {
		return L4D2REP_SOCKETERR;
	}

	long one = 1L;
	ioctl(socket_handler, (int)FIONBIO, &one);

	memset((char *)&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(server_port);

	si_other.sin_addr.s_addr = server_ipaddr->s_addr;

	if (si_other.sin_addr.s_addr == INADDR_NONE || si_other.sin_addr.s_addr == INADDR_ANY) {
		if (socket_handler != -1)
			close(socket_handler);
		return L4D2REP_SOCKETERR;
	}

	int qret = L4D2_QueryServerInfo_Impl(socket_handler, (struct sockaddr *) &si_other, slen, buffer, buflen, result);

	if (qret != 0) {
		if (socket_handler != -1)
			close(socket_handler);
		return L4D2REP_QUERYFAILED;
	}

	return L4D2REP_OK;
}

/**
 *	Get the player list, caller should free players after use, if it is non-null
 */
int L4D2_GetPlayerList(const char* hostname, char*** players, int* count) {
	struct in_addr* server_ipaddr;
	int server_port;
	int ret = parse_hostname(hostname, &server_ipaddr, &server_port);
	if (ret != L4D2REP_OK)
		return ret;

	struct sockaddr_in si_other;
	int slen = sizeof(si_other);

	int socket_handler = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_handler == -1) {
		return L4D2REP_SOCKETERR;
	}

	long one = 1L;
	ioctl(socket_handler, (int)FIONBIO, &one);

	memset((char *)&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(server_port);

	si_other.sin_addr.s_addr = server_ipaddr->s_addr;

	if (si_other.sin_addr.s_addr == INADDR_NONE || si_other.sin_addr.s_addr == INADDR_ANY) {
		if (socket_handler != -1)
			close(socket_handler);
		return L4D2REP_SOCKETERR;
	}

	*players = L4D2_GetPlayerList_Impl(socket_handler, (struct sockaddr *) &si_other, slen, mybuf, BUFLEN, count);
	if (*players == NULL) {
		if (socket_handler != -1)
			close(socket_handler);
		return L4D2REP_QUERYFAILED;
	}

	return L4D2REP_OK;
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

	struct in_addr* server_ipaddr;
	int server_port;
	if (parse_hostname(argv[1], &server_ipaddr, &server_port) != L4D2REP_OK) {
		printf("Unable to resolve the host!\n");
		exit_code = EXIT_FAILURE;
		goto on_error;
	} else {
		printf("Querying: %s:%d\n", inet_ntoa(*server_ipaddr), server_port);
	}

	struct L4D2REP_QUERYSVRINFO query_server_result;
	int ret = L4D2_QueryServerInfo(argv[1], &query_server_result, mybuf, BUFLEN);
	if (ret == L4D2REP_OK) {
		printf("%s: %s (%d/%d)\n",
			query_server_result.servername, query_server_result.mapname,
			query_server_result.player_count, query_server_result.slots);
	} else {
		printf("QueryServerInfo failed (%d)!\n", ret);
	}


	int player_count = 0;
	char** player_list = NULL;
	ret = L4D2_GetPlayerList(argv[1], &player_list, &player_count);
	if (ret == L4D2REP_OK && player_count > 0) {
		printf("Players(%d):\n", player_count);
		for (int j = 0; j < player_count; j++)
			printf("%i. %s\n", j+1, player_list[j]);
	}
	free(player_list);

on_error:

#ifdef _WIN32
	WSACleanup();
#endif

	return exit_code;
}
