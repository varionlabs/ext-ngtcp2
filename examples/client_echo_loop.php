<?php

declare(strict_types=1);

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Connection;
use Varion\Ngtcp2\Datagram;
use Varion\Nghttp2\Events\HandshakeCompleted;
use Varion\Nghttp2\Events\StreamReadable;

function parsePeerAddress(string $peer): Address
{
    if (preg_match('/^\[(.+)\]:(\d+)$/', $peer, $m) === 1) {
        return new Address($m[1], (int)$m[2]);
    }

    $pos = strrpos($peer, ':');
    if ($pos === false) {
        throw new RuntimeException("cannot parse peer address: {$peer}");
    }

    return new Address(substr($peer, 0, $pos), (int)substr($peer, $pos + 1));
}

$remote = new Address('127.0.0.1', 4433);
$connection = new Connection($remote);
$stream = null;

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

    if ($n > 0) {
        $packet = stream_socket_recvfrom($udp, 65535, 0, $peer);
        if (is_string($packet) && $packet !== '') {
            $peerAddress = is_string($peer) && $peer !== '' ? parsePeerAddress($peer) : $remote;
            $connection->recv(new Datagram($packet, $peerAddress));
        }
    } else {
        $connection->onTimeout();
    }

    foreach ($connection->drainOutgoingDatagrams() as $outgoing) {
        stream_socket_sendto($udp, $outgoing->getPayload());
    }

    foreach ($connection->drainEvents() as $event) {
        if ($event instanceof HandshakeCompleted && $stream === null) {
            $stream = $connection->openStream();
            $stream->write("ping\n");
            $stream->end();
        }

        if ($event instanceof StreamReadable && $stream !== null) {
            echo $stream->read(), PHP_EOL;
        }
    }
}
