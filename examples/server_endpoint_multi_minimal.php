<?php

declare(strict_types=1);

/*
 * Multi-connection Sans-I/O server loop demo.
 *
 * This example shows how to use ServerEndpoint for accept/routing/timer/outgoing
 * aggregation. It intentionally keeps application behavior minimal.
 */

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Datagram;
use Varion\Ngtcp2\ServerConfig;
use Varion\Ngtcp2\ServerEndpoint;

function usage(): void
{
    fwrite(STDERR, <<<TXT
Usage:
  php examples/server_endpoint_multi_minimal.php [--host=127.0.0.1] [--port=4433]
                                                 [--cert=/tmp/ngtcp2/server.crt] [--key=/tmp/ngtcp2/server.key]
                                                 [--alpn=h3]

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

function nowMilliseconds(): int
{
    return (int) floor(microtime(true) * 1000);
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

$options = getopt('', ['host::', 'port::', 'cert::', 'key::', 'alpn::', 'help']);
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

$endpoint = new ServerEndpoint(
    (new ServerConfig())
        ->withCertificate($cert, $key)
        ->withAlpn($alpn)
);

fwrite(STDERR, "server endpoint listening on {$host}:{$port}\n");

$active = [];
$idlePollMs = 100;
while (true) {
    $timeoutAt = $endpoint->getNextExpiry();
    if ($timeoutAt === null) {
        $timeoutMs = $idlePollMs;
    } else {
        $timeoutMs = max(0, $timeoutAt - nowMilliseconds());
    }

    $read = [$udp];
    $write = null;
    $except = null;
    $sec = intdiv($timeoutMs, 1000);
    $usec = ($timeoutMs % 1000) * 1000;
    $n = stream_select($read, $write, $except, $sec, $usec);
    if ($n === false) {
        throw new RuntimeException('stream_select failed');
    }

    if ($n > 0) {
        $packet = stream_socket_recvfrom($udp, 65535, 0, $from);
        if (is_string($packet) && $packet !== '' && is_string($from) && $from !== '') {
            try {
                $localName = stream_socket_get_name($udp, false);
                $local = Address::fromString($localName === false ? "{$host}:{$port}" : $localName);
                $endpoint->recv(new Datagram($packet, Address::fromString($from), $local));
            } catch (Throwable $e) {
                fwrite(STDERR, "endpoint recv warning: {$e->getMessage()}\n");
            }
        }
    } else {
        try {
            $endpoint->handleTimers();
        } catch (Throwable $e) {
            fwrite(STDERR, "endpoint timer warning: {$e->getMessage()}\n");
        }
    }

    foreach ($endpoint->drainAcceptedConnections() as $conn) {
        $active[] = $conn;
        fwrite(STDERR, "accepted connection, active=" . count($active) . PHP_EOL);
    }

    foreach ($endpoint->drainOutgoingDatagrams() as $outgoing) {
        sendDatagram($udp, $outgoing);
    }

    foreach ($active as $i => $conn) {
        foreach ($conn->drainEvents() as $event) {
            fwrite(STDERR, get_class($event) . PHP_EOL);
        }

        if ($conn->isClosed()) {
            unset($active[$i]);
        }
    }
    $active = array_values($active);

    // Keep timer progression independent from receive frequency.
    $dueAt = $endpoint->getNextExpiry();
    if ($dueAt !== null && $dueAt <= nowMilliseconds()) {
        try {
            $endpoint->handleTimers();
        } catch (Throwable $e) {
            fwrite(STDERR, "endpoint timer warning: {$e->getMessage()}\n");
        }
    }
}
