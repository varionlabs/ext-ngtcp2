# Server Mode Gap Notes

This document maps `sample_server.c` to the current extension implementation and
lists the minimum tasks required for native server mode.

## Current state

- Extension API is client-only.
- `Connection` initializes native state via `ngtcp2_conn_client_new`.
- Integration currently uses `/usr/sbin/gtlsserver` as an external server fixture.

## sample_server.c mapping

Server-side primitives in `sample_server.c` that are not implemented in extension code yet:

- `ngtcp2_conn_server_new` initialization path
- server transport parameter setup (`stateless_reset_token_present`, token bytes)
- server accept-time CID/path initialization
- server receive loop assumptions (Initial packet handling before connection object exists)
- server close path from server context (`ngtcp2_conn_write_connection_close`)

Already aligned or partially reusable:

- callback patterns (`handshake_completed`, `stream_open`, `stream_close`, `recv_stream_data`)
- stream write path (`ngtcp2_conn_writev_stream`)
- timeout handling (`ngtcp2_conn_get_expiry`, `ngtcp2_conn_handle_expiry`)
- event-queue conversion model (callback -> queue -> `pollEvents()`)

## Minimal native server MVP scope

1. Single connection object created from first Initial packet metadata.
2. Handshake completion event.
3. Single bidi stream RX/TX.
4. Timeout + close handling symmetry with client path.
5. No retry/address-validation policy tuning in MVP.

## Proposed API direction

Option A (new class):

- `Varion\Ngtcp2\ServerConnection`
- static factory for first packet context, e.g. `ServerConnection::accept(Datagram $initial)`

Option B (factory on current class):

- `Connection::acceptServer(Datagram $initial, array $options = []): Connection`

Either option should keep these methods symmetric:

- `recv(Datagram $dgram): void`
- `flush(): array`
- `pollEvents(): array`
- `getNextTimeout(): ?int`
- `onTimeout(): void`
- `close(int $errorCode = 0, string $reason = ''): void`

## Implementation tasks

1. Add server-side native init function (`ngtcp2_conn_server_new` path).
2. Add server transport params setup including stateless reset token.
3. Add initial-packet parse/accept helper to build path + CID.
4. Split client/server callback init if role-specific behavior diverges.
5. Add PHPT for server construction and event progression.
6. Add loopback integration PHPT with in-process UDP sockets (no external gtlsserver).

## Risks

- Accept-time API shape can be hard to evolve without BC impact.
- CID/path handling must be correct before exposing public constructor/factory.
- Memory ownership for accept-time temporary packet buffers must stay explicit.
