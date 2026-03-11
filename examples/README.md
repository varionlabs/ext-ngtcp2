# Examples

## Server wrapper (current workaround)

The extension is currently client-only, so native QUIC server mode is not exposed yet.
Use `server_minimal.php` to run `/usr/sbin/gtlsserver` as a local test server.

```sh
php examples/server_minimal.php --host=127.0.0.1 --port=4433
```

Then run the client in another terminal:

```sh
php examples/client_minimal.php
```

For a one-shot run that exits after first response payload:

```sh
php examples/client_once.php --host=127.0.0.1 --port=4433 --path=/
```

Optional flags:

- `--docroot=/path/to/root`
- `--cert=/path/to/server.crt`
- `--key=/path/to/server.key`

If cert/key are missing, the script generates a temporary self-signed certificate via `openssl`.

## Experimental native server entry

`ServerConnection::accept(...)` is now available as an experimental server-mode entry.
You can try it with:

```sh
php examples/server_native_minimal.php --host=127.0.0.1 --port=4433 --alpn=h3
```

This is still an MVP path and not feature-complete server mode.
