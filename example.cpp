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

///////////////////////////////////////////

void on_connection(sock_connection_id id, sock_connect_status_ status);
void on_receive   (sock_header_t header, const void *data);
void check_input();

///////////////////////////////////////////

int main() {
	// Set up our callback functions
	sock_on_receive   (on_receive);
	sock_on_connection(on_connection);

	// Start as either a server, or a client based on the project #defines
#ifdef WARM_SOCK_SERVER
	printf("Starting server:\n");
	if (!sock_start_server(27015)) return 0;
#else
	printf("Starting client:\n");
	if (!sock_start_client("127.0.0.1", 27015)) return 0;
#endif

	// Poll for network events until it crashes, or we get bored!
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
			? "Connected to server as #%d.\n"
			: "Disconnected from server.\n", id);
		return;
	}

	if (status == sock_connect_status_joined) {
		printf("#%d has joined the session\n", id);
		char message[128];
		sprintf_s(message, "Welcome from #%d!", sock_get_id());
		sock_send_to(id, sock_type_id_str("string"), strlen(message)+1, &message[0]);
	} else if (status == sock_connect_status_left) {
		printf("#%d has left the session.\n", id);
	}
}

///////////////////////////////////////////

void on_receive(sock_header_t header, const void *data) {
	switch (header.data_id) {
	case sock_type_id_str("string"): {
		printf("#%d - %s\n", header.from, (char*)data);
	} break;
	case sock_type_id(test_data_t): {
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
			sock_send(sock_type_id(test_data_t), sizeof(test), &test);

		} else {

			// track typing, and send as a string of chars
			static char msg[128];
			static int  curr = 0;

			_putch(ch);
			if (ch == '\r' || curr == (_countof(msg) - 1)) {
				msg[curr] = '\0';
				sock_send(sock_type_id_str("string"), curr + 1, &msg);
				curr = 0;
			} else {
				msg[curr] = ch;
				curr++;
			}
		}
	}
}