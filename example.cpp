#include "warm_sock.h"

void on_receive(sock_header_t header, void *data);
void type_message();

///////////////////////////////////////////

int main() {
	// Start as either a server, or a client based on the project #defines
#ifdef WARM_SOCK_SERVER
	printf("Starting server:\n");
	if (!sock_start_server()) return 0;
#else
	printf("Starting client:\n");
	if (!sock_start_client("127.0.0.1")) return 0;
#endif

	// Set up our callback function
	sock_listen(on_receive);

	// Poll for network events until it crashes, or we get bored!
	while (sock_poll()) {
		type_message();
		Sleep(1);
	}

	sock_shutdown();
	return 1;
}

///////////////////////////////////////////

void on_receive(sock_header_t header, void *data) {
	switch (header.data_id) {
	case 1: {
		printf("#%d - %s\n", header.from, data);
	} break;
	default: break;
	}
}

///////////////////////////////////////////

void type_message() {
	static char msg[128];
	static int curr = 0;
	if (_kbhit() != 0) {
		char ch = _getch();
		_putch(ch);
		if (ch == '\r' || curr == (_countof(msg)-1)) {
			msg[curr] = '\0';
			sock_send(1, &msg, curr+1);
			curr = 0;
		} else {
			msg[curr] = ch;
			curr++;
		}
	}
}