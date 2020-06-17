# warm-sock
![warm_sock logo](img/warm_sock_logo_wide.png)

A single header high-level socket/networking library for building server/client multi-user experiences.

Still in development, more info to follow! This is designed to be the networking component of [StereoKit](https://stereokit.net), so it needs to be able to handle networking scenarios common to shared Mixed Reality experiences.

## Features

- [x] Simple API
- [x] Server/Client architecture
- [x] LAN discovery
- [x] Non-blocking core loop
- [x] Send to one/send to all
- [x] Connect/disconnect events
- [x] Easy send/receive structs
- [ ] Server can be transferred to a client
- [ ] Send large data

## Example usage

For full example, see [example.cpp](example.cpp). But here's a quick taste of the core experience!

```C
// Set up our callback functions
sock_on_connection(on_connection);
sock_on_receive([](sock_header_t header, const void *data) {
    switch (header.data_id) {
    case sock_hash_type(test_data_t): {

        const test_data_t *test = (const test_data_t *)data;
        printf("test_data_t: [%.2f, %.2f, %.2f], [%.2f, %.2f, %.2f]\n",
            test->position [0], test->position [1], test->position [2],
            test->direction[0], test->direction[1], test->direction[2]);

    }
    default: break;
    }
});

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
while (sock_poll()) {
    if (_kbhit() != 0) {

        // Send a message using a struct
        test_data_t test = { {1, 2, 3}, {3, 2, 1} };
        sock_send(sock_hash_type(test_data_t), sizeof(test), &test);

    }
    Sleep(1);
}

sock_shutdown();
```

## License

MIT or Public Domain. See bottom of warm_sock.h for details.