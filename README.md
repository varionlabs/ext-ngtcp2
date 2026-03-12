# ext-ngtcp2

A PHP extension that exposes `ngtcp2` as a Sans-I/O QUIC transport layer.

Status summary:

- Stable surface: client-side transport API (`Varion\\Ngtcp2\\Connection` and `Stream`)
- Experimental surface: native server entry (`Varion\\Ngtcp2\\ServerConnection::accept(...)`)
- Crypto backend: GnuTLS

Event model:

- Base class: `Varion\\Ngtcp2\\Event` (abstract)
- Concrete events: `Varion\\Nghttp2\\Events\\*`

## Requirements

- PHP development environment (`phpize`, headers, build tools)
- `libngtcp2`
- `libngtcp2_crypto_gnutls`
- `gnutls`
- `openssl` (used by examples/tests for local cert generation)

## Quick Start

### 1. Build the extension

```sh
phpize
./configure
make -j"$(nproc)"
```

### 2. Verify extension loading

```sh
php -n -d extension=$(pwd)/modules/ngtcp2.so -m | rg '^ngtcp2$'
```

### 3. Run a minimal client example

Start an external QUIC test server in one terminal:

```sh
php -d extension=$(pwd)/modules/ngtcp2.so examples/server_minimal.php --host=127.0.0.1 --port=4433
```

Run a client in another terminal:

```sh
php -d extension=$(pwd)/modules/ngtcp2.so examples/client_minimal.php
```

### 4. Run PHPT tests

```sh
NO_INTERACTION=1 REPORT_EXIT_STATUS=1 make test TESTS='tests/001_load_extension.phpt tests/010_connection_ctor.phpt tests/020_datagram_recv_flush.phpt tests/030_stream_read_write_end.phpt tests/031_connection_get_stream.phpt tests/040_event_queue.phpt tests/041_event_abstract_base.phpt tests/050_timeout.phpt tests/060_stream_reset.phpt tests/070_close_reason.phpt'
```

For integration tests and server-focused test groups, see [`tests/README.md`](tests/README.md).

## API Notes

Core classes:

- `Varion\\Ngtcp2\\Address`
- `Varion\\Ngtcp2\\ClientConfig`
- `Varion\\Ngtcp2\\Datagram`
- `Varion\\Ngtcp2\\Connection`
- `Varion\\Ngtcp2\\ServerConnection` (experimental)
- `Varion\\Ngtcp2\\Stream`
- `Varion\\Ngtcp2\\Event` (abstract base)

Event subclasses are under:

- `Varion\\Nghttp2\\Events\\HandshakeCompleted`
- `Varion\\Nghttp2\\Events\\ConnectionClosed`
- `Varion\\Nghttp2\\Events\\ConnectionDraining`
- `Varion\\Nghttp2\\Events\\StreamOpened`
- `Varion\\Nghttp2\\Events\\StreamReadable`
- `Varion\\Nghttp2\\Events\\StreamWritable`
- `Varion\\Nghttp2\\Events\\StreamClosed`
- `Varion\\Nghttp2\\Events\\StreamReset`

## Additional Docs

- Development plan: [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md)
- Server MVP notes: [`docs/server_mode_mvp_plan.md`](docs/server_mode_mvp_plan.md)
- Server gap notes: [`docs/server_mode_gap.md`](docs/server_mode_gap.md)
- Example usage: [`examples/README.md`](examples/README.md)
- Test execution details: [`tests/README.md`](tests/README.md)

## License

MIT. See [`LICENSE`](LICENSE).
