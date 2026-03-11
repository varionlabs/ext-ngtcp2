--TEST--
stream reset updates state and emits StreamReset event
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
use Varion\Ngtcp2\StreamReset;

$conn = new Connection(new Address('127.0.0.1', 4433));
$stream = $conn->openStream();
$conn->pollEvents();

$stream->reset(42);
$events = $conn->pollEvents();
$event = $events[0] ?? null;

var_dump($event instanceof StreamReset);
var_dump($event !== null ? $event->getErrorCode() === 42 : false);
var_dump($event !== null ? $event->isByPeer() === false : false);
var_dump($stream->isClosed());
var_dump($stream->isWritable());
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
