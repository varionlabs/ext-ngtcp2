--TEST--
ServerConnection class is available as server-mode entry point
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
use Varion\Ngtcp2\ServerConnection;

var_dump(class_exists(ServerConnection::class));

try {
    new ServerConnection();
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'cannot be constructed directly'));
}

try {
    $initial = hex2bin('c00000000108aaaaaaaaaaaaaaaa08bbbbbbbbbbbbbbbb');
    ServerConnection::accept(new Datagram($initial, new Address('127.0.0.1', 4433)));
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'not implemented yet'));
}
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
