# Integration Test Notes

This extension has two integration PHPT tests:

- `100_integration_handshake.phpt`
- `110_integration_stream_tx.phpt`
- `120_integration_native_server_accept.phpt` (experimental native server path)
- `121_integration_native_server_stream_readable.phpt` (experimental native stream receive path)

These tests require:

- `ngtcp2-server` package (`/usr/sbin/gtlsserver`)
- `openssl` binary (`/usr/bin/openssl`)
- UDP bind on `127.0.0.1` in the execution environment

If any prerequisite is unavailable, tests intentionally return `skip`.

## Run examples

Run unit-like PHPT tests:

```sh
NO_INTERACTION=1 REPORT_EXIT_STATUS=1 make test TESTS='tests/001_load_extension.phpt tests/010_connection_ctor.phpt tests/020_datagram_recv_flush.phpt tests/030_stream_read_write_end.phpt tests/040_event_queue.phpt tests/050_timeout.phpt tests/060_stream_reset.phpt tests/070_close_reason.phpt'
```

Run integration PHPT tests:

```sh
NO_INTERACTION=1 REPORT_EXIT_STATUS=1 make test TESTS='tests/100_integration_handshake.phpt tests/110_integration_stream_tx.phpt tests/120_integration_native_server_accept.phpt tests/121_integration_native_server_stream_readable.phpt'
```
