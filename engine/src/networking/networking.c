#include "eisenfront/networking.h"

#include <stdlib.h>
#include <string.h>

#include <enet/enet.h>

#define NET_LOG_CATEGORY "networking"

#define NET_PEER_INDEX_BITS 16u
#define NET_PEER_INDEX_MASK ((1u << NET_PEER_INDEX_BITS) - 1u)
#define NET_MAX_PEER_GENERATION ((1u << (32u - NET_PEER_INDEX_BITS)) - 1u)

struct NetHost {
    ENetHost   *enet_host;
    uint32_t   *generations; /* one per ENet peer slot (enet_host->peers[index]) */
    bool       *active;
    uint32_t    peer_count;
    NetEventFn  event_callback;
    void       *event_userdata;
};

/* ENet's own peer array is 0-based, but id 0 is reserved for
 * NET_INVALID_PEER_ID (exactly like BodyId/SoundId reserve slot 0 by
 * starting their free-lists at 1) - so every raw ENet peer array index
 * is stored as index+1 here and converted back on the way out, keeping
 * a legitimately active peer at raw index 0 from ever encoding to the
 * all-zero invalid id. */
static uint32_t peer_index_of(NetPeerId id) {
    return (id & NET_PEER_INDEX_MASK) - 1u;
}
static uint32_t peer_generation_of(NetPeerId id) {
    return id >> NET_PEER_INDEX_BITS;
}
static NetPeerId make_peer_id(uint32_t index, uint32_t generation) {
    return (NetPeerId)((generation << NET_PEER_INDEX_BITS) | ((index + 1u) & NET_PEER_INDEX_MASK));
}

static uint32_t enet_peer_index(const NetHost *host, const ENetPeer *peer) {
    return (uint32_t)(peer - host->enet_host->peers);
}

Result net_system_init(void) {
    if (enet_initialize() != 0) {
        LOG_ERROR(NET_LOG_CATEGORY, "enet_initialize() failed");
        return RESULT_ERROR_MODULE_INIT_FAILED;
    }
    return RESULT_OK;
}

void net_system_shutdown(void) {
    enet_deinitialize();
}

/* ==================================================================== *
 * Addresses
 * ==================================================================== */

