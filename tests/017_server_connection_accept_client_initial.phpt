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
use Varion\Ngtcp2\ServerConfig;
use Varion\Ngtcp2\ServerConnection;

$client = new Connection(new Address('127.0.0.1', 4433));
$out = $client->drainOutgoingDatagrams();
$initial = $out[0] ?? null;

var_dump($initial !== null);

try {
    ServerConnection::accept($initial, new ServerConfig());
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'ServerConfig.certFile and ServerConfig.keyFile are required'));
}
?>
--EXPECT--
bool(true)
bool(true)
