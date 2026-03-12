--TEST--
Connection::openUniStream opens local unidirectional streams with expected IDs
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
use Varion\Nghttp2\Events\StreamOpened;
use Varion\Nghttp2\Events\StreamWritable;

$conn = new Connection(new Address('127.0.0.1', 4433));
var_dump(method_exists($conn, 'openUniStream'));

$s1 = $conn->openUniStream();
$s2 = $conn->openUniStream();
$s3 = $conn->openUniStream();

var_dump($s1 instanceof Stream);
var_dump($s1->getId() === 2);
var_dump($s2->getId() === 6);
var_dump($s3->getId() === 10);

$found = $conn->getStream($s2->getId());
var_dump($found instanceof Stream);
var_dump($found->getId() === $s2->getId());

var_dump($s1->write('abc') === 3);
$s1->end("!");
var_dump($s1->isWritable() === false);
$out = $conn->flush();
var_dump(is_array($out));

$events = $conn->pollEvents();
var_dump($events[0] instanceof StreamOpened);
var_dump($events[0]->isByPeer() === false);
var_dump($events[1] instanceof StreamWritable);
var_dump($events[1]->isByPeer() === false);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
