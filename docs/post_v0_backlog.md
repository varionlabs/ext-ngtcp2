# Post-v0 Backlog Breakdown

This document breaks down deferred v0 items into implementable tracks.

## Track A: Observability (stats/debug dump)

Goal:

- Provide operational visibility without exposing unstable transport internals by default.

Possible API:

- `Connection::getStats(): array`
- `Connection::debugSnapshot(): array`

Initial fields (safe subset):

- bytes sent/received
- packets sent/received
- handshake status
- close/draining flags
- next timeout (ms)

## Track B: Key update visibility

Goal:

- Expose only app-relevant signal that key update happened.

Possible API/event:

- `KeyUpdated` event (no raw key material)
- include timestamp and direction hint if available

Constraint:

- No direct crypto context exposure.

## Track C: nghttp3 integration prep

Goal:

- Make current transport API easy to embed into future HTTP/3 layer.

Tasks:

- define transport adapter boundary (`read stream` / `write stream` / lifecycle events)
- preserve event ordering guarantees needed by upper layer
- identify required stream metadata for H3 mapping (stream type hints, closure reasons)

## Track D: Server mode hardening after MVP

Goal:

- Move from single-connection MVP to multi-connection readiness.

Tasks:

- connection map keyed by DCID
- stateless reset token handling policy
- optional Retry policy
- preferred address / active migration interoperability checks
