/*
 * Networking is exercised over real loopback UDP sockets (127.0.0.1) -
 * no mocking of ENet itself, since the sandbox's loopback interface is
 * always available even when there is no external network.
 */
#include "eisenfront/networking.h"

#include "unity.h"

#include <string.h>

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, net_system_init());
}

void tearDown(void) {
    net_system_shutdown();
}

typedef struct EventLog {
    int      connect_count;
    int      disconnect_count;
    int      receive_count;
    NetPeerId last_peer;
    uint32_t  last_user_data;
    uint8_t   last_data[256];
    size_t    last_size;
    uint8_t   last_channel;
} EventLog;

static void log_event(const NetEventData *event, void *userdata) {
    EventLog *log = (EventLog *)userdata;
    log->last_peer = event->peer;
    switch (event->type) {
        case NET_EVENT_CONNECT:
            log->connect_count += 1;
            log->last_user_data = event->peer_user_data;
            break;
        case NET_EVENT_DISCONNECT:
            log->disconnect_count += 1;
            log->last_user_data = event->peer_user_data;
            break;
        case NET_EVENT_RECEIVE:
            log->receive_count += 1;
            log->last_size = event->size < sizeof(log->last_data) ? event->size : sizeof(log->last_data);
            memcpy(log->last_data, event->data, log->last_size);
            log->last_channel = event->channel;
            break;
    }
}

static void pump_until(NetHost *a, EventLog *log_a, NetHost *b, EventLog *log_b,
                        int (*done)(const EventLog *a, const EventLog *b), int max_iterations) {
    for (int i = 0; i < max_iterations; ++i) {
        net_host_service(a, 50);
        if (b != nullptr) {
            net_host_service(b, 50);
        }
        if (done(log_a, log_b)) {
            return;
        }
    }
}

static int done_both_connected(const EventLog *a, const EventLog *b) {
    return a->connect_count >= 1 && b->connect_count >= 1;
}

static int done_a_received(const EventLog *a, const EventLog *b) {
    (void)b;
    return a->receive_count >= 1;
}

static int done_both_disconnected(const EventLog *a, const EventLog *b) {
    return a->disconnect_count >= 1 && b->disconnect_count >= 1;
}

static void make_loopback_pair(uint16_t port, NetHost **out_server, NetHost **out_client) {
    NetAddress bind_address;
    net_address_any(port, &bind_address);

    NetHostDesc server_desc = net_host_desc_default();
    server_desc.bind_address = &bind_address;
    server_desc.max_peers = 8;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_create(&server_desc, out_server));

    NetHostDesc client_desc = net_host_desc_default();
    client_desc.max_peers = 8;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_create(&client_desc, out_client));
}

static void test_create_destroy_client_host(void) {
    NetHost *host = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_create(nullptr, &host));
    TEST_ASSERT_NOT_NULL(host);
    net_host_destroy(host);
}

static void test_create_server_host_with_bind_address(void) {
    NetAddress bind_address;
    net_address_any(34561, &bind_address);
    NetHostDesc desc = net_host_desc_default();
    desc.bind_address = &bind_address;

    NetHost *host = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_create(&desc, &host));
    net_host_destroy(host);
}

static void test_address_from_string_and_any(void) {
    NetAddress loopback;
    TEST_ASSERT_EQUAL(RESULT_OK, net_address_from_string("127.0.0.1", 1234, &loopback));
    TEST_ASSERT_EQUAL(1234, loopback.port);

    char buffer[64];
    TEST_ASSERT_EQUAL(RESULT_OK, net_address_to_string(loopback, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", buffer);

    NetAddress any;
    net_address_any(4321, &any);
    TEST_ASSERT_EQUAL(4321, any.port);
}

static void test_connect_fires_events_on_both_sides(void) {
    NetHost *server = nullptr;
    NetHost *client = nullptr;
    make_loopback_pair(34562, &server, &client);

    EventLog server_log = {0};
    EventLog client_log = {0};
    net_host_set_event_callback(server, log_event, &server_log);
    net_host_set_event_callback(client, log_event, &client_log);

    NetAddress server_address;
    net_address_from_string("127.0.0.1", 34562, &server_address);
    NetPeerId client_side_peer = NET_INVALID_PEER_ID;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       net_host_connect(client, server_address, 1, 0xABCDu, &client_side_peer));
    TEST_ASSERT_TRUE(net_peer_is_valid(client, client_side_peer));

    pump_until(client, &client_log, server, &server_log, done_both_connected, 40);

    TEST_ASSERT_EQUAL(1, client_log.connect_count);
    TEST_ASSERT_EQUAL(1, server_log.connect_count);
    TEST_ASSERT_EQUAL_UINT32(0xABCDu, server_log.last_user_data);

    net_host_destroy(client);
    net_host_destroy(server);
}

static void test_reliable_message_roundtrip(void) {
    NetHost *server = nullptr;
    NetHost *client = nullptr;
    make_loopback_pair(34563, &server, &client);

    EventLog server_log = {0};
    EventLog client_log = {0};
    net_host_set_event_callback(server, log_event, &server_log);
    net_host_set_event_callback(client, log_event, &client_log);

    NetAddress server_address;
    net_address_from_string("127.0.0.1", 34563, &server_address);
    NetPeerId peer = NET_INVALID_PEER_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_connect(client, server_address, 1, 0, &peer));
    pump_until(client, &client_log, server, &server_log, done_both_connected, 40);
    TEST_ASSERT_EQUAL(1, server_log.connect_count);

    const char *message = "hello from the client";
    TEST_ASSERT_EQUAL(RESULT_OK, net_peer_send(client, peer, 0, message, strlen(message) + 1,
                                                NET_PACKET_FLAG_RELIABLE));
    net_host_flush(client);

    pump_until(server, &server_log, nullptr, nullptr, done_a_received, 40);

    TEST_ASSERT_EQUAL(1, server_log.receive_count);
    TEST_ASSERT_EQUAL_STRING(message, (const char *)server_log.last_data);
    TEST_ASSERT_EQUAL(0, server_log.last_channel);

    net_host_destroy(client);
    net_host_destroy(server);
}

