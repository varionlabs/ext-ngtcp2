<?php

declare(strict_types=1);

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Datagram;
use Varion\Ngtcp2\ServerConfig;
use Varion\Ngtcp2\ServerConnection;
use Varion\Nghttp2\Events\StreamReadable;

function usage(): void
{
    fwrite(STDERR, <<<TXT
Usage:
  php examples/server_echo.php [--host=127.0.0.1] [--port=4433]
                              [--cert=/tmp/ngtcp2/server.crt] [--key=/tmp/ngtcp2/server.key]
                              [--alpn=h3] [--prefix='echo: ']

TXT);
}

function ensureCertificate(string $certPath, string $keyPath, string $host): void
{
    // Demo helper: certificate bootstrap is not part of the ngtcp2 API surface.
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

function sendDatagram($udp, Datagram $datagram): void
{
    $sent = stream_socket_sendto(
        $udp,
        $datagram->getPayload(),
        0,
        (string)$datagram->getPeerAddress()
    );
    if ($sent === false) {
        fwrite(
            STDERR,
            "send warning: failed to send datagram to " . (string)$datagram->getPeerAddress() . PHP_EOL
        );
    }
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

while (true) {
    $packet = stream_socket_recvfrom($udp, 65535, 0, $from);
    if (is_string($packet) && $packet !== '' && is_string($from) && $from !== '') {
        break;
    }
    usleep(10000);
}

$remote = Address::fromString($from);
$localName = stream_socket_get_name($udp, false);
$local = Address::fromString($localName === false ? "{$host}:{$port}" : $localName);

$server = ServerConnection::accept(
    new Datagram($packet, $remote, $local),
    (new ServerConfig())
        ->withCertificate($cert, $key)
        ->withAlpn($alpn)
);

while (!$server->isClosed()) {
    $packet = stream_socket_recvfrom($udp, 65535, 0, $from);
    if (is_string($packet) && $packet !== '' && is_string($from) && $from !== '') {
        try {
            $server->recv(new Datagram($packet, Address::fromString($from), $local));
        } catch (Throwable $e) {
            fwrite(STDERR, "recv warning: {$e->getMessage()}\n");
        }
    } else {
        try {
            $server->handleTimers();
        } catch (Throwable $e) {
            fwrite(STDERR, "timeout warning: {$e->getMessage()}\n");
        }
    }

    foreach ($server->drainEvents() as $event) {
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

    try {
        foreach ($server->drainOutgoingDatagrams() as $outgoing) {
            sendDatagram($udp, $outgoing);
        }
    } catch (Throwable $e) {
        fwrite(STDERR, "drainOutgoingDatagrams warning: {$e->getMessage()}\n");
    }

    usleep(10000);
}

fclose($udp);
