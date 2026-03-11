<?php

declare(strict_types=1);

use Ngtcp2\Address;
use Ngtcp2\Connection;
use Ngtcp2\Datagram;

$remote = new Address('127.0.0.1', 4433);
$connection = new Connection($remote);

$udp = stream_socket_client('udp://127.0.0.1:4433', $errno, $errstr, 1, STREAM_CLIENT_CONNECT);
if ($udp === false) {
    throw new RuntimeException("UDP socket error: {$errno} {$errstr}");
}
stream_set_blocking($udp, false);

while (!$connection->isClosed()) {
    $timeout = $connection->getNextTimeout();
    $read = [$udp];
    $write = null;
    $except = null;

    $sec = $timeout === null ? 1 : intdiv($timeout, 1000);
    $usec = $timeout === null ? 0 : ($timeout % 1000) * 1000;
    $n = stream_select($read, $write, $except, $sec, $usec);

    if ($n === false) {
        throw new RuntimeException('stream_select failed');
    }

    if ($n > 0) {
        $packet = stream_socket_recvfrom($udp, 65535, 0, $peer);
        if ($packet !== false && $packet !== '') {
            $connection->recv(new Datagram($packet, $remote));
        }
    } else {
        $connection->onTimeout();
    }

    foreach ($connection->flush() as $outgoing) {
        $bytes = $outgoing->getPayload();
        stream_socket_sendto($udp, $bytes);
    }

    foreach ($connection->pollEvents() as $event) {
        echo get_class($event), PHP_EOL;
    }
}
