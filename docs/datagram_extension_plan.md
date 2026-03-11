# QUIC DATAGRAM Extension Plan

This document scopes post-v0 support for QUIC DATAGRAM extension in the PHP extension.

## Scope

- Out of v0 baseline.
- Client-first implementation.
- Keep existing stream API unchanged.

## Proposed API additions

- `Connection::sendDatagram(string $payload): int`
- `Connection::pollDatagrams(): array` (returns `string[]` or `DatagramEvent[]`)

Design rule:

- Reuse event-queue model where possible.
- Avoid exposing transport internals (ack/loss details).

## Native integration points

- Enable DATAGRAM transport parameter during connection init.
- Register DATAGRAM receive callback in ngtcp2 callbacks.
- Add per-connection RX datagram queue.
- Add send path wiring in `flush()` progression.

## Tests

1. PHPT
- API signature and return values
- empty/non-empty datagram roundtrip semantics

2. Integration
- loopback client/server datagram echo
- mixed traffic (stream + datagram concurrently)

## Risks

- MTU/fragmentation assumptions can cause flaky tests.
- API shape should not force allocation-heavy copies in hot path.
