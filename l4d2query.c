#ifndef _WIN32
#include <unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<errno.h>
#include<netdb.h>

#define SOCKET_FLAG MSG_NOSIGNAL

#else
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <WinSock2.h>
#include <Windows.h>

#define SOCKET_FLAG 0

static int close(SOCKET s)
{
	return closesocket(s);
}

#endif

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#define MAX_RETRY_COUNT 3
#define TIMEOUT_SECONDS 5

#define BUFLEN 512  //Max length of buffer

#define L4D2REQ_QUERYSVRINFO_LEN 25
const char L4D2REQ_QUERYSVRINFO[] = { 0xff, 0xff, 0xff, 0xff, 0x54, 0x53, 0x6f,
		0x75, 0x72, 0x63, 0x65, 0x20, 0x45, 0x6e, 0x67, 0x69, 0x6e, 0x65, 0x20,
		0x51, 0x75, 0x65, 0x72, 0x79, 0x00 };

int hostname_to_ip(const char *hostname, char ip[][16], int ipMax) {
	struct hostent *he;
	struct in_addr **addr_list;
	int i;

	if ((he = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, "gethostbyname() throws an exception!\n");
		return 0;
	}

	addr_list = (struct in_addr **) he->h_addr_list;
	for (i = 0; i < ipMax && addr_list[i] != NULL; i++) {
		strcpy(ip[i], inet_ntoa(*addr_list[i]));
	}

	return i;
}

char* remove_bom(char* input) {
	if ((input[0] & 0xff) == 0xEF &&
		(input[1] & 0xff) == 0xBB &&
		(input[2] & 0xff) == 0xBF) {
		return input + 3;
	} else {
		return input;
	}
}

int main(int argc, char *argv[]) {
	int exit_code = EXIT_SUCCESS;

	int server_port;
	char* server_hostname = 0;
	char server_ipaddr[16][16];

#ifdef _WIN32
	WSADATA wd = { 0 };
	if (WSAStartup(MAKEWORD(1, 1), &wd) != 0)
	{
		fprintf(stderr, "Fail to initailize Winsock DLL.\n");
		goto on_error;
	}

	SetConsoleOutputCP(CP_UTF8);
#endif

	if (argc == 2) {
		char* server_port_str = strchr(argv[1], ':');
		if (server_port_str == NULL) {
			server_port = 27015;
			server_hostname = strdup(argv[1]);
		} else {
			server_port = atoi(server_port_str + 1);
			int server_hostname_len = server_port_str - argv[1];
			server_hostname = malloc(sizeof(char) * (server_hostname_len + 1));
			memcpy(server_hostname, argv[1], server_hostname_len);
			server_hostname[server_hostname_len] = 0;
		}
	} else {
		printf("Parameter: hostname[:port]\nBy Rikka0w0, source code available on Github\n");
		exit_code = EXIT_FAILURE;
		goto on_error;
	}

	int server_ip_count = hostname_to_ip(server_hostname, server_ipaddr, 16);

	if (server_ip_count == 0) {
		fprintf(stderr, "Can not resolve %s\n", server_hostname);
		exit_code = EXIT_FAILURE;
		goto on_error;
	}

	int i;
	for (i = 0; i < server_ip_count; ++i)
	{
		printf("Testing: %s:%d\n", server_ipaddr[i], server_port);

		struct sockaddr_in si_other;
		int slen = sizeof(si_other);

		int socket_handler = -1;

		socket_handler = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socket_handler == -1) {
			fprintf(stderr, "Can not create socket\n");
			exit_code = EXIT_FAILURE;
			goto on_error;
		}

		memset((char *)&si_other, 0, sizeof(si_other));
		si_other.sin_family = AF_INET;
		si_other.sin_port = htons(server_port);

		si_other.sin_addr.s_addr = inet_addr(server_ipaddr[i]);

		if (si_other.sin_addr.s_addr == INADDR_NONE || si_other.sin_addr.s_addr == INADDR_ANY)
		{
			fprintf(stderr, "inet_aton() failed\n");
			goto on_test_error;
		}

		char recv_buf[BUFLEN];
		memset(recv_buf, '\0', BUFLEN);

		int retry_count;
		for (retry_count = 0; retry_count < MAX_RETRY_COUNT; ++retry_count)
		{
			// Send the message
			if (sendto(socket_handler, L4D2REQ_QUERYSVRINFO, L4D2REQ_QUERYSVRINFO_LEN,
				SOCKET_FLAG, (struct sockaddr *) &si_other, slen) == -1) {
				fprintf(stderr, "Failed to send UDP packet to server\n");
				goto on_test_error;
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

			if (select(1, &read_socket_list, 0, &err_socket_list, &timeout_val))
			{
				if (FD_ISSET(socket_handler, &read_socket_list))
				{
					// Try to receive some data, this will not blocking since select call ensure packet arrived
					if (recvfrom(socket_handler, recv_buf, BUFLEN, SOCKET_FLAG, (struct sockaddr *) &si_other, &slen) == -1) {
						fprintf(stderr, "Failed to receive UDP packet from the server\n");
					}

					break;
				}
				else if (FD_ISSET(socket_handler, &err_socket_list))
				{
					fprintf(stderr, "Error occurred on socket.\n");
				}
			}
			else
			{
				fprintf(stderr, "Wait for UDP packet from server timeout.\n");
			}
		}

		if (retry_count == MAX_RETRY_COUNT)
		{
			fprintf(stderr, "Server may be down. Retry count (%d) is reached.\n", MAX_RETRY_COUNT);
		}
		else
		{
			char* recv_ptr = recv_buf;

			char* l4d2_servername;
			char* l4d2_mapname;
			char* l4d2_dir;
			char* l4d2_gametype;
			char  l4d2_players;
			char  l4d2_slots;

			// Check magic number and header
			if ((recv_ptr[0] & 0xff) != 0xff ||
				(recv_ptr[1] & 0xff) != 0xff ||
				(recv_ptr[2] & 0xff) != 0xff ||
				(recv_ptr[3] & 0xff) != 0xff ||
				(recv_ptr[4] & 0xff) != 0x49 ||
				(recv_ptr[5] & 0xff) != 0x11) {
				fprintf(stderr, "Invalid responce from server\n");
				goto on_test_error;
			}

			recv_ptr += 6;
			l4d2_servername = recv_ptr;
			recv_ptr += strlen(l4d2_servername) + 1;
			l4d2_mapname = recv_ptr;
			recv_ptr += strlen(l4d2_mapname) + 1;
			l4d2_dir = recv_ptr;
			recv_ptr += strlen(l4d2_dir) + 1;
			l4d2_gametype = recv_ptr;
			recv_ptr += strlen(l4d2_gametype) + 1;

			if (recv_ptr[0] != 0x26 ||
				recv_ptr[1] != 0x02) {
				fprintf(stderr, "Invalid responce from server\n");
				goto on_test_error;
			}
			recv_ptr += 2;
			l4d2_players = recv_ptr[0];
			l4d2_slots = recv_ptr[1];

			l4d2_servername = remove_bom(l4d2_servername);
			l4d2_mapname = remove_bom(l4d2_mapname);
			printf("%s: %s (%d/%d)\n", l4d2_servername, l4d2_mapname, l4d2_players, l4d2_slots);
		}

	on_test_error:
		if (socket_handler != -1)
			close(socket_handler);
	}

on_error:
	if (server_hostname)
		free(server_hostname);

#ifdef _WIN32
	WSACleanup();
#endif

	return exit_code;
}
