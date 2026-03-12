# ServerEndpoint / ConnectionRegistry Development Plan

Last updated: 2026-03-12

## Context

Current server-side surface is centered on:

- `ServerConnection::accept(Datagram $initial, ServerConfig $config): ServerConnection`
- Per-connection operations (`recv`, `drainOutgoingDatagrams`, `drainEvents`, timeout APIs)

This is a correct low-level API. For production server loops, a higher-level endpoint layer is needed to own:

1. Intake of unassigned Initial datagrams
2. CID-based routing to existing connections
3. Accept path for new connections
4. Aggregation of outgoing datagrams across all active connections

This document defines an implementation-ready plan for introducing `ServerEndpoint` and `ConnectionRegistry` without breaking existing APIs.

## 1. Goals and Non-Goals

### Goals

- Keep current low-level APIs intact and supported:
  - `Connection`
  - `ServerConnection::accept(...)`
  - `ServerConfig`
- Add a higher-level server API that encapsulates routing + accept + fan-out send.
- Keep timer semantics aligned with existing APIs (`?int`, milliseconds, null behavior).
- Enable incremental rollout (MVP first, then observability and advanced transport features).

### Non-Goals (initial rollout)

- Full Retry/token issuance policy
- qlog emission and full tracing surface
- Multi-process/shared-memory connection registry
- Full production-grade anti-abuse controls (rate limiting, amplification control policies)
- Kernel bypass/advanced send batching optimization

## 2. Proposed PHP API

### New class: `Varion\Ngtcp2\ServerEndpoint`

```php
final class ServerEndpoint
{
    public function __construct(Address $local, ServerConfig $config) {}

    public function recv(Datagram $dgram): void {}

    /** @return array<ServerConnection> */
    public function drainAcceptedConnections(): array {}

    /** @return array<Datagram> */
    public function drainOutgoingDatagrams(): array {}

    public function getNextTimeout(): ?int {}
    public function getNextExpiry(): ?int {}
    public function getTimeoutAt(): ?int {}
    public function onTimeout(): void {}
    public function handleTimers(): void {}
    public function tick(): void {}

    /** Optional, phase-scoped */
    public function getConnectionCount(): int {}
}
```

### Behavior notes

- `recv()` accepts any datagram observed on the bound socket.
- `drainAcceptedConnections()` exposes newly accepted `ServerConnection` objects in FIFO order.
- `drainOutgoingDatagrams()` aggregates datagrams from all managed connections (fair-drain strategy in Phase 1).
- Timeout methods represent endpoint-level next action across managed connections:
  - `null`: no active timers
  - `<= now`: endpoint should process timers immediately

### Low-level API retention policy

- `ServerConnection::accept()` remains available and documented as a low-level primitive.
- `ServerEndpoint` is additive and does not replace/remove low-level entry points.

## 3. ConnectionRegistry Design

### Responsibilities

- Maintain active `ServerConnection` instances.
- Map packet DCID -> owning connection.
- Track accepted-connection queue and close cleanup.

### Core data structures (conceptual)

- `connectionsById: array<string, ServerConnection>`  
  Internal stable ID for bookkeeping.
- `cidToConnectionId: array<string, string>`  
  Keyed by CID bytes (binary-safe string).
- `acceptedQueue: SplQueue<ServerConnection>`
- `pendingCloseIds: array<string, true>`  
  Cleanup set applied after recv/timer/drain loops.

### CID key strategy

- Primary routing key: packet destination CID (`DCID`) from incoming datagram.
- Canonical key encoding:
  - raw CID bytes -> hex string (lowercase) for stable HashTable keys.
- Mapping lifecycle:
  - On accept: register Initial DCID + server SCID.
  - On CID update events (future callback integration): add new CID mappings, expire old mappings.
  - On close/draining terminal state: remove all CIDs linked to the connection.

### Unknown CID policy

- Phase 1 default:
  - If packet looks like valid Initial and no route exists -> attempt accept.
  - Otherwise silently drop.
- Observability (Phase 2):
  - increment metrics counter (`unknown_cid_drops`)
  - optional debug event/log hook

### Close cleanup strategy

- Cleanup triggers:
  - `ServerConnection::isClosed()` true
  - terminal close event observed in drained events
- Cleanup action:
  - remove from `connectionsById`
  - remove all linked CID mappings
  - remove from pending round-robin cursor if used

## 4. Packet Processing Flow

### `ServerEndpoint::recv($dgram)` decision order

1. Parse QUIC header enough to extract destination CID and packet type.
2. If `DCID` matches existing registry entry:
  - route to that `ServerConnection->recv($dgram)`.
