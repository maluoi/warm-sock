#define WARM_SOCK_IMPL
#include "warm_sock.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h> // for Sleep
#include <stdio.h>
#include <conio.h> // for _kbhit
#include <stdlib.h> // for _countof

///////////////////////////////////////////

struct test_data_t {
	float position[3];
	float direction[3];
};

bool app_run = true;
char app_names[SOCK_MAX_CONNECTIONS][32];
char app_user_name[32];

///////////////////////////////////////////

void on_connection(sock_connection_id id, sock_connect_status_ status);
void on_receive   (sock_header_t header, const void *data);
void check_input();

///////////////////////////////////////////

int main() {
	printf ("What name would you like to go by?\n");
	scanf_s("%s", app_user_name, (uint32_t)sizeof(app_user_name));

	// Set up our callback functions
	sock_on_receive   (on_receive);
	sock_on_connection(on_connection);

	// Set up warm_sock
	if (!sock_init(sock_hash("Warm sock example"), 27015)) return 0;

	// Connect to a server if we find one. If not, make our own server!
	char addr[32];
	if (sock_find_server(addr, sizeof(addr))) {
		printf("Connecting to server at %s\n", addr);
		if (!sock_start_client(addr)) return 0;
	} else {
		printf("Starting a server!\n");
		if (!sock_start_server()) return 0;
	}

	// Poll for network events until it crashes or we get bored!
	while (app_run && sock_poll()) {
		check_input();
		Sleep(1);
	}

	sock_shutdown();
	return 1;
}

///////////////////////////////////////////

void on_connection(sock_connection_id id, sock_connect_status_ status) {
	if (id == sock_get_id()) {
		printf(status == sock_connect_status_joined
			? "Connected to server as #%d, welcome %s!\n"
			: "Disconnected from server.\n", id, app_user_name);
		if (status == sock_connect_status_joined)
			sock_send(sock_hash("user_name"), (int32_t)strlen(app_user_name)+1, app_user_name);
		return;
	}

	if (status == sock_connect_status_joined) {
		sock_send_to(id, sock_hash("user_name"), (int32_t)strlen(app_user_name)+1, app_user_name);
	} else if (status == sock_connect_status_left) {
		printf("%s has left the session.\n", app_names[id]);
	}
}

///////////////////////////////////////////

void on_receive(sock_header_t header, const void *data) {
	switch (header.data_id) {
	case sock_hash("string"): {
		printf("%s - %s\n", app_names[header.from], (char*)data);
	} break;
	case sock_hash("user_name"): {
		strcpy_s(app_names[header.from], (char*)data);
		if (header.from != sock_get_id())
			printf("%s has joined\n", app_names[header.from]);
	} break;
	case sock_hash_type(test_data_t): {
		const test_data_t *test = (test_data_t *)data;
		printf("test_data_t: [%.2f, %.2f, %.2f], [%.2f, %.2f, %.2f]\n",
			test->position [0], test->position [1], test->position [2],
			test->direction[0], test->direction[1], test->direction[2]);
	}
	default: break;
	}
}

///////////////////////////////////////////

void check_input() {
	
	if (_kbhit() != 0) {
		char ch = _getch();
		if (ch == '`') {
			app_run = false;
		} if (ch == '1') {

			// Send a message using a struct
			test_data_t test = { {1, 2, 3}, {3, 2, 1} };
			sock_send(sock_hash_type(test_data_t), sizeof(test), &test);

		} else {

			// track typing, and send as a string of chars
			static char msg[128];
			static int  curr = 0;

			_putch(ch);
			if (ch == '\r' || curr == (_countof(msg) - 1)) {
				msg[curr] = '\0';
				sock_send(sock_hash("string"), curr + 1, &msg);
				curr = 0;
			} else {
				msg[curr] = ch;
				curr++;
			}
		}
	}
}