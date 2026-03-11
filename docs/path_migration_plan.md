# Path Migration Plan

This document scopes post-v0 path migration support.

## Scope

- Out of v0 baseline.
- Client-first support only.
- Keep existing API stable for non-migration users.

## Proposed API additions

- `Connection::migrate(Address $newLocalAddress, ?Address $newRemoteAddress = null, bool $immediate = false): void`
- `Connection::isMigrating(): bool`

Optional event exposure (minimal):

- `ConnectionMigrating`
- `ConnectionMigrated`
- `ConnectionMigrationFailed`

## Native integration points

- Build `ngtcp2_path` from current + new local/remote addresses.
- Use:
  - `ngtcp2_conn_initiate_migration(...)`
  - or `ngtcp2_conn_initiate_immediate_migration(...)`
- Ensure callback wiring for path validation result if exposed.
- Keep `recv()/flush()/onTimeout()` flow unchanged during migration.

## sample/client references

`sample_client.c` already contains migration-related knobs:

- `--change-local-addr`
- `--nat-rebinding`

These should be used as behavioral reference when designing userland examples.

## Tests

1. PHPT
- API validation (`migrate` argument checks, invalid state errors).

2. Integration
- dual local UDP sockets to simulate rebinding.
- verify connection stays alive and stream transfer continues after migration.
- failure case: migration to unreachable address should emit failure event (or raise exception if event not added).

## Risks

- Migration timing is sensitive to PTO/loss and can be flaky in CI.
- Event semantics can become noisy unless transitions are coalesced.
- Incorrect path bookkeeping can break close/draining state transitions.