3. Else if packet is Initial:
  - call `ServerConnection::accept($dgram, $configWithLocal)` and register connection + CIDs.
  - enqueue accepted connection into `acceptedQueue`.
4. Else:
  - drop (Phase 1), optionally account in metrics (Phase 2).

### Outgoing aggregation (`drainOutgoingDatagrams`)

- Iterate active connections and call `drainOutgoingDatagrams()`.
- Aggregate into endpoint return array.
- Fairness approach for MVP:
  - simple round-robin start index per call (avoid always favoring oldest connection).
- Hard cap per drain call (configurable later) to avoid unbounded memory spikes.

### Timer integration

- Endpoint timeout is min(timeout of each active connection).
- Endpoint `handleTimers()/onTimeout()/tick()`:
  - invoke timer handling on all connections whose timeout is due (`<= 0` relative or `<= now` absolute).
  - apply cleanup after timer sweep.

### Timeout representation note

- Public deadline APIs are kept as epoch milliseconds (`?int`) for PHP userland ergonomics and easy multi-connection deadline merge.
- Internal transport timing can remain monotonic.
- See ADR: `docs/adr/0001-timeout-api-epoch-ms-vs-monotonic.md`.

## 5. Error / Exception Policy

### Error taxonomy (design baseline)

1. Misuse / API contract violations
- Examples: invalid constructor args, invalid object state, wrong call order assumptions.
- Handling: throw exception immediately.

2. TLS/configuration failures
- Examples: invalid cert/key paths, TLS setup failure during accept bootstrap.
- Handling: throw exception (operator action required).

3. Protocol/transport close outcomes
- Examples: peer protocol violation, transport close, draining/closing transitions.
- Handling: do not model as hot-path exception in endpoint recv loop.
- Surface via connection state/events and generated close datagrams.

4. Ignorable network input
- Examples: malformed/unroutable datagram, unknown CID non-Initial, parse-failed non-critical packet.
- Handling: drop without exception (optionally count/log).

5. Internal invariants/bugs
- Examples: CID mapped to missing connection, impossible registry ownership state.
- Handling: throw exception (implementation defect).

### `recv()` contract (important)

- `ServerEndpoint::recv()` should be resilient for network-originated bad input.
- Principle:
  - network weirdness -> state transition / drop / metric (non-throw)
  - programmer misuse or invariant break -> throw
- Operational goal: avoid exception-per-packet behavior under hostile/noisy traffic.
- Current status note:
  - Existing server examples still wrap `recv()` in broad `try/catch`.
  - This indicates the current low-level behavior can still surface network-originated exceptions.
  - As a design task, exception granularity for `recv()` remains an explicit review item.

### Connection close behavior

- When packet handling implies close/draining:
  - update connection state (`isClosed`/events)
  - close frames are emitted through `drainOutgoingDatagrams()`
- Endpoint caller should not be forced to catch on every malformed packet.

### Observability policy

- Phase 1:
  - counters only are sufficient (no mandatory new event classes).
- Phase 2:
  - add endpoint-level observability surface:
    - counters (`accepted_total`, `unknown_cid_drops`, `route_hits`, `route_misses`, `recv_drop_malformed`)
    - optional lightweight `drainEndpointEvents(): array` for operational events.
  - event taxonomy review:
    - keep public events user-meaningful (not internal implementation-only notifications)
    - document criteria for new event types before adding them

### Public event boundary (important)

`drainEvents()` should expose application-meaningful lifecycle milestones, not transport internals.

Design rule:

- Public events:
  - represent decisions or milestones that userland can act on
  - remain stable even if internal transport mechanics evolve
- Internal events:
  - may exist for implementation/diagnostics
  - should not be surfaced directly as public API types by default

Avoid exposing internal-only transport signals such as:

- `PacketNumberSpaceUpdated`
- `AckElicitedPacketSent`
- `LossTimerArmed`

Prefer user-meaningful events such as:

- `HandshakeCompleted`
- `ConnectionReady` (future candidate)
- `ConnectionClosed`
- `PeerAddressChanged` (future candidate)
- `StreamOpened`
- `StreamReadable`
- `StreamWritable`
- `StreamClosed`

### Event class staging table

Stable (existing / suitable public API):

- `HandshakeCompleted`
- `ConnectionClosed`
- `ConnectionDraining`
- `StreamOpened`
- `StreamReadable`
- `StreamWritable`
- `StreamClosed`
- `StreamReset`

Deferred (candidate public events, design review required):

- `ConnectionReady`
- `PeerAddressChanged`
- `DatagramReceived`
- `TlsAlertReceived`

Internal-only (default):

- packet-number-space transitions
- loss-timer arm/disarm notifications
- ack-eliciting packet accounting

