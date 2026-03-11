--TEST--
timeout API basic behavior
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
var_dump(is_int($conn->getNextTimeout()) || $conn->getNextTimeout() === null);
$conn->close();
var_dump($conn->getNextTimeout() === null);
?>
--EXPECT--
bool(true)
bool(true)
