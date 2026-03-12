# ServerConfig Expansion Plan

Last updated: 2026-03-12

## Goal

Keep `ServerConfig` as a single user-facing builder while scaling configuration safely as server features grow.

Current API (`withCertificate`, `withAlpn`, etc.) is a good base. This plan defines classification and extension rules so future `withXxx()` additions remain coherent.

## Scope

- In scope:
  - configuration taxonomy
  - future `withXxx()` surface design rules
  - units/defaults/application timing policy
- Out of scope:
  - immediate implementation of all candidate options
  - retry/token/qlog runtime behavior details

## Configuration Taxonomy

### 1) TLS settings

Examples:

- certificate path
- private key path
- ALPN
- verify mode (future, if server-side verification modes are exposed)
- key log output (future)

Application point:

- Accept/bootstrap phase (`ServerConnection::accept(...)` path)

### 2) QUIC transport settings

Examples:

- idle timeout (ms)
- max data
- max streams bidi/uni
- ack delay exponent
- active connection ID limit

Application point:

- ngtcp2 transport params during connection creation

### 3) Debug / observability settings

Examples:

- qlog output
- TLS secrets log
- debug callbacks / verbose tracing switches

Application point:

- setup phase + optional runtime hooks

## API Shape Policy

User-facing API stays one class:

```php
$config = (new ServerConfig())
    ->withCertificate($cert, $key)
    ->withAlpn('h3')
    ->withIdleTimeout(30_000)
    ->withMaxStreamsBidi(100);
```

Rules:

- fluent builder (`withXxx()` returns `ServerConfig`)
- explicit units in method names/docs where ambiguity exists
  - e.g. `withIdleTimeoutMs(int $ms)` or clearly documented `withIdleTimeout(int $ms)`
- strict argument validation (`InvalidArgumentException` / `ValueError` semantics aligned with existing style)
- no silent coercion for invalid ranges

## Candidate Additions (Planned, Not Implemented)

### Phase A (high-value transport knobs)

- `withIdleTimeout(int $milliseconds): ServerConfig`
- `withMaxData(int $bytes): ServerConfig`
- `withMaxStreamsBidi(int $count): ServerConfig`
- `withMaxStreamsUni(int $count): ServerConfig`
- `withActiveConnectionIdLimit(int $count): ServerConfig`

### Phase B (advanced transport)

- `withAckDelayExponent(int $value): ServerConfig`
- `withInitialMaxStreamDataBidiLocal(int $bytes): ServerConfig`
- `withInitialMaxStreamDataBidiRemote(int $bytes): ServerConfig`
- `withInitialMaxStreamDataUni(int $bytes): ServerConfig`

### Phase C (observability/debug)

- `withQlogPath(string $path): ServerConfig`
- `withSecretsLogPath(string $path): ServerConfig`
- `withDebugCallbacks(bool $enabled): ServerConfig`

## Internal Structuring Guidance

Even with one public class, implementation should keep grouped fields internally:

- `tls.*`
- `transport.*`
- `observability.*`

This reduces drift and makes mapping to ngtcp2/GnuTLS setup code auditable.

## Validation and Defaults Policy

- Every new option must define:
  - type
  - valid range
  - default behavior when unset
  - when it is applied (accept only / runtime)
- Defaults should remain conservative and backward-compatible.
- Existing behavior must not change for users who only set current fields.

## Compatibility Policy

- Additive only:
  - new `withXxx()`/`getXxx()` methods may be added
  - no breaking changes to existing `ServerConfig` methods/signatures
- Existing code using only certificate/alpn/local address must behave identically.

## Documentation Checklist for Each New Option

- README API note update
- `tests/053_server_config_api.phpt` extension or dedicated PHPT
- server integration test coverage if behavior affects runtime negotiation/flow control
- mention in server planning docs when relevant (`server_endpoint_plan.md`)

## First Implementation Tasks (when coding starts)

- Add transport subgroup fields in internal server config storage and wire to accept path.
- Implement Phase A methods with strict validation and PHPT coverage.
- Add concise user docs table (`method`, `unit`, `default`, `applied at`).
