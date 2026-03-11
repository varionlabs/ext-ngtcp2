<?php

declare(strict_types=1);

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Connection;
use Varion\Ngtcp2\Datagram;
use Varion\Nghttp2\Events\HandshakeCompleted;
use Varion\Nghttp2\Events\StreamReadable;

function usage(): void
{
    fwrite(STDERR, <<<TXT
Usage:
  php examples/client_once.php [--host=127.0.0.1] [--port=4433] [--path=/]

TXT);
}

$options = getopt('', ['host::', 'port::', 'path::', 'help']);
if ($options === false) {
    usage();
    exit(2);
}
if (isset($options['help'])) {
    usage();
    exit(0);
}

$host = is_string($options['host'] ?? null) ? $options['host'] : '127.0.0.1';
$port = (int)($options['port'] ?? 4433);
$path = is_string($options['path'] ?? null) ? $options['path'] : '/';
if ($port <= 0 || $port > 65535) {
    throw new InvalidArgumentException("invalid --port: {$port}");
}

$remote = new Address($host, $port);
$connection = new Connection($remote);
$stream = null;
$requestSent = false;
$received = '';

$udp = stream_socket_client("udp://{$host}:{$port}", $errno, $errstr, 1, STREAM_CLIENT_CONNECT);
if ($udp === false) {
    throw new RuntimeException("UDP socket error: {$errno} {$errstr}");
}
stream_set_blocking($udp, false);

$deadline = microtime(true) + 8.0;
while (microtime(true) < $deadline && !$connection->isClosed()) {
    foreach ($connection->flush() as $outgoing) {
        stream_socket_sendto($udp, $outgoing->getPayload());
    }

    $read = [$udp];
    $write = null;
    $except = null;
    $n = stream_select($read, $write, $except, 0, 100000);
    if ($n === false) {
        throw new RuntimeException('stream_select failed');
    }

    if ($n > 0) {
        $packet = stream_socket_recvfrom($udp, 65535, 0, $peer);
        if (is_string($packet) && $packet !== '') {
            $connection->recv(new Datagram($packet, $remote));
        }
    } else {
        $connection->onTimeout();
    }

    foreach ($connection->pollEvents() as $event) {
        if ($event instanceof HandshakeCompleted && !$requestSent) {
            $stream = $connection->openStream();
            $stream->write("GET {$path}\n");
            $requestSent = true;
        }

        if ($event instanceof StreamReadable && $stream !== null) {
            $received .= $stream->read();
            if ($received !== '') {
                break 2;
            }
        }
    }
}

fclose($udp);

if ($received === '') {
    fwrite(STDERR, "no response payload received\n");
    exit(1);
}

echo $received, PHP_EOL;
