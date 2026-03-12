--TEST--
ServerConnection::accept validates initial datagram format
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Datagram;
use Varion\Ngtcp2\ServerConfig;
use Varion\Ngtcp2\ServerConnection;

try {
    ServerConnection::accept(
        new Datagram('x', new Address('127.0.0.1', 4433)),
        (new ServerConfig())->withCertificate('/tmp/nope.crt', '/tmp/nope.key')
    );
    echo "no-exception\n";
} catch (Throwable $e) {
    $msg = $e->getMessage();
    var_dump(
        str_contains($msg, 'failed to decode initial datagram') ||
        str_contains($msg, 'unsupported QUIC version in initial datagram')
    );
}
?>
--EXPECT--
bool(true)
