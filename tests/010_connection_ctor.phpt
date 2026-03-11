--TEST--
Connection can be instantiated
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Ngtcp2\Address;
use Ngtcp2\Connection;

$conn = new Connection(new Address('127.0.0.1', 4433));
var_dump($conn instanceof Connection);
var_dump($conn->isEstablished());
var_dump($conn->isClosed());
?>
--EXPECT--
bool(true)
bool(false)
bool(false)
