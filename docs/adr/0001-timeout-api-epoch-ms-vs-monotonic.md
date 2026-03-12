# ADR 0001: Timeout API Uses Epoch Milliseconds for Public Deadline

- Status: Accepted
- Date: 2026-03-12
- Related: `Connection::getNextExpiry()`, `Connection::getTimeoutAt()`, `Connection::getNextTimeout()`

## Context

The extension needs timeout APIs that integrate cleanly with event loops and future multi-connection server orchestration.

Candidate public representations:

1. Absolute monotonic deadline (e.g., nanoseconds)
2. Absolute UNIX epoch deadline (milliseconds)
3. Relative timeout only (milliseconds)

Current implementation internally relies on monotonic time for transport progress (`zend_hrtime()`/ngtcp2 timestamp path), while userland APIs are consumed in PHP event loops (`stream_select`, socket polling, merged deadlines across connections).

## Decision

Public API uses absolute UNIX epoch milliseconds (`?int`) for deadline APIs:

- `getNextExpiry(): ?int`
- `getTimeoutAt(): ?int` (alias)

And keeps relative timeout API for convenience:

- `getNextTimeout(): ?int` (milliseconds)

Semantics:

- `null`: no scheduled timer
- `<= now`: timer handling should run immediately

## Rationale

- PHP userland ergonomics: epoch milliseconds are easy to compare with `microtime(true)`-derived values.
- Multi-connection merge is straightforward with absolute deadlines (`min(expiry)`).
- Type simplicity: integer milliseconds avoids float precision issues.
- Backward compatibility: keeps existing relative-time API while adding an absolute deadline API.

## Consequences

Positive:

- Event-loop integration remains simple for single and multi-connection loops.
- Existing code using `getNextTimeout()` remains valid.

Trade-off:

- Public clock base is not monotonic; callers should compare against current wall clock consistently.
- Internal monotonic timing remains implementation detail.

## Follow-ups

- Consider optional future API for monotonic deadline exposure if needed for advanced orchestrators, without changing existing method behavior.
