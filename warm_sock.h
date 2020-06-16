#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS

// References:
// https://docs.microsoft.com/en-us/windows/win32/winsock/sending-and-receiving-data-on-the-client
// https://tangentsoft.net/wskfaq/examples/basics/select-server.html

#pragma comment(lib, "Ws2_32.lib")

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <conio.h>

#define SOCK_BUFFER_SIZE 1024

///////////////////////////////////////////

typedef int16_t sock_connection_id;

struct sock_buffer_t {
	char   *data;
	int32_t size;
	int32_t curr;
};

struct sock_header_t {
	int32_t            data_id;
	int32_t            data_size;
	sock_connection_id from;
};

///////////////////////////////////////////

void    sock_shutdown();
void    sock_send(int32_t data_id, void *data, int32_t data_size, sock_connection_id force_from = -2);
int32_t sock_start_server(const char *port = "27015");
int32_t sock_start_client(const char *ip, const char *port = "27015");
bool    sock_poll();
void    sock_listen(void (*on_receive)(sock_header_t header, void *data));
bool    sock_is_server();

int32_t _sock_init();
void    _sock_on_receive(sock_header_t header, void *data);
void    _sock_connection_close(sock_connection_id id);
int32_t _sock_server_connect();
bool    _sock_server_poll();
bool    _sock_client_poll();
void    _sock_buffer_create(sock_buffer_t &buffer);
void    _sock_buffer_free  (sock_buffer_t &buffer);
void    _sock_buffer_add   (sock_buffer_t &buffer, void *data, int32_t size);
void    _sock_buffer_submit(sock_buffer_t &buffer);

///////////////////////////////////////////

struct connection_t {
	bool          connected;
	SOCKET        sock;
	sock_buffer_t in_buffer;
	sock_buffer_t out_buffer;
};

WSADATA sock_wsadata = {};
SOCKET  sock_primary = INVALID_SOCKET;
bool    sock_server = false;
void  (*sock_on_receive_callback)(sock_header_t header, void *data);

connection_t       sock_conns[FD_MAX_EVENTS-1];
int32_t            sock_conn_count;
sock_connection_id sock_self_id = -2;
sock_buffer_t      sock_primary_in_buffer  = {};
sock_buffer_t      sock_primary_out_buffer = {};

///////////////////////////////////////////

int32_t _sock_init() {
	if (sock_wsadata.wVersion != 0)
		return 1;

	if (WSAStartup(MAKEWORD(2,2), &sock_wsadata) != 0) {
		return -1;
	}
	_sock_buffer_create(sock_primary_in_buffer);
	_sock_buffer_create(sock_primary_out_buffer);
	return 1;
}

///////////////////////////////////////////

void sock_shutdown() {
	// Shut down any client connections
	for (int32_t i = 0; i < _countof(sock_conns); i++) {
		_sock_connection_close(i);
	}

	if (sock_primary != INVALID_SOCKET) closesocket(sock_primary);
	sock_primary = INVALID_SOCKET;

	_sock_buffer_free(sock_primary_in_buffer);
	_sock_buffer_free(sock_primary_out_buffer);

	WSACleanup();
	sock_wsadata = {};
}

///////////////////////////////////////////

void _sock_connection_close(sock_connection_id id) {
	if (sock_conns[id].connected) {
		shutdown   (sock_conns[id].sock, SD_SEND);
		closesocket(sock_conns[id].sock);
		_sock_buffer_free(sock_conns[id].in_buffer);
		_sock_buffer_free(sock_conns[id].out_buffer);
		sock_conns[id].sock       = INVALID_SOCKET;
		sock_conns[id].connected  = false;
		sock_conn_count -= 1;
	}
}

///////////////////////////////////////////

void _sock_buffer_create(sock_buffer_t &buffer) {
	buffer = {};
	buffer.size = SOCK_BUFFER_SIZE;
	buffer.data = (char*)malloc(buffer.size);
}

///////////////////////////////////////////

void _sock_buffer_free(sock_buffer_t &buffer) {
	free(buffer.data);
	buffer = {};
}

///////////////////////////////////////////
void _sock_buffer_add(sock_buffer_t &buffer, void *data, int32_t size) {
	if (buffer.curr + size > buffer.size) {
		printf("Out buffer is full!");
		return;
	}
	memcpy(buffer.data, data, size);
	buffer.curr += size;
}

///////////////////////////////////////////

void sock_send(int32_t data_id, void *data, int32_t data_size, sock_connection_id force_from) {
	size_t msg_size = data_size + sizeof(sock_header_t);
	sock_header_t *message = (sock_header_t*)malloc(msg_size);
	message->data_id   = data_id;
	message->data_size = data_size;
	message->from      = force_from == -2 ? sock_self_id : force_from;
	memcpy(&message[1], data, data_size);

	if (sock_server) {
		int32_t count = 0;
		// Send to all connected clients
		for (size_t i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
			if (!sock_conns[i].connected) continue;
			count += 1;
			if (i == message->from) continue;
			_sock_buffer_add(sock_conns[i].out_buffer, message, msg_size);
		}
	} else {
		_sock_buffer_add(sock_primary_out_buffer, message, msg_size);
	}

	// send to self
	_sock_on_receive(*message, &message[1]);
	free(message);
}