static void test_graceful_disconnect_invalidates_after_event(void) {
    NetHost *server = nullptr;
    NetHost *client = nullptr;
    make_loopback_pair(34564, &server, &client);

    EventLog server_log = {0};
    EventLog client_log = {0};
    net_host_set_event_callback(server, log_event, &server_log);
    net_host_set_event_callback(client, log_event, &client_log);

    NetAddress server_address;
    net_address_from_string("127.0.0.1", 34564, &server_address);
    NetPeerId peer = NET_INVALID_PEER_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_connect(client, server_address, 1, 0, &peer));
    pump_until(client, &client_log, server, &server_log, done_both_connected, 40);

    net_peer_disconnect(client, peer, 0);
    /* Handle stays valid until the DISCONNECT event actually arrives -
     * see the file header comment on handle lifecycle. */
    pump_until(client, &client_log, server, &server_log, done_both_disconnected, 40);

    TEST_ASSERT_EQUAL(1, client_log.disconnect_count);
    TEST_ASSERT_EQUAL(1, server_log.disconnect_count);
    TEST_ASSERT_FALSE(net_peer_is_valid(client, peer));

    net_host_destroy(client);
    net_host_destroy(server);
}

static void test_peer_reset_invalidates_synchronously(void) {
    NetHost *client = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_create(nullptr, &client));

    NetAddress unreachable;
    net_address_from_string("127.0.0.1", 34599, &unreachable);
    NetPeerId peer = NET_INVALID_PEER_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_connect(client, unreachable, 1, 0, &peer));
    TEST_ASSERT_TRUE(net_peer_is_valid(client, peer));

    net_peer_reset(client, peer);
    TEST_ASSERT_FALSE(net_peer_is_valid(client, peer));

    net_host_destroy(client);
}

static void test_connect_capacity_exceeded(void) {
    NetHostDesc desc = net_host_desc_default();
    desc.max_peers = 1;
    NetHost *client = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_create(&desc, &client));

    NetAddress unreachable_a;
    net_address_from_string("127.0.0.1", 34598, &unreachable_a);
    NetAddress unreachable_b;
    net_address_from_string("127.0.0.1", 34597, &unreachable_b);

    NetPeerId first = NET_INVALID_PEER_ID;
    NetPeerId second = NET_INVALID_PEER_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_connect(client, unreachable_a, 1, 0, &first));
    TEST_ASSERT_EQUAL(RESULT_ERROR_CAPACITY_EXCEEDED,
                       net_host_connect(client, unreachable_b, 1, 0, &second));

    net_host_destroy(client);
}

static void test_invalid_handles_are_safe(void) {
    NetHost *host = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, net_host_create(nullptr, &host));

    TEST_ASSERT_FALSE(net_peer_is_valid(host, NET_INVALID_PEER_ID));
    TEST_ASSERT_FALSE(net_peer_is_valid(host, (NetPeerId)9999));

    const char *payload = "x";
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT,
                       net_peer_send(host, (NetPeerId)9999, 0, payload, 1, 0));

    /* No-ops, not crashes. */
    net_peer_disconnect(host, (NetPeerId)9999, 0);
    net_peer_disconnect_now(host, (NetPeerId)9999, 0);
    net_peer_reset(host, (NetPeerId)9999);

    net_host_destroy(host);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_destroy_client_host);
    RUN_TEST(test_create_server_host_with_bind_address);
    RUN_TEST(test_address_from_string_and_any);
    RUN_TEST(test_connect_fires_events_on_both_sides);
    RUN_TEST(test_reliable_message_roundtrip);
    RUN_TEST(test_graceful_disconnect_invalidates_after_event);
    RUN_TEST(test_peer_reset_invalidates_synchronously);
    RUN_TEST(test_connect_capacity_exceeded);
    RUN_TEST(test_invalid_handles_are_safe);

    return UNITY_END();
}
