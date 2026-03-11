--TEST--
ServerConnection::accept accepts client-generated Initial datagram shape
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Connection;
use Varion\Ngtcp2\ServerConnection;

$client = new Connection(new Address('127.0.0.1', 4433));
$out = $client->flush();
$initial = $out[0] ?? null;

var_dump($initial !== null);

try {
    ServerConnection::accept($initial);
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'requires options array'));
}
?>
--EXPECT--
bool(true)
bool(true)