///////////////////////////////////////////

int32_t sock_start_server(const char *port) {
	int32_t result = _sock_init();
	if (!result) return result;

	sock_server = true;

	/*sock_primary = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = htons(27015);
	bind(sock_primary, (SOCKADDR *)&addr, sizeof(addr));
	listen(sock_primary, SOMAXCONN);*/

	addrinfo *address = nullptr;
	addrinfo  hints;

	// Create socket as server
	hints = {};
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags    = AI_PASSIVE;
	if (getaddrinfo(nullptr, port, &hints, &address) != 0) 
		return -1;
	
	// create, bind, and begin listening on the socket
	result = 1;
	sock_primary = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
	if (sock_primary == INVALID_SOCKET
		|| bind  (sock_primary, address->ai_addr, (int)address->ai_addrlen) == SOCKET_ERROR
		|| listen(sock_primary, SOMAXCONN                                 ) == SOCKET_ERROR) {
		result = -2;
	}
	
	freeaddrinfo(address);
	
	sock_self_id = -1;
	return 1;
}

///////////////////////////////////////////

bool sock_is_server() {
	return sock_server;
}

///////////////////////////////////////////

int32_t sock_start_client(const char *ip, const char *port) {
	int32_t result = _sock_init();
	if (!result) return result;

	sock_server = false;

	/*sock_primary = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr( "127.0.0.1" );
	addr.sin_port = htons( 27015 );
	connect( sock_primary, (SOCKADDR*) &addr, sizeof(addr) );*/

	addrinfo *address = nullptr;
	addrinfo  hints;

	// Create socket as client
	hints = {};
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	if (getaddrinfo(ip, port, &hints, &address) != 0)
		return -2;

	sock_primary = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
	if (sock_primary == INVALID_SOCKET) {
		freeaddrinfo(address);
		return -2;
	}
	if (connect(sock_primary, address->ai_addr, (int)address->ai_addrlen) == SOCKET_ERROR) {
		closesocket(sock_primary);
		sock_primary = INVALID_SOCKET;
	}
	freeaddrinfo(address);
	if (sock_primary == INVALID_SOCKET) {
		return -3;
	}

	// get a connection id from the server
	recv(sock_primary, (char*)&sock_self_id, sizeof(int32_t), 0);
	printf("Connected to server as #%d\n", sock_self_id);

	return 1;
}

///////////////////////////////////////////

int32_t _sock_server_connect() {
	sockaddr_in address;
	int32_t     address_size = sizeof(sockaddr_in);
	SOCKET      new_client   = accept(sock_primary, (sockaddr*)&address, &address_size);
	if (new_client == INVALID_SOCKET)
		return -1;

	// Find a free slot in our connections
	int32_t connection_id = -1;
	for (size_t i = 0; i < _countof(sock_conns); i++) {
		if (sock_conns[i].connected == false) {
			connection_id = i;
			break;
		}
	}

	// store the connection
	if (connection_id != -1) {
		sock_conns[connection_id].sock = new_client;
		sock_conns[connection_id].connected = true;
		_sock_buffer_create(sock_conns[connection_id].in_buffer);
		_sock_buffer_create(sock_conns[connection_id].out_buffer);
		printf("New connection #%d\n", connection_id); // inet_ntoa(address.sin_addr)
		sock_conn_count += 1;
	} else {
		printf("Connections are full! Rejecting a new connection.\n");
		if (shutdown(new_client, SD_SEND) == SOCKET_ERROR) {
			printf("shutdown failed with error: %d\n", WSAGetLastError());
			closesocket(new_client);
			return -2;
		}
		closesocket(new_client);
		return -1;
	}

	// Send the client its id
	send(new_client, (char *)&connection_id, sizeof(int32_t), 0);

	return 1;
}

///////////////////////////////////////////

void _sock_buffer_submit(sock_buffer_t &buffer) {
	sock_header_t *head   = (sock_header_t*)buffer.data;
	size_t         length = head->data_size + sizeof(sock_header_t);
	while (buffer.curr >= length) {
		if (sock_server) {
			sock_send(head->data_id, &head[1], head->data_size, head->from);
		} else {
			_sock_on_receive(*head, &head[1]);
		}
		memmove(buffer.data, &buffer.data[length], buffer.curr - length);
		buffer.curr -= length;

		if (buffer.curr > sizeof(sock_header_t)) {
			head = (sock_header_t *)buffer.data;
			length = head->data_size + sizeof(sock_header_t);
		} else {
			break;
		}
	}
}

///////////////////////////////////////////

