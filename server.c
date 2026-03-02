#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <stdbool.h>
#include <netinet/tcp.h>

#define TOPIC_LEN 51
#define ID_LEN 11
#define BUFFER_SIZE 2048 // pt mesaje mari

// structura pt clienti chiar si daca nu sunt conectati
typedef struct {
	char id[ID_LEN];
	char** subscriptions; // alocare dinamica
	int sub_count;
} ClientInfo;

// struct pt clientii conectati
typedef struct {
	int sock;
	char id[ID_LEN];
	ClientInfo* info;
	char buffer[BUFFER_SIZE];
	int buf_len;
} Client;

// fct asta verifca daca un topic se potriveste cu un pattern
bool match(const char* pattern, const char* topic) {
	char p[TOPIC_LEN], t[TOPIC_LEN];
	strncpy(p, pattern, TOPIC_LEN - 1);
	strncpy(t, topic, TOPIC_LEN - 1);
	p[TOPIC_LEN - 1] = '\0';
	t[TOPIC_LEN - 1] = '\0';

	// fct recursiva care verifica daca un topic se potriveste cu * sau +
	bool match_recursive(const char* p, const char* t) {
		if (!p[0] && !t[0]) return true;
		if (!p[0] || !t[0]) return false; // daca unul s a term si altul nu nu se potrivesc

		const char* p_next = strchr(p, '/');
		const char* t_next = strchr(t, '/');
		size_t p_len;
		if (p_next)
			p_len = (size_t)(p_next - p);
		else
			p_len = strlen(p);
		size_t t_len;
		if (t_next)
			t_len = (size_t)(t_next - t);
		else
			t_len = strlen(t);

		if (p_len == 1 && p[0] == '*') { // * ia tot ce e in fata
			if (!p_next) return true; // * e ultimul, deci merge
			const char* t_curr = t;
			while (t_curr) {
				if (match_recursive(p_next + 1, t_curr)) return true;
				t_curr = strchr(t_curr, '/');
				if (t_curr) t_curr++;
			}
			return false;
		}

		if (p_len == 1 && p[0] == '+') { // + ia doar un nivel
			if (!t_next && !p_next) return true;
			if (!t_next || !p_next) return false; // la fel ca mai sus
			return match_recursive(p_next + 1, t_next + 1);
		}
		// verific daca se potrivesc
		if (p_len != t_len || strncmp(p, t, p_len) != 0)
			return false;

		if (!p_next && !t_next) return true; // ok
		if (!p_next || !t_next) return false; // != ok
		return match_recursive(p_next + 1, t_next + 1);
	}
	return match_recursive(p, t);
}

// trimite mesajul la client prin socketul lui
void forward_message(int client_sock, const char* message) {
	if (send(client_sock, message, strlen(message), 0) < 0)
		perror("send");
}

// elimina clientul din lista de clienti
// inchide socketul si muta toti clientii cu un loc in fata
void remove_client(Client** clients, int* client_count, int index) {
	printf("Client %s disconnected.\n", (*clients)[index].id);
	close((*clients)[index].sock);
	for (int j = index; j < *client_count - 1; j++) {
		(*clients)[j] = (*clients)[j + 1];
	}
	(*client_count)--;
}

