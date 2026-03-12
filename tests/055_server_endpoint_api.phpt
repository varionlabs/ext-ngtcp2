--TEST--
ServerEndpoint exposes basic sans-io multi-connection API surface
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\ServerConfig;
use Varion\Ngtcp2\ServerEndpoint;

$endpoint = new ServerEndpoint(new ServerConfig());

var_dump($endpoint instanceof ServerEndpoint);
var_dump(method_exists($endpoint, 'recv'));
var_dump(method_exists($endpoint, 'handleTimers'));
var_dump(method_exists($endpoint, 'getNextExpiry'));
var_dump(method_exists($endpoint, 'drainAcceptedConnections'));
var_dump(method_exists($endpoint, 'drainOutgoingDatagrams'));
var_dump(method_exists($endpoint, 'drainEvents'));

var_dump($endpoint->getNextExpiry() === null);
var_dump($endpoint->getNextTimeout() === null);
var_dump($endpoint->getTimeoutAt() === null);
var_dump($endpoint->getConnectionCount() === 0);
var_dump($endpoint->drainAcceptedConnections() === []);
var_dump($endpoint->drainOutgoingDatagrams() === []);
var_dump($endpoint->drainEvents() === []);
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