## 6. Staged Release Plan

### Phase 1 (MVP)

- Single-socket endpoint wrapper with:
  - recv routing
  - new accept
  - accepted queue
  - outgoing aggregation
  - endpoint timeout aggregation APIs
- No Retry/token issuance.
- Unknown CID handling is drop-by-default.

Exit criteria:

- Multiple concurrent connections can be accepted and progressed through one endpoint object.
- Existing `ServerConnection::accept` users unaffected.

### Phase 2 (observability + hardening)

- Endpoint counters and optional endpoint event queue.
- Better diagnostics for drop reasons.
- Optional config knobs:
  - max active connections
  - per-drain output cap
  - unknown packet logging level

### Phase 3 (advanced transport features)

- Retry/token issuance policy
- qlog export hooks
- CID migration and key update observability hardening

## 7. Test Plan

### Unit-like PHPT (new)

- `tests/080_server_endpoint_registry_routing.phpt`
  - route by CID to existing connection
- `tests/081_server_endpoint_accepted_queue.phpt`
  - accept queue FIFO and drain semantics
- `tests/082_server_endpoint_close_cleanup.phpt`
  - close removes registry mappings
- `tests/083_server_endpoint_timeout_propagation.phpt`
  - endpoint timeout = min(connection timeouts), null behavior, aliases
- `tests/084_server_endpoint_recv_error_policy.phpt`
  - malformed/unknown network datagrams do not throw
  - misuse/config errors still throw

### Integration PHPT (new)

- `tests/130_integration_endpoint_multi_client_accept.phpt`
  - two+ clients concurrently via one endpoint/socket
- `tests/131_integration_endpoint_wrong_cid_drop.phpt`
  - mismatched CID packet is dropped and does not break active connections
- `tests/132_integration_endpoint_reaccept_after_close.phpt`
  - connection close followed by successful new accept
- `tests/133_integration_endpoint_noisy_input_resilience.phpt`
  - noisy malformed traffic does not cause exception storm in main loop

### Compatibility checks

- Re-run existing server tests (`015`-`025`, `120`-`123`).
- Re-run timeout alias/deadline tests (`050`-`052`).
- Verify existing examples that call `ServerConnection::accept` remain valid.

## 8. Compatibility Policy

- No breaking changes in existing classes/method signatures.
- `ServerEndpoint` is additive only.
- `ServerConnection::accept` remains documented as low-level primitive.
- Migration guide (draft):
  1. Existing code can stay as-is.
  2. New deployments can move socket loop responsibility to `ServerEndpoint`.
  3. For custom accept policies, keep using low-level path directly.

## 9. Risks and Open Questions

### Risks

- CID migration/update handling requires robust mapping updates from ngtcp2 callbacks.
- Memory pressure under many simultaneous connections and queued datagrams/events.
- Endpoint-level event queue growth if consumer drains too slowly.

### Open questions

- Should endpoint expose dropped-packet counters in Phase 1 or Phase 2 only?
- Should `drainOutgoingDatagrams()` expose source connection metadata for debugging?
- What default caps should be applied (active connections, per-drain datagrams)?
- Should endpoint provide explicit `closeAll()`/`shutdown()` API in MVP?
- How should low-level `ServerConnection::recv()` classify ngtcp2 errors into:
  - throw (misuse/config/internal bug)
  - non-throw state transition (protocol/transport close)
  - silent drop / metric (ignorable network input)?
- Event surface design scope:
  - Which event classes are stable and user-actionable enough for long-term API (`HandshakeCompleted`, `ConnectionClosed`, `StreamReadable`, etc.)?
  - Should candidates like `DatagramReceived` / `TlsAlertReceived` be first-class events or represented via state/metrics?
  - How to prevent drift into internal-only event leakage while keeping observability practical?

## File-Level Implementation Outline (for future coding phase)

Planned new/updated areas:

- New: `src/server_endpoint.c`
- New: `src/internal/server_endpoint.h`
- New: endpoint/registry structures in `src/internal/types.h` (or dedicated registry header)
- Update: `ngtcp2.c` (class registration)
- Update: docs + tests index files

No code changes are part of this document; this section defines expected touch points for implementation.

## First 3 Tasks at Implementation Start

- Implement `ServerEndpoint` class skeleton + MINIT registration + arginfo stubs.
- Implement `ConnectionRegistry` core maps (CID routing, accepted queue, close cleanup) and wire `recv()` decision flow.
- Implement endpoint-level `drainOutgoingDatagrams()` + timeout aggregation (`getNextTimeout/getTimeoutAt/handleTimers`) with PHPT coverage.
