/*Licensed under MIT or Public Domain. See bottom of file for details.

warm_sock.h

	In one file before including "warm_sock.h", be sure to #define 
	WARM_SOCK_IMPL

	#define WARM_SOCK_IMPL
	#include "warm_sock.h"

References:
	https://docs.microsoft.com/en-us/windows/win32/winsock/sending-and-receiving-data-on-the-client
	https://tangentsoft.net/wskfaq/examples/basics/select-server.html
*/

#pragma once
#pragma comment(lib, "Ws2_32.lib")

#include <stdint.h>

///////////////////////////////////////////

typedef int16_t sock_connection_id;

typedef struct sock_buffer_t {
	char   *data;
	int32_t size;
	int32_t curr;
} sock_buffer_t;

typedef struct sock_header_t {
	int32_t            data_id;
	int32_t            data_size;
	sock_connection_id from;
} sock_header_t;

///////////////////////////////////////////

int32_t sock_start_server(const char *port);
int32_t sock_start_client(const char *ip, const char *port);
void    sock_shutdown    ();
void    sock_send        (int32_t data_id, int32_t data_size, void *data);
bool    sock_poll        ();
void    sock_listen      (void (*on_receive)(sock_header_t header, void *data));
bool    sock_is_server   ();

///////////////////////////////////////////

// Compile time string hashing for data type ids 
// see: http://lolengine.net/blog/2011/12/20/cpp-constant-string-hash
#define _sock_H1(s,i,x)   (x*65599u+(uint8_t)s[(i)<(sizeof(s)-1)?(sizeof(s)-1)-1-(i):(sizeof(s)-1)])
#define _sock_H4(s,i,x)   _sock_H1(s,i,_sock_H1(s,i+1,_sock_H1(s,i+2,_sock_H1(s,i+3,x))))
#define _sock_H16(s,i,x)  _sock_H4(s,i,_sock_H4(s,i+4,_sock_H4(s,i+8,_sock_H4(s,i+12,x))))
#define _sock_H64(s,i,x)  _sock_H16(s,i,_sock_H16(s,i+16,_sock_H16(s,i+32,_sock_H16(s,i+48,x))))

#define sock_type_id_str(str) ((uint32_t)(_sock_H64(str,0,0)^(_sock_H64(str,0,0)>>16)))
#define sock_type_id(type) sock_type_id_str(#type)

///////////////////////////////////////////

#ifdef WARM_SOCK_IMPL

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#define SOCK_BUFFER_SIZE 1024

///////////////////////////////////////////

int32_t _sock_init();
void    _sock_on_receive(sock_header_t header, void *data);
void    _sock_send_from (sock_header_t header, void *data);
void    _sock_connection_close(sock_connection_id id);
int32_t _sock_server_connect();
bool    _sock_server_poll();
bool    _sock_client_poll();
void    _sock_buffer_create(sock_buffer_t *buffer);
void    _sock_buffer_free  (sock_buffer_t *buffer);
void    _sock_buffer_add   (sock_buffer_t *buffer, void *data, int32_t size);
void    _sock_buffer_submit(sock_buffer_t *buffer);

///////////////////////////////////////////

typedef struct sock_conn_t {
	bool          connected;
	SOCKET        sock;
	sock_buffer_t in_buffer;
	sock_buffer_t out_buffer;
} sock_conn_t;

WSADATA sock_wsadata = {};
SOCKET  sock_primary = INVALID_SOCKET;
bool    sock_server  = false;
void  (*sock_on_receive_callback)(sock_header_t header, void *data);

sock_conn_t        sock_conns[FD_MAX_EVENTS-1];
int32_t            sock_conn_count = 0;
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
	_sock_buffer_create(&sock_primary_in_buffer);
	_sock_buffer_create(&sock_primary_out_buffer);
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

	_sock_buffer_free(&sock_primary_in_buffer);
	_sock_buffer_free(&sock_primary_out_buffer);

	WSACleanup();
	sock_wsadata = {};
}

