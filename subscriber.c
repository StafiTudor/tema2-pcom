#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <netinet/tcp.h>

#define BUFFER_SIZE 2048
#define ID_LEN 11

// functie pentru a dezactiva Nagle - tcp_NODELAY
void disable_nagle(int sock) {
	int flag = 1;
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
		perror("setsockopt TCP_NODELAY");
	}
}

int main(int argc, char* argv[]) {
	// Verificam numarul de argumente
	// argv[0] - numele programului
	// argv[1] - ID ul clientului
	// argv[2] - IP ul serverului
	// argv[3] - portul serverului
	if (argc != 4) {
		fprintf(stderr, "%s", argv[0]);
		exit(1);
	}
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	char* id = argv[1];
	char* ip = argv[2];
	int port = atoi(argv[3]);
	if (strlen(id) > ID_LEN - 1) { // Verificam lungimea ID-ului
		fprintf(stderr, "ID too long\n");
		exit(1);
	}
	int sock = socket(AF_INET, SOCK_STREAM, 0); // Creare socket TCP - sock_stream
	if (sock < 0) {
		perror("socket");
		exit(1);
	}

	struct sockaddr_in server_addr; // Structura pentru adresa serverului
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port); // Portul serverului
	if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
		fprintf(stderr, "Invalid IP address\n");
		exit(1);
	}
	// Conectare la server
	if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("connect");
		exit(1);
	}
	// dezactivam Nagle
	disable_nagle(sock);
	char id_msg[ID_LEN];
	snprintf(id_msg, sizeof(id_msg), "%s\n", id);
	if (send(sock, id_msg, strlen(id_msg), 0) < 0) { // trimitem ID u clientului
		perror("send ID");
		exit(1);
	}

	char buffer[BUFFER_SIZE];
	int n = recv(sock, buffer, BUFFER_SIZE - 1, 0); // primim raspunsul de la server
	if (n <= 0) {
		perror("recv response");
		exit(1);
	}
	buffer[n] = '\0';
	if (strcmp(buffer, "OK\n") != 0) { // verificam daca raspunsul e OK
		printf("%s", buffer);
		close(sock);
		exit(1);
	}
	fd_set read_fds;
	int max_fd; // cel mai mare socket
	if (sock > STDIN_FILENO)
		max_fd = sock;
	else
		max_fd = STDIN_FILENO;
	char sock_buffer[BUFFER_SIZE]; // buffer pentru socket
	int sock_buf_len = 0;

	while (1) { // bucla principala
		FD_ZERO(&read_fds); // resetam setul de socketi
		FD_SET(sock, &read_fds); // adaugam socketul in set
		FD_SET(STDIN_FILENO, &read_fds); // adaugam stdin in set

		if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR) continue;
			perror("select");
			exit(1);
		}

		if (FD_ISSET(STDIN_FILENO, &read_fds)) { // ceva de la stdin
			char command[BUFFER_SIZE];
			if (!fgets(command, sizeof(command), stdin)) { // citim comanda de la stdin
				close(sock);
				exit(0);
			}
			if (send(sock, command, strlen(command), 0) < 0) { // trimitem comanda la server
				perror("send command");
				exit(1);
			}
			command[strcspn(command, "\n")] = '\0'; // eliminam newline-ul
			if (strcmp(command, "exit") == 0) {
				close(sock);
				exit(0);
			}
		}

		if (FD_ISSET(sock, &read_fds)) { // ceva de la server
			// verificam daca a venit ceva pe socket
			// primim date de la server
			int n = recv(sock, sock_buffer + sock_buf_len, BUFFER_SIZE - sock_buf_len - 1, 0);
			if (n <= 0) {
				close(sock);
				exit(0);
			}
			sock_buf_len += n; // actualizam lungimea bufferului
			sock_buffer[sock_buf_len] = '\0';

			char* line_start = sock_buffer; // pointer la inceputul bufferului
			while (1) { // cautam newline in buffer
				char* newline = strchr(line_start, '\n');
				if (!newline) break;
				*newline = '\0';
				printf("%s\n", line_start); // afisam mesajul
				line_start = newline + 1;
			}
			// mutam restul bufferului la inceput
			memmove(sock_buffer, line_start, sock_buf_len - (line_start - sock_buffer) + 1);
			sock_buf_len -= (line_start - sock_buffer); // actualizam lungimea bufferului
		}
	}
	close(sock); // inchidem socketul
	return 0;
}