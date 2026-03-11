--TEST--
Connection::getStream returns existing stream and null for unknown stream id
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
use Varion\Ngtcp2\Stream;

$conn = new Connection(new Address('127.0.0.1', 4433));
$opened = $conn->openStream();
$found = $conn->getStream($opened->getId());

var_dump($found instanceof Stream);
var_dump($found->getId() === $opened->getId());
var_dump($conn->getStream(9999) === null);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
