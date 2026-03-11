<?php

declare(strict_types=1);

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Datagram;
use Varion\Ngtcp2\ServerConnection;
use Varion\Ngtcp2\StreamReadable;

function usage(): void
{
    fwrite(STDERR, <<<TXT
Usage:
  php examples/server_native_echo.php [--host=127.0.0.1] [--port=4433]
                                     [--cert=/tmp/ngtcp2/server.crt] [--key=/tmp/ngtcp2/server.key]
                                     [--alpn=h3] [--prefix='echo: ']

TXT);
}

function ensureCertificate(string $certPath, string $keyPath, string $host): void
{
    if (is_file($certPath) && is_file($keyPath)) {
        return;
    }

    $certDir = dirname($certPath);
    if (!is_dir($certDir) && !mkdir($certDir, 0700, true) && !is_dir($certDir)) {
        throw new RuntimeException("failed to create certificate directory: {$certDir}");
    }

    $keyDir = dirname($keyPath);
    if (!is_dir($keyDir) && !mkdir($keyDir, 0700, true) && !is_dir($keyDir)) {
        throw new RuntimeException("failed to create key directory: {$keyDir}");
    }

    $subject = '/CN=' . $host;
    $command = sprintf(
        '/usr/bin/openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 -subj %s -keyout %s -out %s',
        escapeshellarg($subject),
        escapeshellarg($keyPath),
        escapeshellarg($certPath)
    );
    exec($command . ' 2>/dev/null', $out, $code);
    if ($code !== 0) {
        throw new RuntimeException('openssl certificate generation failed');
    }
}

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

$options = getopt('', ['host::', 'port::', 'cert::', 'key::', 'alpn::', 'prefix::', 'help']);
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
$cert = is_string($options['cert'] ?? null) ? $options['cert'] : '/tmp/ngtcp2/server.crt';
$key = is_string($options['key'] ?? null) ? $options['key'] : '/tmp/ngtcp2/server.key';
$alpn = is_string($options['alpn'] ?? null) ? $options['alpn'] : 'h3';
$prefix = is_string($options['prefix'] ?? null) ? $options['prefix'] : 'echo: ';

if ($port <= 0 || $port > 65535) {
    throw new InvalidArgumentException("invalid --port: {$port}");
}
if (!is_executable('/usr/bin/openssl')) {
    throw new RuntimeException('/usr/bin/openssl is not available');
}

ensureCertificate($cert, $key, $host);

$udp = stream_socket_server(
    "udp://{$host}:{$port}",
    $errno,
    $errstr,
    STREAM_SERVER_BIND
);
if ($udp === false) {
    throw new RuntimeException("failed to bind UDP socket: ({$errno}) {$errstr}");
}
stream_set_blocking($udp, false);

fwrite(STDERR, "native echo server waiting on {$host}:{$port}\n");

$peer = null;
while (true) {
    $packet = stream_socket_recvfrom($udp, 65535, 0, $peer);
    if (is_string($packet) && $packet !== '' && is_string($peer) && $peer !== '') {
        break;
    }
    usleep(10000);
}

$remote = parsePeerAddress($peer);
$localName = stream_socket_get_name($udp, false);
$local = parsePeerAddress($localName === false ? "{$host}:{$port}" : $localName);

$server = ServerConnection::accept(
    new Datagram($packet, $remote, $local),
    $local,
    [
        'certFile' => $cert,
        'keyFile' => $key,
        'alpn' => $alpn,
    ]
);

while (!$server->isClosed()) {
    $packet = stream_socket_recvfrom($udp, 65535, 0, $from);
    if (is_string($packet) && $packet !== '' && is_string($from) && $from !== '') {
        $peer = $from;
        $server->recv(new Datagram($packet, parsePeerAddress($from), $local));
    } else {
        $server->onTimeout();
    }

    foreach ($server->pollEvents() as $event) {
        if ($event instanceof StreamReadable) {
            $stream = $server->getStream($event->getStreamId());
            if ($stream === null) {
                continue;
            }

            $payload = $stream->read(65535);
            if ($payload === '') {
                continue;
            }

            fwrite(STDERR, "stream {$event->getStreamId()} rx: " . trim($payload) . PHP_EOL);
            $stream->write($prefix . $payload);
        }
    }

    if (!is_string($peer) || $peer === '') {
        usleep(10000);
        continue;
    }

    foreach ($server->flush() as $outgoing) {
        stream_socket_sendto($udp, $outgoing->getPayload(), 0, $peer);
    }

    usleep(10000);
}

fclose($udp);
