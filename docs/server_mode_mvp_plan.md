# Server Mode MVP Plan

This is an implementation-oriented checklist for introducing native server mode
on top of the current stable client baseline.

## Goal

Enable one accepted QUIC server connection with:

- `recv/drainOutgoingDatagrams/drainEvents/onTimeout/getNextTimeout/close`
- single bidi stream RX/TX
- existing event-queue model

## API decision (proposed)

Use a dedicated class to avoid constructor ambiguity:

- `Varion\Ngtcp2\ServerConnection`
- `ServerConnection::accept(Datagram $initial, ?Address $localAddress = null, ?array $options = null): ServerConnection`

Why:

- Keeps client constructor unchanged.
- Encodes "first packet is required" at API boundary.
- Allows role-specific arginfo without mode flags.

## File-level tasks

1. `src/internal/types.h`
- Add role flag (`is_server`) and optional fields for server-specific options.

2. `src/internal/connection.h` / `src/connection.c`
- Split native init into client/server variants.
- Implement `php_quic_connection_init_native_server(...)` using `ngtcp2_conn_server_new`.
- Reuse existing stream/event/timeout/drainOutgoingDatagrams machinery where possible.

3. `src/internal/callbacks.h` / `src/callbacks.c`
- Keep common callback registration.
- If needed, add a role-aware callback init (`php_quic_callbacks_init_server`).

4. `ngtcp2.c`
- Register `ServerConnection` class during MINIT.

5. `src/server_connection.c` (new)
- Implement class entry, constructor prohibition, static `accept(...)`, and method forwarding to shared connection internals.

6. `tests/`
- Add PHPT:
  - `120_server_accept_ctor.phpt`
  - `130_server_timeout_and_close.phpt`
  - `140_server_event_queue.phpt`

7. `tests/` integration
- Add loopback integration with two PHP UDP sockets:
  - client side uses existing `Connection`
  - server side uses new `ServerConnection::accept(...)`

## sample_server.c references

Use these areas as implementation anchors:

- `ngtcp2_conn_server_new` path and params setup
- `params.stateless_reset_token_present` + token generation
- handshake/stream callbacks (`handshake_completed_cb`, `stream_open_cb`, `stream_close_cb`, `recv_stream_data_cb`)
- timeout progression (`ngtcp2_conn_get_expiry`, `ngtcp2_conn_handle_expiry`)
- close packet emission (`ngtcp2_conn_write_connection_close`)

## Exit criteria

1. Server accept from Initial packet succeeds without crash.
2. Handshake completion event is emitted once.
3. Server can read request bytes and send response bytes on one bidi stream.
4. Close/draining transition is observable from events and state methods.
5. New PHPT + integration tests are reproducible in a UDP-enabled environment.

## Status (2026-03-11)

Implemented:

- `ServerConnection::accept(...)` with `ngtcp2_conn_server_new` path
- handshake event progression and stream RX/TX path
- server close propagation integration (`tests/123`)
- `accept` stage-based error prefixes (`[decode]`, `[options]`, `[tls]`, `[ngtcp2:new]`, `[ngtcp2:read_initial]`)
- deterministic test hook for `[ngtcp2:new]` (`NGTCP2_TEST_FORCE_SERVER_NEW_FAILURE=1`, `tests/025`)

Remaining:

- Keep integration execution guidance in sync for UDP-restricted runners
- Transition from MVP to hardening scope (multi-connection/DCID map/Retry policy)
