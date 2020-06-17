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

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdint.h>

///////////////////////////////////////////

typedef int16_t sock_connection_id;

typedef enum sock_connect_status_ {
	sock_connect_status_joined,
	sock_connect_status_left,
} sock_connect_status_;

typedef struct sock_buffer_t {
	char   *data;
	int32_t size;
	int32_t curr;
} sock_buffer_t;

typedef struct sock_header_t {
	uint32_t           data_id;
	int32_t            data_size;
	sock_connection_id from;
	sock_connection_id to;
} sock_header_t;

///////////////////////////////////////////

int32_t sock_init         (uint32_t app_id, uint16_t port);
bool    sock_find_server  (char *out_address, int32_t out_address_size);
int32_t sock_start_server ();
int32_t sock_start_client (const char *ip);
void    sock_shutdown     ();
void    sock_send         (uint32_t data_id, int32_t data_size, const void *data);
void    sock_send_to      (sock_connection_id to, uint32_t data_id, int32_t data_size, const void *data);
bool    sock_poll         ();
void    sock_on_receive   (void (*on_receive   )(sock_header_t header, const void *data));
void    sock_on_connection(void (*on_connection)(sock_connection_id id, sock_connect_status_ status));
bool               sock_is_server();
sock_connection_id sock_get_id   ();

///////////////////////////////////////////

// Compile time string hashing for data type ids 
// see: http://lolengine.net/blog/2011/12/20/cpp-constant-string-hash
#define _sock_H1(s,i,x)   (x*65599u+(uint8_t)s[(i)<(sizeof(s)-1)?(sizeof(s)-1)-1-(i):(sizeof(s)-1)])
#define _sock_H4(s,i,x)   _sock_H1(s,i,_sock_H1(s,i+1,_sock_H1(s,i+2,_sock_H1(s,i+3,x))))
#define _sock_H16(s,i,x)  _sock_H4(s,i,_sock_H4(s,i+4,_sock_H4(s,i+8,_sock_H4(s,i+12,x))))
#define _sock_H64(s,i,x)  _sock_H16(s,i,_sock_H16(s,i+16,_sock_H16(s,i+32,_sock_H16(s,i+48,x))))

#define sock_hash(str) ((uint32_t)(_sock_H64(str,0,0)^(_sock_H64(str,0,0)>>16)))
#define sock_hash_type(type) sock_hash(#type)

///////////////////////////////////////////

#ifdef WARM_SOCK_IMPL

// FD_SET has a warning built into it
#pragma warning( push )
#pragma warning( disable: 6319 )

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#define SOCK_BUFFER_SIZE 1024

///////////////////////////////////////////

void    _sock_on_receive   (sock_header_t header, const void *data);
void    _sock_send_ex      (sock_header_t header, const void *data);
void    _sock_connection_close     (sock_connection_id id, bool notify);
int32_t _sock_server_new_connection();
bool    _sock_server_poll  ();
bool    _sock_client_poll  ();
void    _sock_buffer_create(sock_buffer_t *buffer);
void    _sock_buffer_free  (sock_buffer_t *buffer);
void    _sock_buffer_add   (sock_buffer_t *buffer, void *data, int32_t size);
void    _sock_buffer_submit(sock_buffer_t *buffer);
void    _sock_multicast_begin();
void    _sock_multicast_end();
bool    _sock_multicast_step();

///////////////////////////////////////////

typedef enum sock_conn_type_ {
	sock_conn_type_free,
	sock_conn_type_client,
	sock_conn_type_primary,
} sock_conn_type_;

typedef struct sock_conn_t {
	sock_conn_type_ type;
	SOCKET          sock;
	sock_buffer_t   in_buffer;
	sock_buffer_t   out_buffer;
} sock_conn_t;

typedef struct sock_conn_event_t {
	sock_connection_id   id;
	sock_connect_status_ status;
} sock_conn_event_t;

typedef struct sock_initial_data_t {
	char               id[10];
	uint32_t           app_id;
	sock_connection_id conn_id;
} sock_initial_data_t;

WSADATA sock_wsadata = {};
SOCKET  sock_discovery;
bool    sock_server  = false;
void  (*sock_on_receive_callback   )(sock_header_t header, const void *data);
void  (*sock_on_connection_callback)(sock_connection_id id, sock_connect_status_ status);

