/*
 * Networking - reliable/unreliable UDP messaging over ENet, wrapped
 * behind an instance-owned NetHost (one process may run several, e.g.
 * a listen server plus a loopback client) exactly like PhysicsWorld
 * owns its bodies and AudioEngine owns its sounds: peers are addressed
 * through generational NetPeerId handles rather than raw ENetPeer
 * pointers, and events are delivered via a poll-and-callback pump
 * (net_host_service()) matching Window's window_poll_events() /
 * window_set_event_callback() pattern rather than exposing ENet's
 * ENetEvent directly.
 *
 * A NetHost with a non-null bind_address in its NetHostDesc accepts
 * incoming connections (a server); one created with bind_address ==
 * nullptr only ever dials out via net_host_connect() (a client). Both
 * cases use the same NetHost type and the same event pump - ENet
 * itself makes no client/server distinction at the host level, and
 * neither does this wrapper.
 *
 * Handle lifecycle: a NetPeerId becomes valid the moment it is
 * returned (from net_host_connect(), or from the NET_EVENT_CONNECT
 * event for an incoming peer) and is invalidated - generation bumped,
 * exactly like BodyId/SoundId - at the point disconnection is known to
 * be final. For a graceful net_peer_disconnect(), that point is the
 * later NET_EVENT_DISCONNECT event (ENet must complete a handshake
 * first); for net_peer_disconnect_now() and net_peer_reset(), ENet
 * itself never raises a local event at all, so this wrapper invalidates
 * the handle synchronously, inside that same call.
 *
 * Scope, stated plainly rather than left implicit:
 *   - IPv4 only (ENet 1.3's addressing model; no IPv6).
 *   - No built-in encryption - ENet packets are sent in the clear.
 *     Wiring in DTLS or an ENet fork that supports it is a later,
 *     separate pass, not something this module attempts.
 *   - Address resolution is synchronous and blocks the calling thread
 *     (net_address_from_string() calls through to ENet's own resolver,
 *     which in turn is a blocking getaddrinfo/gethostbyname call) -
 *     call it during connection setup, not on a latency-sensitive path.
 *
 * Dependencies: Core, Platform; ENet (privately, in networking.c only).
 */
#ifndef EISENFRONT_NETWORKING_H
#define EISENFRONT_NETWORKING_H

#include "core.h"

/* Process-global: wraps ENet's own required global init. Call once
 * before any NetHost exists; net_system_shutdown() after every NetHost
 * has been destroyed. */
ENGINE_API Result net_system_init(void);
ENGINE_API void   net_system_shutdown(void);

/* ==================================================================== *
 * Addresses
 * ==================================================================== */

typedef struct NetAddress {
    uint32_t ipv4; /* opaque; populate only via net_address_from_string()/net_address_any() */
    uint16_t port;
} NetAddress;

/* host may be a dotted-quad ("127.0.0.1") or a hostname to resolve
 * ("example.com") - see the file header comment on this blocking. */
ENGINE_API Result net_address_from_string(const char *host, uint16_t port, NetAddress *out_address);
/* A bind address meaning "every local interface" - the normal choice
 * for NetHostDesc.bind_address on a server. */
ENGINE_API void net_address_any(uint16_t port, NetAddress *out_address);
/* Numeric dotted-quad only (e.g. "192.168.1.4") - never performs a
 * reverse DNS lookup. */
ENGINE_API Result net_address_to_string(NetAddress address, char *out_buffer, size_t buffer_size);

/* ==================================================================== *
 * Hosts
 * ==================================================================== */

typedef struct NetHostDesc {
    const NetAddress *bind_address; /* nullptr: outgoing-only (client); non-null: accepts incoming (server) */
    uint32_t          max_peers;
    uint32_t          channel_count;      /* at least 1 */
    uint32_t          incoming_bandwidth; /* bytes/sec, 0 = unlimited */
    uint32_t          outgoing_bandwidth; /* bytes/sec, 0 = unlimited */
} NetHostDesc;