int main(int argc, char* argv[]) {
	if (argc != 2) { // verific portul daca e dat ca argument
		fprintf(stderr, "%s\n", argv[0]);
		exit(1);
	}
	// dezactivez ca sa vad mesajele instant
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	int port = atoi(argv[1]);
	int tcp_sock = socket(AF_INET, SOCK_STREAM, 0); // Socket TCP pentru clienti
	// socket UDP pentru mesaje de laUDP
	int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (tcp_sock < 0 || udp_sock < 0) {
		perror("socket");
		exit(1);
	}

	struct sockaddr_in server_addr; // structura pentru adresa serverului
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY; // accept orice IP
	// serverul asculta pe portul dat ca argument
	server_addr.sin_port = htons(port);

	// leg socketurile la adresa serverului
	if (bind(tcp_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 ||
		bind(udp_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind");
		exit(1);
	}
	// listen - TCP pentru conexiuni noi
	if (listen(tcp_sock, 10) < 0) {
		perror("listen");
		exit(1);
	}
	// alocare dinamica (fara limitare)
	Client* clients = (Client*)malloc(100 * sizeof(Client));
	ClientInfo* client_infos = (ClientInfo*)malloc(100 * sizeof(ClientInfo));
	
	int client_count = 0; // nr clienti conectati
	int info_count = 0; // nr clienti cu info
	int max_clients = 100; // limita initiala
	fd_set read_fds;
	int max_fd; // cel mai mare socket
	if (tcp_sock > udp_sock)
		max_fd = tcp_sock;
	else
		max_fd = udp_sock;
	if (max_fd > STDIN_FILENO)
		max_fd = max_fd;
	else
		max_fd = STDIN_FILENO;

	while (1) { // bucla principala
		FD_ZERO(&read_fds); // reset
		FD_SET(tcp_sock, &read_fds); // adaug socketul TCP
		FD_SET(udp_sock, &read_fds); // adaug socketul UDP
		FD_SET(STDIN_FILENO, &read_fds); //stdin pt comenzi
		// adaug socketurile clientilor
		for (int i = 0; i < client_count; i++) {
			FD_SET(clients[i].sock, &read_fds);
			if (clients[i].sock > max_fd)
				max_fd = clients[i].sock;
		}

		// select - astept sa vina ceva pe socketuri
		if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR) continue;
			perror("select");
			exit(1);
		}

		if (FD_ISSET(tcp_sock, &read_fds)) { // a venit un client nou
			struct sockaddr_in client_addr;
			socklen_t addr_len = sizeof(client_addr);
			// accept conexiunea
			int client_sock = accept(tcp_sock, (struct sockaddr*)&client_addr, &addr_len);
			if (client_sock < 0) {
				perror("accept");
				continue;
			}
			int flag = 1;
			// dezactivez Nagle ca sa trimitem instant mesajele
			setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

			char buffer[ID_LEN];
			int n = recv(client_sock, buffer, ID_LEN - 1, 0); // primesc ID ul clientului
			if (n <= 0) {
				close(client_sock);
				continue;
			}
			buffer[n] = '\0';
			char* newline = strchr(buffer, '\n');
			if (newline) *newline = '\0';

			bool id_exists = false;
			for (int i = 0; i < client_count; i++) {
				if (strcmp(clients[i].id, buffer) == 0) { // verific daca ID-ul e deja folosit
					// daca e, ies
					id_exists = true;
					break;
				}
			}
			if (id_exists) { // daca e folosit, inchid socketul
				printf("Client %s already connected.\n", buffer);
				send(client_sock, "ID already in use\n", 18, 0);
				close(client_sock);
			} else { // daca nu e folosit, il adaug in lista de clienti
				if (client_count == max_clients) { // verificare daca am depasit limita
					max_clients *= 2; // dublam dimensiunea
					clients = (Client*)realloc(clients, max_clients * sizeof(Client));
					client_infos = (ClientInfo*)realloc(client_infos, max_clients * sizeof(ClientInfo));
				}

				printf("New client %s connected from %s:%d.\n", buffer, 
					inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
				send(client_sock, "OK\n", 3, 0);
				clients[client_count].sock = client_sock;
				strcpy(clients[client_count].id, buffer);
				clients[client_count].buf_len = 0;

				ClientInfo* info = NULL;
				for (int i = 0; i < info_count; i++) {
					if (strcmp(client_infos[i].id, buffer) == 0) { // caut info existent
						info = &client_infos[i];
						break;
					}
				}
				if (!info && info_count < max_clients) { // daca nu exista, il creez
					strcpy(client_infos[info_count].id, buffer);
					client_infos[info_count].sub_count = 0;
					client_infos[info_count].subscriptions = NULL;
					info = &client_infos[info_count];
					info_count++;
				}
				clients[client_count].info = info;
				client_count++;
			}
		}

		// verific daca a venit ceva pe UDP
		if (FD_ISSET(udp_sock, &read_fds)) {
			char buffer[1551];
			struct sockaddr_in udp_addr;
			socklen_t addr_len = sizeof(udp_addr);
			int n = recvfrom(udp_sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&udp_addr, &addr_len);
			if (n < 51) continue;

			char topic[TOPIC_LEN];
			int topic_len = strnlen(buffer, 50);
			strncpy(topic, buffer, topic_len);
			topic[topic_len] = '\0';
			uint8_t type = (uint8_t)buffer[50];

			char message[BUFFER_SIZE];
			char value[1500];
			// verific ce tip de mesaj e (int short etc.)
			switch (type) {
				case 0: { // INT
					uint8_t sign = buffer[51];
					uint32_t val = ntohl(*(uint32_t*)&buffer[52]);
					int formatted_val;
					if (sign)
						formatted_val = -(int)val;
					else
						formatted_val = (int)val;
					snprintf(value, sizeof(value), "%d", formatted_val);
					snprintf(message, sizeof(message), "%s:%d - %s - INT - %s\n",
							 inet_ntoa(udp_addr.sin_addr), ntohs(udp_addr.sin_port), topic, value);
					break;
				}
				case 1: { // SHORT_REAL
					uint16_t val = ntohs(*(uint16_t*)&buffer[51]);
					snprintf(value, sizeof(value), "%.2f", val / 100.0);
					snprintf(message, sizeof(message), "%s:%d - %s - SHORT_REAL - %s\n",
							 inet_ntoa(udp_addr.sin_addr), ntohs(udp_addr.sin_port), topic, value);
					break;
				}
				case 2: { // FLOAT
					uint8_t sign = buffer[51];
					uint32_t value1 = ntohl(*(uint32_t*)&buffer[52]);
					uint8_t power = buffer[56];
					double val = (double)value1;
					double factor = 1.0;
					while (power > 0) {
						factor *= 10;
						power--;
					}
					val /= factor;
					double formatted_val;
					if (sign)
						formatted_val = -val;
					else
						formatted_val = val;
					snprintf(value, sizeof(value), "%.4f", formatted_val);
					snprintf(message, sizeof(message), "%s:%d - %s - FLOAT - %s\n",
							 inet_ntoa(udp_addr.sin_addr), ntohs(udp_addr.sin_port), topic, value);
					break;
				}
				case 3: { // STRING
					strncpy(value, &buffer[51], n - 51);
					value[n - 51] = '\0';
					snprintf(message, sizeof(message), "%s:%d - %s - STRING - %s\n",
							 inet_ntoa(udp_addr.sin_addr), ntohs(udp_addr.sin_port), topic, value);
					break;
				}
				default:
					continue;
			}

			for (int i = 0; i < client_count; i++) { // trimit mesajul la clienti
				for (int j = 0; j < clients[i].info->sub_count; j++) {
					if (match(clients[i].info->subscriptions[j], topic)) {
						forward_message(clients[i].sock, message);
						break;
					}
				}
			}
		}

		if (FD_ISSET(STDIN_FILENO, &read_fds)) {
			char cmd[10];
			if (fgets(cmd, sizeof(cmd), stdin) && strcmp(cmd, "exit\n") == 0) {
				for (int i = 0; i < client_count; i++) {
					close(clients[i].sock); // inchid socketurile clientilor
				}
				for (int i = 0; i < info_count; i++) {
					for (int j = 0; j < client_infos[i].sub_count; j++) {
						free(client_infos[i].subscriptions[j]); // eliberez memorie
					}
					free(client_infos[i].subscriptions); // eliberez memoria pentru fiecare client
				}
				close(tcp_sock);
				close(udp_sock);
				exit(0); // ies din program
			}
		}

		for (int i = 0; i < client_count; i++) {
			if (FD_ISSET(clients[i].sock, &read_fds)) { // s a trimis ceva
				int n = recv(clients[i].sock, clients[i].buffer + clients[i].buf_len, 
							 BUFFER_SIZE - clients[i].buf_len - 1, 0);
				if (n <= 0) { // clientul s a deconectat
					remove_client(&clients, &client_count, i);
					i--;
					continue;
				}
				clients[i].buf_len += n;
				clients[i].buffer[clients[i].buf_len] = '\0';

				char* line_start = clients[i].buffer;
				while (1) {
					char* newline = strchr(line_start, '\n');
					if (!newline) break;
					*newline = '\0';

					// verific subscribe sau unsubscribe
					if (strncmp(line_start, "subscribe ", 10) == 0) {
						char* topic = line_start + 10;
						// realocare 
						clients[i].info->subscriptions = (char**)realloc(clients[i].info->subscriptions,
							(clients[i].info->sub_count + 1) * sizeof(char*));
						clients[i].info->subscriptions[clients[i].info->sub_count] = strdup(topic);
						clients[i].info->sub_count++;

						char response[BUFFER_SIZE];
						snprintf(response, sizeof(response), "Subscribed to topic %s\n", topic);
						send(clients[i].sock, response, strlen(response), 0);
					} else if (strncmp(line_start, "unsubscribe ", 12) == 0) {
						char* topic = line_start + 12;
						for (int j = 0; j < clients[i].info->sub_count; j++) {
							if (strcmp(clients[i].info->subscriptions[j], topic) == 0) {
								free(clients[i].info->subscriptions[j]);
								for (int k = j; k < clients[i].info->sub_count - 1; k++) {
									clients[i].info->subscriptions[k] = clients[i].info->subscriptions[k + 1];
								}
								clients[i].info->sub_count--;
								clients[i].info->subscriptions = (char**)realloc(clients[i].info->subscriptions,
									clients[i].info->sub_count * sizeof(char*));
								char response[BUFFER_SIZE];
								snprintf(response, sizeof(response), "Unsubscribed from topic %s\n", topic);
								send(clients[i].sock, response, strlen(response), 0);
								break;
							}
						}
					}

					line_start = newline + 1;
				}
				// Mut mesajele ramase in buffer
				memmove(clients[i].buffer, line_start, clients[i].buf_len - (line_start - clients[i].buffer) + 1);
				clients[i].buf_len -= (line_start - clients[i].buffer);
			}
		}
	}
	return 0;
}
