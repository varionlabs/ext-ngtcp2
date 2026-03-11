--TEST--
recv transitions handshake state and flush returns array
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
use Ngtcp2\Datagram;
use Ngtcp2\HandshakeCompleted;

$remote = new Address('127.0.0.1', 4433);
$conn = new Connection($remote);
$conn->recv(new Datagram('dummy', $remote));

$events = $conn->pollEvents();
var_dump($conn->isEstablished());
var_dump(count($events) >= 1);
var_dump($events[0] instanceof HandshakeCompleted);
var_dump(is_array($conn->flush()));
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
