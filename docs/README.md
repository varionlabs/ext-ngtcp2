# Planning Docs Index

## Architecture Decisions

- `adr/0001-timeout-api-epoch-ms-vs-monotonic.md`
  - Why public timeout deadline APIs use epoch milliseconds

## Server-related

- `server_mode_gap.md`
  - Gap analysis against `sample_server.c`
- `server_mode_mvp_plan.md`
  - File-level checklist for native server mode MVP
- `server_endpoint_plan.md`
  - Development plan for high-level `ServerEndpoint` / `ConnectionRegistry`
- `server_config_plan.md`
  - Expansion plan for `ServerConfig` taxonomy and builder growth

## Deferred feature tracks

- `datagram_extension_plan.md`
  - QUIC DATAGRAM extension plan
- `path_migration_plan.md`
  - Path migration plan
- `post_v0_backlog.md`
  - Key update/debug/nghttp3 and post-MVP hardening breakdown
