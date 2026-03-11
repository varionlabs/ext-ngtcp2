--TEST--
pollEvents returns queued stream events in order
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
use Ngtcp2\StreamOpened;
use Ngtcp2\StreamWritable;

$conn = new Connection(new Address('127.0.0.1', 4433));
$conn->openStream();
$events = $conn->pollEvents();

var_dump($events[0] instanceof StreamOpened);
var_dump($events[1] instanceof StreamWritable);
var_dump(count($conn->pollEvents()) === 0);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