sock_conn_t        sock_conns[FD_MAX_EVENTS];
int32_t            sock_conn_count = 0;
sock_connection_id sock_self_id = -1;
uint32_t           sock_app_id = 0;
uint16_t           sock_port = 0;

///////////////////////////////////////////

int32_t sock_init (uint32_t app_id, uint16_t port) {
	if (sock_wsadata.wVersion != 0)
		return 1;

	if (WSAStartup(MAKEWORD(2,2), &sock_wsadata) != 0) {
		return -1;
	}

	sock_app_id = app_id;
	sock_port = port;
	return 1;
}

///////////////////////////////////////////

void sock_shutdown() {
	// Manually notify of disconnect, since everything is shutting down
	uint8_t            msg[sizeof(sock_header_t) + sizeof(sock_conn_event_t)];
	sock_header_t     *header = (sock_header_t    *)&msg[0];
	sock_conn_event_t *evt    = (sock_conn_event_t*)&msg[sizeof(sock_header_t)];
	header->from      = sock_self_id;
	header->to        = -1;
	header->data_id   = sock_hash_type(sock_conn_event_t);
	header->data_size = sizeof(sock_conn_event_t);
	evt->id     = sock_self_id;
	evt->status = sock_connect_status_left;

	// Notify to self
	_sock_on_receive(*header, evt);

	// Notify and shut down client connections
	if (sock_server) {
		// Stop the discovery socket
		_sock_multicast_end();

		for (int32_t i = 0; i < _countof(sock_conns); i++) {
			if (sock_conns[i].type == sock_conn_type_client) {
				send(sock_conns[i].sock, (char *)&msg[0], _countof(msg), 0);
				_sock_connection_close(i, false);
			}
		}
	}
	
	// Close down the primary socket
	_sock_connection_close(sock_self_id, false);

	WSACleanup();
	sock_wsadata = {};
}

///////////////////////////////////////////