bool _sock_server_poll() {
	static fd_set fd_read, fd_write, fd_except;

	int32_t data_size = 0;
	bool    result    = true;
	
	// Setup the sockets for the 'select' call
	FD_ZERO(&fd_read);
	FD_ZERO(&fd_write);
	FD_ZERO(&fd_except);
	FD_SET(sock_primary, &fd_read);
	FD_SET(sock_primary, &fd_except);
	int32_t count = 0;
	for (size_t i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
		if (!sock_conns[i].connected) continue;
		count += 1;
		FD_SET(sock_conns[i].sock, &fd_except);
		if (sock_conns[i].in_buffer .curr < sock_conns[i].in_buffer.size) FD_SET(sock_conns[i].sock, &fd_read);
		if (sock_conns[i].out_buffer.curr > 0                            ) FD_SET(sock_conns[i].sock, &fd_write);
	}

	// 'select' will check all the FD_SET sockets to see if any of them are
	// ready for read/write/exception information
	timeval time = {};
	if (select(0, &fd_read, &fd_write, &fd_except, &time) > 0) {
		// Check for connecting clients
		if (FD_ISSET(sock_primary, &fd_except)) {
			result = false;
			printf("primary socket failed with error: %d\n", WSAGetLastError());
			FD_CLR(sock_primary, &fd_except);
		} else if (FD_ISSET(sock_primary, &fd_read)) {
			_sock_server_connect();
			FD_CLR(sock_primary, &fd_read);
		}

		// Receive and send data to any client that's got something
		count = 0;
		for (size_t i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
			connection_t &conn = sock_conns[i];
			if (!conn.connected)
				continue;
			count += 1;

			if (FD_ISSET(conn.sock, &fd_except)) {
				_sock_connection_close(i);
				FD_CLR(conn.sock, &fd_except);
			} else {
				if (FD_ISSET(conn.sock, &fd_read)) {
					data_size = recv(conn.sock, &conn.in_buffer.data[conn.in_buffer.curr], conn.in_buffer.size - conn.in_buffer.curr, 0);
					FD_CLR(conn.sock, &fd_read);
					if (data_size < 0) {
						printf("recv failed with error: %d\n", WSAGetLastError());
						_sock_connection_close(i);
					} else {
						conn.in_buffer.curr += data_size;
						_sock_buffer_submit(conn.in_buffer);
					}
				}
				if (FD_ISSET(conn.sock, &fd_write)) {
					if (conn.out_buffer.curr > 0) {
						if (send(conn.sock, conn.out_buffer.data, conn.out_buffer.curr, 0) == SOCKET_ERROR)
							printf("send failed with error: %d\n", WSAGetLastError());
						conn.out_buffer.curr = 0;
					}
					FD_CLR(conn.sock, &fd_write);
				}
			}
		}
	}

	return result;
}

///////////////////////////////////////////

bool _sock_client_poll() {
	int32_t data_size = 0;
	bool    result = true;

	static fd_set fd_read, fd_write, fd_except;
	FD_ZERO(&fd_read);
	FD_ZERO(&fd_write);
	FD_ZERO(&fd_except);

	FD_SET(sock_primary, &fd_except);
	FD_SET(sock_primary, &fd_read);
	FD_SET(sock_primary, &fd_write);

	timeval time = {};
	if (select(0, &fd_read, &fd_write, &fd_except, &time) > 0) {
		if (FD_ISSET(sock_primary, &fd_except)) {
			printf("primary socket failed with error: %d\n", WSAGetLastError());
			result = false;
			FD_CLR(sock_primary, &fd_except);
		} else {
			if (FD_ISSET(sock_primary, &fd_read)) {
				data_size = recv(sock_primary, (char*)&sock_primary_in_buffer.data[sock_primary_in_buffer.curr], sock_primary_in_buffer.size - sock_primary_in_buffer.curr, 0);
				if (data_size < 0) {
					printf("recv failed with error: %d\n", WSAGetLastError());
					result = false;
				} else {
					sock_primary_in_buffer.curr += data_size;
					_sock_buffer_submit(sock_primary_in_buffer);
				}
				FD_CLR(sock_primary, &fd_read);
			}
			if (FD_ISSET(sock_primary, &fd_write)) {
				if (sock_primary_out_buffer.curr > 0) {
					if (send(sock_primary, sock_primary_out_buffer.data, sock_primary_out_buffer.curr, 0) == SOCKET_ERROR)
						printf("send failed with error: %d\n", WSAGetLastError());
					sock_primary_out_buffer.curr = 0;
				}
				FD_CLR(sock_primary, &fd_write);
			}
		}
	}
	return result;
}

///////////////////////////////////////////

bool sock_poll() {
	return sock_server
		? _sock_server_poll()
		: _sock_client_poll();
}

///////////////////////////////////////////

void _sock_on_receive(sock_header_t header, void *data) {
	if (sock_on_receive_callback)
		sock_on_receive_callback(header, data);
}

///////////////////////////////////////////

void sock_listen(void (*on_receive)(sock_header_t header, void *data)) {
	sock_on_receive_callback = on_receive;
}