///////////////////////////////////////////

void _sock_connection_close(sock_connection_id id) {
	if (sock_conns[id].connected) {
		shutdown   (sock_conns[id].sock, SD_SEND);
		closesocket(sock_conns[id].sock);
		_sock_buffer_free(&sock_conns[id].in_buffer);
		_sock_buffer_free(&sock_conns[id].out_buffer);
		sock_conns[id].sock       = INVALID_SOCKET;
		sock_conns[id].connected  = false;
		sock_conn_count -= 1;
	}
}

///////////////////////////////////////////

void _sock_buffer_create(sock_buffer_t *buffer) {
	*buffer = {};
	buffer->size = SOCK_BUFFER_SIZE;
	buffer->data = (char*)malloc(buffer->size);
}

///////////////////////////////////////////

void _sock_buffer_free(sock_buffer_t *buffer) {
	free(buffer->data);
	*buffer = {};
}

///////////////////////////////////////////
void _sock_buffer_add(sock_buffer_t *buffer, void *data, int32_t size) {
	if (buffer->curr + size > buffer->size) {
		printf("Out buffer is full!");
		return;
	}
	memcpy(buffer->data, data, size);
	buffer->curr += size;
}

///////////////////////////////////////////

void sock_send(int32_t data_id, int32_t data_size, void *data) {
	sock_header_t header;
	header.data_id   = data_id;
	header.data_size = data_size;
	header.from      = sock_self_id;
	_sock_send_from(header, data);
}

///////////////////////////////////////////

void _sock_send_from(sock_header_t header, void *data) {
	int32_t        msg_size = header.data_size + sizeof(sock_header_t);
	sock_header_t *message  = (sock_header_t*)malloc(msg_size);
	*message = header;
	memcpy(&message[1], data, header.data_size);

	if (sock_server) {
		int32_t count = 0;
		// Send to all connected clients
		for (int32_t i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
			if (!sock_conns[i].connected) continue;
			count += 1;
			if (i == message->from) continue;
			_sock_buffer_add(&sock_conns[i].out_buffer, message, msg_size);
		}
	} else {
		_sock_buffer_add(&sock_primary_out_buffer, message, msg_size);
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
	sock_connection_id id = -1;
	for (sock_connection_id i = 0; i < _countof(sock_conns); i++) {
		if (sock_conns[i].connected == false) {
			id = i;
			break;
		}
	}

	// store the connection
	if (id != -1) {
		sock_conns[id].sock = new_client;
		sock_conns[id].connected = true;
		_sock_buffer_create(&sock_conns[id].in_buffer);
		_sock_buffer_create(&sock_conns[id].out_buffer);
		printf("New connection #%d\n", id); // inet_ntoa(address.sin_addr)
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
	send(new_client, (char *)&id, sizeof(int32_t), 0);

	return 1;
}

///////////////////////////////////////////

void _sock_buffer_submit(sock_buffer_t *buffer) {
	sock_header_t *head   = (sock_header_t*)buffer->data;
	int32_t        length = head->data_size + sizeof(sock_header_t);
	while (buffer->curr >= length) {
		if (sock_server) {
			_sock_send_from(*head, &head[1]);
		} else {
			_sock_on_receive(*head, &head[1]);
		}
		memmove(buffer->data, &buffer->data[length], (size_t)buffer->curr - (size_t)length);
		buffer->curr -= length;

		if (buffer->curr > sizeof(sock_header_t)) {
			head = (sock_header_t *)buffer->data;
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
	for (sock_connection_id i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
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
		for (sock_connection_id i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
			sock_conn_t &conn = sock_conns[i];
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
						_sock_buffer_submit(&conn.in_buffer);
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
					_sock_buffer_submit(&sock_primary_in_buffer);
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

#endif

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2020 Nick Klingensmith
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/