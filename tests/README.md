# Integration Test Notes

This extension has two integration PHPT tests:

- `100_integration_handshake.phpt`
- `110_integration_stream_tx.phpt`
- `120_integration_native_server_accept.phpt` (experimental native server path)
- `121_integration_native_server_stream_readable.phpt` (experimental native stream receive path)
- `122_integration_native_server_stream_roundtrip.phpt` (experimental native stream reply path)
- `123_integration_native_server_close.phpt` (experimental native close propagation path)

These tests require:

- `ngtcp2-server` package (`/usr/sbin/gtlsserver`)
- `openssl` binary (`/usr/bin/openssl`)
- UDP bind on `127.0.0.1` in the execution environment

If any prerequisite is unavailable, tests intentionally return `skip`.

## Run examples

Run unit-like PHPT tests:

```sh
NO_INTERACTION=1 REPORT_EXIT_STATUS=1 make test TESTS='tests/001_load_extension.phpt tests/010_connection_ctor.phpt tests/020_datagram_recv_flush.phpt tests/030_stream_read_write_end.phpt tests/031_connection_get_stream.phpt tests/040_event_queue.phpt tests/050_timeout.phpt tests/060_stream_reset.phpt tests/070_close_reason.phpt'
```

Run server API PHPT tests:

```sh
NO_INTERACTION=1 REPORT_EXIT_STATUS=1 make test TESTS='tests/015_server_connection_api.phpt tests/016_server_connection_accept_validation.phpt tests/017_server_connection_accept_client_initial.phpt tests/018_server_connection_accept_with_cert.phpt tests/019_server_connection_accept_options_validation.phpt tests/021_server_close_event.phpt tests/022_server_accept_error_stages.phpt tests/023_server_accept_tls_error_stage.phpt tests/024_server_accept_read_initial_error_stage.phpt'
```

Run integration PHPT tests:

```sh
NO_INTERACTION=1 REPORT_EXIT_STATUS=1 make test TESTS='tests/100_integration_handshake.phpt tests/110_integration_stream_tx.phpt tests/120_integration_native_server_accept.phpt tests/121_integration_native_server_stream_readable.phpt tests/122_integration_native_server_stream_roundtrip.phpt tests/123_integration_native_server_close.phpt'
```

## Server MVP coverage map

- Exit criteria 1 (accept from Initial): `120_integration_native_server_accept.phpt`
- Exit criteria 2 (handshake completion event): `120_integration_native_server_accept.phpt`
- Exit criteria 3 (single bidi RX/TX): `121_integration_native_server_stream_readable.phpt`, `122_integration_native_server_stream_roundtrip.phpt`
- Exit criteria 4 (close/draining observability): `021_server_close_event.phpt`, `123_integration_native_server_close.phpt`
- Exit criteria 5 (UDP-enabled reproducibility): `120`-`123` are runnable in environments where UDP bind is available, otherwise intentionally `skip`
- Error stage mapping (`accept`): `[decode]/[options]` by `022`, `[tls]` by `023`, `[ngtcp2:read_initial]` by `024`
