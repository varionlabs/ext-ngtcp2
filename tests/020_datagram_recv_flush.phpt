--TEST--
recv with non-QUIC payload does not establish handshake and flush returns array
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
use Varion\Ngtcp2\Datagram;

$remote = new Address('127.0.0.1', 4433);
$conn = new Connection($remote);
$conn->recv(new Datagram('dummy', $remote));

$events = $conn->pollEvents();
var_dump($conn->isEstablished() === false);
var_dump(count($events) === 0);
var_dump(is_array($conn->flush()));
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