ENGINE_API NetHostDesc net_host_desc_default(void);

typedef struct NetHost NetHost;

ENGINE_API Result net_host_create(const NetHostDesc *desc, NetHost **out_host);
ENGINE_API void   net_host_destroy(NetHost *host);

typedef uint32_t NetPeerId;
#define NET_INVALID_PEER_ID ((NetPeerId)0)

/* Begins connecting; returns a valid (but not yet NET_EVENT_CONNECT-ed)
 * peer handle immediately - see the file header comment on handle
 * lifecycle. user_data is delivered to the remote end's NET_EVENT_CONNECT. */
ENGINE_API Result net_host_connect(NetHost *host, NetAddress address, uint32_t channel_count,
                                    uint32_t user_data, NetPeerId *out_peer);

ENGINE_API bool net_peer_is_valid(const NetHost *host, NetPeerId peer);

/* Graceful: queues a disconnect notification to the remote end; the
 * handle stays valid until the resulting NET_EVENT_DISCONNECT. */
ENGINE_API void net_peer_disconnect(NetHost *host, NetPeerId peer, uint32_t user_data);
/* Immediate: notifies the remote end once (best-effort, unacknowledged)
 * and invalidates the handle synchronously - no later event fires. */
ENGINE_API void net_peer_disconnect_now(NetHost *host, NetPeerId peer, uint32_t user_data);
/* Immediate and silent: the remote end is never notified. Invalidates
 * the handle synchronously - no later event fires. */
ENGINE_API void net_peer_reset(NetHost *host, NetPeerId peer);

typedef enum NetPacketFlags {
    /* Guaranteed delivery, in order, per channel. Omit both flags for
     * ENet's default: unreliable but still sequenced (a stale packet
     * that arrives after a newer one on the same channel is dropped). */
    NET_PACKET_FLAG_RELIABLE = 1u << 0,
    /* No ordering guarantee at all; cannot be combined with RELIABLE. */
    NET_PACKET_FLAG_UNSEQUENCED = 1u << 1,
} NetPacketFlags;

ENGINE_API Result net_peer_send(NetHost *host, NetPeerId peer, uint8_t channel, const void *data,
                                 size_t size, uint32_t flags);

/* Forces any packets queued by net_peer_send() out immediately rather
 * than waiting for the next net_host_service() call to send them. */
ENGINE_API void net_host_flush(NetHost *host);

/* ==================================================================== *
 * Events
 * ==================================================================== */

typedef enum NetEventType {
    NET_EVENT_CONNECT = 0,
    NET_EVENT_DISCONNECT,
    NET_EVENT_RECEIVE,
} NetEventType;

typedef struct NetEventData {
    NetEventType type;
    NetPeerId    peer;
    /* NET_EVENT_CONNECT: the user_data the connecting side passed to
     * net_host_connect(). NET_EVENT_DISCONNECT: the user_data passed to
     * net_peer_disconnect()/net_peer_disconnect_now(), or 0 for a
     * timeout. Unused for NET_EVENT_RECEIVE. */
    uint32_t peer_user_data;
    /* NET_EVENT_RECEIVE only; valid only for the duration of the
     * callback - copy it if it must outlive the call. */
    const uint8_t *data;
    size_t         size;
    uint8_t        channel;
} NetEventData;

typedef void (*NetEventFn)(const NetEventData *event, void *userdata);
ENGINE_API void net_host_set_event_callback(NetHost *host, NetEventFn fn, void *userdata);

/* Pumps every pending event (connects, disconnects, receives) that
 * arrives within timeout_ms, firing the event callback for each; 0
 * polls without blocking, the normal choice for a game loop already
 * running at a fixed rate. Call once per frame. */
ENGINE_API void net_host_service(NetHost *host, uint32_t timeout_ms);

#endif /* EISENFRONT_NETWORKING_H */