Result net_address_from_string(const char *host, uint16_t port, NetAddress *out_address) {
    if (host == nullptr || out_address == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    ENetAddress address;
    if (enet_address_set_host(&address, host) != 0) {
        return RESULT_ERROR_NOT_FOUND;
    }
    out_address->ipv4 = address.host;
    out_address->port = port;
    return RESULT_OK;
}

void net_address_any(uint16_t port, NetAddress *out_address) {
    ASSERT(out_address != nullptr);
    if (out_address == nullptr) {
        return;
    }
    out_address->ipv4 = ENET_HOST_ANY;
    out_address->port = port;
}

Result net_address_to_string(NetAddress address, char *out_buffer, size_t buffer_size) {
    if (out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    ENetAddress enet_address = {.host = address.ipv4, .port = address.port};
    if (enet_address_get_host_ip(&enet_address, out_buffer, buffer_size) != 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    return RESULT_OK;
}

/* ==================================================================== *
 * Hosts
 * ==================================================================== */

NetHostDesc net_host_desc_default(void) {
    return (NetHostDesc){
        .bind_address = nullptr,
        .max_peers = 32,
        .channel_count = 1,
        .incoming_bandwidth = 0,
        .outgoing_bandwidth = 0,
    };
}

Result net_host_create(const NetHostDesc *desc, NetHost **out_host) {
    if (out_host == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    const NetHostDesc effective = (desc != nullptr) ? *desc : net_host_desc_default();
    if (effective.max_peers == 0 || effective.max_peers > NET_PEER_INDEX_MASK - 1u ||
        effective.channel_count == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    NetHost *host = calloc(1, sizeof(*host));
    if (host == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    ENetAddress enet_address = {0};
    ENetAddress *enet_address_ptr = nullptr;
    if (effective.bind_address != nullptr) {
        enet_address.host = effective.bind_address->ipv4;
        enet_address.port = effective.bind_address->port;
        enet_address_ptr = &enet_address;
    }

    host->enet_host = enet_host_create(enet_address_ptr, effective.max_peers,
                                        effective.channel_count, effective.incoming_bandwidth,
                                        effective.outgoing_bandwidth);
    if (host->enet_host == nullptr) {
        LOG_ERROR(NET_LOG_CATEGORY, "enet_host_create() failed");
        free(host);
        return RESULT_ERROR_MODULE_INIT_FAILED;
    }

    host->peer_count = effective.max_peers;
    host->generations = calloc(effective.max_peers, sizeof(uint32_t));
    host->active = calloc(effective.max_peers, sizeof(bool));
    if (host->generations == nullptr || host->active == nullptr) {
        free(host->generations);
        free(host->active);
        enet_host_destroy(host->enet_host);
        free(host);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    *out_host = host;
    return RESULT_OK;
}

void net_host_destroy(NetHost *host) {
    if (host == nullptr) {
        return;
    }
    free(host->generations);
    free(host->active);
    enet_host_destroy(host->enet_host);
    free(host);
}

Result net_host_connect(NetHost *host, NetAddress address, uint32_t channel_count,
                         uint32_t user_data, NetPeerId *out_peer) {
    if (host == nullptr || out_peer == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    ENetAddress enet_address = {.host = address.ipv4, .port = address.port};
    ENetPeer   *peer = enet_host_connect(host->enet_host, &enet_address, channel_count, user_data);
    if (peer == nullptr) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    const uint32_t index = enet_peer_index(host, peer);
    host->active[index] = true;
    *out_peer = make_peer_id(index, host->generations[index]);
    return RESULT_OK;
}

bool net_peer_is_valid(const NetHost *host, NetPeerId peer) {
    if (host == nullptr || peer == NET_INVALID_PEER_ID) {
        return false;
    }
    const uint32_t index = peer_index_of(peer);
    if (index >= host->peer_count) {
        return false;
    }
    return host->active[index] && host->generations[index] == peer_generation_of(peer);
}

static void invalidate_peer(NetHost *host, uint32_t index) {
    host->active[index] = false;
    host->generations[index] =
        (host->generations[index] < NET_MAX_PEER_GENERATION) ? host->generations[index] + 1 : 0;
}

void net_peer_disconnect(NetHost *host, NetPeerId peer, uint32_t user_data) {
    if (!net_peer_is_valid(host, peer)) {
        return;
    }
    enet_peer_disconnect(&host->enet_host->peers[peer_index_of(peer)], user_data);
}

void net_peer_disconnect_now(NetHost *host, NetPeerId peer, uint32_t user_data) {
    if (!net_peer_is_valid(host, peer)) {
        return;
    }
    const uint32_t index = peer_index_of(peer);
    enet_peer_disconnect_now(&host->enet_host->peers[index], user_data);
    invalidate_peer(host, index);
}

void net_peer_reset(NetHost *host, NetPeerId peer) {
    if (!net_peer_is_valid(host, peer)) {
        return;
    }
    const uint32_t index = peer_index_of(peer);
    enet_peer_reset(&host->enet_host->peers[index]);
    invalidate_peer(host, index);
}

Result net_peer_send(NetHost *host, NetPeerId peer, uint8_t channel, const void *data, size_t size,
                      uint32_t flags) {
    if (!net_peer_is_valid(host, peer) || data == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    enet_uint32 enet_flags = 0;
    if (flags & NET_PACKET_FLAG_RELIABLE) {
        enet_flags |= ENET_PACKET_FLAG_RELIABLE;
    }
    if (flags & NET_PACKET_FLAG_UNSEQUENCED) {
        enet_flags |= ENET_PACKET_FLAG_UNSEQUENCED;
    }

    ENetPacket *packet = enet_packet_create(data, size, enet_flags);
    if (packet == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    if (enet_peer_send(&host->enet_host->peers[peer_index_of(peer)], channel, packet) != 0) {
        enet_packet_destroy(packet);
        return RESULT_ERROR_IO;
    }
    return RESULT_OK;
}

void net_host_flush(NetHost *host) {
    ASSERT(host != nullptr);
    if (host == nullptr) {
        return;
    }
    enet_host_flush(host->enet_host);
}

void net_host_set_event_callback(NetHost *host, NetEventFn fn, void *userdata) {
    ASSERT(host != nullptr);
    if (host == nullptr) {
        return;
    }
    host->event_callback = fn;
    host->event_userdata = userdata;
}

void net_host_service(NetHost *host, uint32_t timeout_ms) {
    ASSERT(host != nullptr);
    if (host == nullptr) {
        return;
    }

    ENetEvent event;
    while (enet_host_service(host->enet_host, &event, timeout_ms) > 0) {
        const uint32_t index = enet_peer_index(host, event.peer);

        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                /* Fires both when our own net_host_connect() reaches
                 * CONNECTED and when a remote peer connects to us; only
                 * the latter hasn't already been marked active. */
                if (!host->active[index]) {
                    host->active[index] = true;
                }
                if (host->event_callback != nullptr) {
                    const NetEventData data = {
                        .type = NET_EVENT_CONNECT,
                        .peer = make_peer_id(index, host->generations[index]),
                        .peer_user_data = event.data,
                        .data = nullptr,
                        .size = 0,
                        .channel = 0,
                    };
                    host->event_callback(&data, host->event_userdata);
                }
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                const NetPeerId id = make_peer_id(index, host->generations[index]);
                invalidate_peer(host, index);
                if (host->event_callback != nullptr) {
                    const NetEventData data = {
                        .type = NET_EVENT_DISCONNECT,
                        .peer = id,
                        .peer_user_data = event.data,
                        .data = nullptr,
                        .size = 0,
                        .channel = 0,
                    };
                    host->event_callback(&data, host->event_userdata);
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                if (host->event_callback != nullptr) {
                    const NetEventData data = {
                        .type = NET_EVENT_RECEIVE,
                        .peer = make_peer_id(index, host->generations[index]),
                        .peer_user_data = 0,
                        .data = event.packet->data,
                        .size = event.packet->dataLength,
                        .channel = event.channelID,
                    };
                    host->event_callback(&data, host->event_userdata);
                }
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_NONE:
                break;
        }
    }
}
