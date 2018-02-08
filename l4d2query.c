#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include <unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<errno.h>
#include<netdb.h>

#define BUFLEN 512  //Max length of buffer

#define L4D2REQ_QUERYSVRINFO_LEN 25
const char L4D2REQ_QUERYSVRINFO[] = { 0xff, 0xff, 0xff, 0xff, 0x54, 0x53, 0x6f,
		0x75, 0x72, 0x63, 0x65, 0x20, 0x45, 0x6e, 0x67, 0x69, 0x6e, 0x65, 0x20,
		0x51, 0x75, 0x65, 0x72, 0x79, 0x00 };

int hostname_to_ip(const char *hostname, char *ip) {
	struct hostent *he;
	struct in_addr **addr_list;
	int i;

	if ((he = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, "gethostbyname() throws an exception!\n");
		return 1;
	}

	addr_list = (struct in_addr **) he->h_addr_list;
	for (i = 0; addr_list[i] != NULL; i++) {
		strcpy(ip, inet_ntoa(*addr_list[i]));
		return 0;
	}

	return 1;
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
	int server_port;
	char* server_hostname;
	char server_ipaddr[16];

	if (argc == 2) {
		char* server_port_str = strchr(argv[1], ':');
		if (server_port_str == NULL) {
			server_port = 27015;
			server_hostname = argv[1];
		} else {
			server_port = atoi(server_port_str + 1);
			char server_hostname_len = server_port_str - argv[1];
			server_hostname = malloc(sizeof(char) * (server_hostname_len + 1));
			memcpy(server_hostname, argv[1], server_hostname_len);
			server_hostname[server_hostname_len] = 0;
		}
	} else {
		printf("Parameter: hostname[:port]\nBy Rikka0w0, source code available on Github\n");
		exit(EXIT_FAILURE);
	}

	int server_hostname_unsolved = hostname_to_ip(server_hostname, server_ipaddr);

	if (server_hostname_unsolved) {
		fprintf(stderr, "Can not resolve %s\n", server_hostname);
	}

	if (server_hostname != argv[1] && server_hostname != NULL) {
		free(server_hostname);
	}
	
	if (server_hostname_unsolved) {
		exit(EXIT_FAILURE);
	}

	printf("Testing: %s:%d\n", server_ipaddr, server_port);

	struct sockaddr_in si_other;
	int slen = sizeof(si_other);

	int socket_handler;

	socket_handler = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_handler == -1) {
		fprintf(stderr, "Can not create socket\n");
		exit(EXIT_FAILURE);
	}

	memset((char *)&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(server_port);

	if (inet_aton(server_ipaddr, &si_other.sin_addr) == 0) {
		fprintf(stderr, "inet_aton() failed\n");
		exit(EXIT_FAILURE);
	}

	// Send the message
	if (sendto(socket_handler, L4D2REQ_QUERYSVRINFO, L4D2REQ_QUERYSVRINFO_LEN,
		0, (struct sockaddr *) &si_other, slen) == -1) {
		fprintf(stderr, "Failed to send UDP packet to server\n");
		exit(EXIT_FAILURE);
	}


	char recv_buf[BUFLEN];
	memset(recv_buf, '\0', BUFLEN);
	// Try to receive some data, this is a blocking call
	if (recvfrom(socket_handler, recv_buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen) == -1) {
		fprintf(stderr, "Failed to receive UDP packet from the server\n");
		exit(EXIT_FAILURE);
	}

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
		exit(1);
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
		exit(EXIT_FAILURE);
	}
	recv_ptr += 2;
	l4d2_players = recv_ptr[0];
	l4d2_slots = recv_ptr[1];

	l4d2_servername = remove_bom(l4d2_servername);
	l4d2_mapname = remove_bom(l4d2_mapname);
	printf("%s: %s (%d/%d)\n", l4d2_servername, l4d2_mapname, l4d2_players, l4d2_slots);
	
	close(socket_handler);
	return 0;
}
