#define WARM_SOCK_IMPL
#include "warm_sock.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h> // for Sleep
#include <stdio.h>
#include <conio.h> // for _kbhit
#include <stdlib.h> // for _countof

void on_receive(sock_header_t header, void *data);
void type_message();

///////////////////////////////////////////

int main() {
	// Start as either a server, or a client based on the project #defines
#ifdef WARM_SOCK_SERVER
	printf("Starting server:\n");
	if (!sock_start_server("27015")) return 0;
#else
	printf("Starting client:\n");
	if (!sock_start_client("127.0.0.1", "27015")) return 0;
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
	case sock_type_id_str("string"): {
		printf("#%d - %s\n", header.from, (char*)data);
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
			sock_send(sock_type_id_str("string"), curr+1, &msg);
			curr = 0;
		} else {
			msg[curr] = ch;
			curr++;
		}
	}
}