void _sock_connection_close(sock_connection_id id, bool notify) {
	if (sock_conns[id].type == sock_conn_type_free)
		return;

	shutdown   (sock_conns[id].sock, SD_SEND);
	closesocket(sock_conns[id].sock);
	_sock_buffer_free(&sock_conns[id].in_buffer );
	_sock_buffer_free(&sock_conns[id].out_buffer);
	sock_conns[id].sock = INVALID_SOCKET;
	sock_conns[id].type = sock_conn_type_free;
	sock_conn_count -= 1;

	if (notify) {
		sock_conn_event_t evt = {};
		evt.id     = id;
		evt.status = sock_connect_status_left;
		sock_send(sock_hash_type(sock_conn_event_t), sizeof(evt), &evt);
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
	memcpy(&buffer->data[buffer->curr], data, size);
	buffer->curr += size;
}

///////////////////////////////////////////

void sock_send(uint32_t data_id, int32_t data_size, const void *data) {
	sock_header_t header;
	header.data_id   = data_id;
	header.data_size = data_size;
	header.from      = sock_self_id;
	header.to        = -1;
	_sock_send_ex(header, data);
}

///////////////////////////////////////////

void sock_send_to(sock_connection_id to, uint32_t data_id, int32_t data_size, const void *data) {
	sock_header_t header;
	header.data_id   = data_id;
	header.data_size = data_size;
	header.from      = sock_self_id;
	header.to        = to;
	_sock_send_ex(header, data);
}

///////////////////////////////////////////

void _sock_send_ex(sock_header_t header, const void *data) {
	int32_t        msg_size = header.data_size + sizeof(sock_header_t);
	sock_header_t *message  = (sock_header_t*)malloc(msg_size);
	*message = header;
	memcpy(&message[1], data, header.data_size);

	if (sock_server) {
		if (message->to == -1) {
			// Send to all connected clients
			int32_t count = 0;
			for (int32_t i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
				if (sock_conns[i].type == sock_conn_type_free) continue;
				count += 1;
				if (sock_conns[i].type == sock_conn_type_primary) continue;
				if (i == message->from) continue;
				_sock_buffer_add(&sock_conns[i].out_buffer, message, msg_size);
			}
		} else {
			// Send to specific connection
			if (message->to >= 0 && message->to < _countof(sock_conns) && sock_conns[message->to].type == sock_conn_type_client) {
				_sock_buffer_add(&sock_conns[message->to].out_buffer, message, msg_size);
			}
		}
	} else {
		_sock_buffer_add(&sock_conns[sock_self_id].out_buffer, message, msg_size);
	}
	
	// send to self
	_sock_on_receive(*message, &message[1]);
	free(message);
}

///////////////////////////////////////////

int32_t sock_start_server() {
	char      port_str[32];
	addrinfo *address = nullptr;
	addrinfo  hints;

	// convert the port to a string
	sprintf_s(port_str, "%hu", sock_port);

	// Create socket as server
	hints = {};
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags    = AI_PASSIVE;
	if (getaddrinfo(nullptr, port_str, &hints, &address) != 0) 
		return -1;
	
	// create, bind, and begin listening on the socket
	int32_t result = 1;
	SOCKET sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
	if (sock == INVALID_SOCKET
		|| bind  (sock, address->ai_addr, (int)address->ai_addrlen) == SOCKET_ERROR
		|| listen(sock, SOMAXCONN                                 ) == SOCKET_ERROR) {
		result = -2;
	}
	
	freeaddrinfo(address);

	sock_server  = true;
	sock_self_id = 0;
	sock_conns[sock_self_id].sock = sock;
	sock_conns[sock_self_id].type = sock_conn_type_primary;
	_sock_buffer_create(&sock_conns[sock_self_id].in_buffer);
	_sock_buffer_create(&sock_conns[sock_self_id].out_buffer);
	sock_conn_count += 1;

	// Create a discovery socket, so people can find us on the network
	_sock_multicast_begin();

	// Notify everyone (mostly just self) of the new connection
	sock_conn_event_t evt = {};
	evt.id     = sock_self_id;
	evt.status = sock_connect_status_joined;
	sock_send(sock_hash_type(sock_conn_event_t), sizeof(evt), &evt);

	return 1;
}

///////////////////////////////////////////

int32_t sock_start_client(const char *ip) {
	char      port_str[32];
	addrinfo *address = nullptr;
	addrinfo  hints;

	// convert the port to a string
	sprintf_s(port_str, "%hu", sock_port);

	// Create socket as client
	hints = {};
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	if (getaddrinfo(ip, port_str, &hints, &address) != 0)
		return -2;

	SOCKET sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
	if (sock == INVALID_SOCKET) {
		freeaddrinfo(address);
		return -3;
	}
	if (connect(sock, address->ai_addr, (int)address->ai_addrlen) == SOCKET_ERROR) {
		closesocket(sock);
		sock = INVALID_SOCKET;
	}
	freeaddrinfo(address);
	if (sock == INVALID_SOCKET) {
		return -4;
	}

	// get a connection id from the server
	sock_initial_data_t initial = {};
	if (recv(sock, (char *)&initial, sizeof(initial), 0) != sizeof(initial)) {
		closesocket(sock);
		return -5;
	}

	// Make sure we've got a connection from something that looks about right
	if (strcmp(initial.id, "warm_sock") != 0 || initial.app_id != sock_app_id) {
		closesocket(sock);
		return -6;
	}

	sock_self_id = initial.conn_id;
	sock_server = false;
	sock_conns[sock_self_id].sock = sock;
	sock_conns[sock_self_id].type = sock_conn_type_primary;
	_sock_buffer_create(&sock_conns[sock_self_id].in_buffer);
	_sock_buffer_create(&sock_conns[sock_self_id].out_buffer);
	sock_conn_count += 1;

	return 1;
}

///////////////////////////////////////////

int32_t _sock_server_new_connection() {
	sockaddr_in address;
	int32_t     address_size = sizeof(sockaddr_in);
	SOCKET      new_client   = accept(sock_conns[sock_self_id].sock, (sockaddr*)&address, &address_size);
	if (new_client == INVALID_SOCKET)
		return -1;

	// Find a free slot in our connections
	sock_connection_id id = -1;
	for (sock_connection_id i = 0; i < _countof(sock_conns); i++) {
		if (sock_conns[i].type == sock_conn_type_free) {
			id = i;
			break;
		}
	}

	// store the connection
	if (id != -1) {
		sock_conns[id].sock = new_client;
		sock_conns[id].type = sock_conn_type_client;
		_sock_buffer_create(&sock_conns[id].in_buffer);
		_sock_buffer_create(&sock_conns[id].out_buffer);
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
	sock_initial_data_t initial = {"warm_sock"};
	initial.app_id  = sock_app_id;
	initial.conn_id = id;
	send(new_client, (char *)&initial, sizeof(initial), 0);

	// Notify everyone of the new connection
	sock_conn_event_t evt = {};
	evt.id     = id;
	evt.status = sock_connect_status_joined;
	sock_send(sock_hash_type(sock_conn_event_t), sizeof(evt), &evt);

	return 1;
}

///////////////////////////////////////////

void _sock_buffer_submit(sock_buffer_t *buffer) {
	sock_header_t *head   = (sock_header_t*)buffer->data;
	int32_t        length = head->data_size + sizeof(sock_header_t);
	while (buffer->curr >= length) {
		if (sock_server) {
			_sock_send_ex(*head, &head[1]);
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
	FD_SET(sock_discovery, &fd_read);
	int32_t count = 0;
	for (sock_connection_id i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
		if (sock_conns[i].type == sock_conn_type_free) continue;
		count += 1;
		FD_SET(sock_conns[i].sock, &fd_except);
		if (sock_conns[i].in_buffer .curr < sock_conns[i].in_buffer.size) FD_SET(sock_conns[i].sock, &fd_read);
		if (sock_conns[i].out_buffer.curr > 0                           ) FD_SET(sock_conns[i].sock, &fd_write);
	}

	// 'select' will check all the FD_SET sockets to see if any of them are
	// ready for read/write/exception information
	timeval time = {};
	if (select(0, &fd_read, &fd_write, &fd_except, &time) > 0) {

		// Check our connection discovery socket
		if (FD_ISSET(sock_discovery, &fd_read)) {
			_sock_multicast_step();
			FD_CLR(sock_discovery, &fd_read);
		}

		count = 0;
		for (sock_connection_id i = 0; i < _countof(sock_conns) && count < sock_conn_count; i++) {
			sock_conn_t &conn = sock_conns[i];
			if (conn.type == sock_conn_type_free) continue;
			count += 1;

			if (conn.type == sock_conn_type_primary) {
				// Check for connecting clients
				if (FD_ISSET(conn.sock, &fd_except)) {
					result = false;
					printf("primary socket failed with error: %d\n", WSAGetLastError());
					FD_CLR(conn.sock, &fd_except);
				} else if (FD_ISSET(conn.sock, &fd_read)) {
					_sock_server_new_connection();
					FD_CLR(conn.sock, &fd_read);
				}
			} else {
				// Receive and send data to any client that's got something
				if (FD_ISSET(conn.sock, &fd_except)) {
					_sock_connection_close(i, true);
					FD_CLR(conn.sock, &fd_except);
				} else {
					if (FD_ISSET(conn.sock, &fd_read)) {
						data_size = recv(conn.sock, &conn.in_buffer.data[conn.in_buffer.curr], conn.in_buffer.size - conn.in_buffer.curr, 0);
						FD_CLR(conn.sock, &fd_read);
						if (data_size < 1) {
							if (data_size < 0)
								printf("recv failed with error: %d\n", WSAGetLastError());
							_sock_connection_close(i, true);
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
	}

	return result;
}

///////////////////////////////////////////

bool _sock_client_poll() {
	sock_conn_t &conn      = sock_conns[sock_self_id];
	int32_t      data_size = 0;
	bool         result    = true;

	static fd_set fd_read, fd_write, fd_except;
	FD_ZERO(&fd_read);
	FD_ZERO(&fd_write);
	FD_ZERO(&fd_except);

	FD_SET(conn.sock, &fd_except);
	FD_SET(conn.sock, &fd_read);
	FD_SET(conn.sock, &fd_write);

	timeval time = {};
	if (select(0, &fd_read, &fd_write, &fd_except, &time) > 0) {
		

		if (FD_ISSET(conn.sock, &fd_except)) {
			printf("primary socket failed with error: %d\n", WSAGetLastError());
			result = false;
			FD_CLR(conn.sock, &fd_except);
		} else {
			if (FD_ISSET(conn.sock, &fd_read)) {
				data_size = recv(conn.sock, (char*)&conn.in_buffer.data[conn.in_buffer.curr], conn.in_buffer.size - conn.in_buffer.curr, 0);
				if (data_size < 0) {
					printf("recv failed with error: %d\n", WSAGetLastError());
					result = false;
				} else {
					conn.in_buffer.curr += data_size;
					_sock_buffer_submit(&conn.in_buffer);
				}
				FD_CLR(conn.sock, &fd_read);
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
	return result;
}

///////////////////////////////////////////

bool sock_poll() {
	return sock_server
		? _sock_server_poll()
		: _sock_client_poll();
}

///////////////////////////////////////////

void _sock_on_receive(sock_header_t header, const void *data) {
	if (header.to != -1 && header.to != sock_self_id)
		return;

	if (header.data_id == sock_hash_type(sock_conn_event_t)) {
		const sock_conn_event_t *evt = (sock_conn_event_t*)data;
		if (sock_on_connection_callback) {
			sock_on_connection_callback(evt->id, evt->status);
		}
	} else if (sock_on_receive_callback) {
		sock_on_receive_callback(header, data);
	}
}

///////////////////////////////////////////

bool sock_is_server() {
	return sock_server;
}

///////////////////////////////////////////

sock_connection_id sock_get_id() {
	return sock_self_id;
}

///////////////////////////////////////////

void sock_on_receive(void (*on_receive)(sock_header_t header, const void *data)) {
	sock_on_receive_callback = on_receive;
}

///////////////////////////////////////////

void sock_on_connection(void (*on_connection)(sock_connection_id id, sock_connect_status_ status)) {
	sock_on_connection_callback = on_connection;
}

///////////////////////////////////////////

// https://gist.github.com/hostilefork/f7cae3dc33e7416f2dd25a402857b6c6
void _sock_multicast_begin() {
	sock_discovery = socket(AF_INET, SOCK_DGRAM, 0);

	sockaddr_in addr = {};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(sock_port+1);
	bind(sock_discovery, (sockaddr *)&addr, sizeof(addr));

	ip_mreq mreq;
	mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.1");
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(sock_discovery, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq));
}

///////////////////////////////////////////

void _sock_multicast_end() {
	closesocket(sock_discovery);
}

///////////////////////////////////////////

bool _sock_multicast_step() {
	char        buffer[1024];
	sockaddr_in addr = {};
	int         addrlen = sizeof(addr);
	int         bytes   = recvfrom(sock_discovery, buffer, _countof(buffer), 0, (sockaddr *) &addr, &addrlen );
	if (bytes >= sizeof(sock_initial_data_t)) {
		sock_initial_data_t *data = (sock_initial_data_t *)buffer;

		// Check if it's intended for us
		if (strcmp(data->id, "warm_sock") == 0 && data->app_id == sock_app_id) {
			const char *message = "Welcome!";
			bytes = sendto(sock_discovery, message, strlen(message)+1, 0, (sockaddr*)&addr, sizeof(addr) );
			if (bytes < 1) {
				return false;
			}
		}
	} else if (bytes < 1) {
		return false;
	}

	return true;
}

///////////////////////////////////////////

bool sock_find_server(char *out_address, int32_t out_address_size) {
	sock_discovery = socket(AF_INET, SOCK_DGRAM, 0);

	sockaddr_in addr = {};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = inet_addr("224.0.0.1");
	addr.sin_port        = htons(sock_port+1);
	bind(sock_discovery, (sockaddr *)&addr, sizeof(addr));

	// Send off a hello message
	sock_initial_data_t data = { "warm_sock" };
	data.app_id  = sock_app_id;
	data.conn_id = 0;
	int nbytes = sendto(sock_discovery, (char*)&data, sizeof(data), 0, (sockaddr*)&addr, sizeof(addr) );
	if (nbytes < 1) {
		return false;
	}

	fd_set fd_read, fd_write, fd_except;
	FD_ZERO(&fd_read);
	FD_ZERO(&fd_write);
	FD_ZERO(&fd_except);

	FD_SET(sock_discovery, &fd_read);

	timeval time   = {0,500*1000}; // only wait 500ms for an answer
	bool    result = false;
	if (select(0, &fd_read, &fd_write, &fd_except, &time) > 0) {
		// Wait for a response
		if (FD_ISSET(sock_discovery, &fd_read)) {
			const char  expected[] = "Welcome!";
			char        buffer[_countof(expected)];
			sockaddr_in server_addr = {};
			int         addrlen = sizeof(server_addr);
			int         bytes = recvfrom(sock_discovery, buffer, _countof(buffer), 0, (sockaddr *)&server_addr, &addrlen);
			if (bytes >= _countof(expected)) {
				if (strcmp(expected, buffer) == 0) {
					strcpy_s(out_address, out_address_size, inet_ntoa(server_addr.sin_addr));
					result = true;
				}
			}
			FD_CLR(sock_discovery, &fd_read);
		}
	}
	
	closesocket(sock_discovery);
	return result;
}

#pragma warning( pop )

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