# Server Mode Gap Notes

This document maps `sample_server.c` to the current extension implementation and
lists the minimum tasks required for native server mode.

## Current state

- Stable API is client-centric (`Connection` via `ngtcp2_conn_client_new`).
- Experimental server API exists as `ServerConnection::accept(...)`.
- Native server loopback integration (`tests/120`-`123`) is available.
- `/usr/sbin/gtlsserver` fixture remains available for fallback validation.

## sample_server.c mapping

Server-side primitives in `sample_server.c` that are not fully aligned in extension code yet:

- retry/address-validation policy handling (not in MVP)
- accept-time policy knobs beyond minimal CID/path setup
- multi-connection dispatch from Initial/DCID map (MVP is single connection)

Already aligned or partially reusable:

- `ngtcp2_conn_server_new` initialization path via `ServerConnection::accept(...)`
- server transport params baseline including stateless reset token generation
- accept-time CID/path initialization from first Initial packet metadata
- callback patterns (`handshake_completed`, `stream_open`, `stream_close`, `recv_stream_data`)
- stream write path (`ngtcp2_conn_writev_stream`)
- timeout handling (`ngtcp2_conn_get_expiry`, `ngtcp2_conn_handle_expiry`)
- event-queue conversion model (callback -> queue -> `drainEvents()`)

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
- `drainOutgoingDatagrams(): array`
- `drainEvents(): array`
- `getNextTimeout(): ?int`
- `onTimeout(): void`
- `close(int $errorCode = 0, string $reason = ''): void`

## Remaining implementation tasks

1. multi-connection dispatch keyed by DCID (current MVP is single connection).
2. optional Retry/address-validation policy controls.
3. server-mode capability boundaries and stability guarantees in public docs.
4. CI strategy for UDP-required integration tests.

## Risks

- Accept-time API shape can be hard to evolve without BC impact.
- CID/path handling must be correct before exposing public constructor/factory.
- Memory ownership for accept-time temporary packet buffers must stay explicit.
