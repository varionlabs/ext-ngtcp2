<?php

declare(strict_types=1);

/*
 * Multi-connection Sans-I/O server loop demo.
 *
 * This example shows how to use ServerEndpoint for accept/routing/timer/outgoing
 * aggregation. It intentionally keeps application behavior minimal.
 *
 * Timing note:
 * This example uses wall-clock milliseconds because deadline APIs are exposed as epoch ms.
 * For production timer robustness, prefer monotonic scheduling where available.
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
    // Demo helper: use wall-clock epoch milliseconds for API consistency.
    // In production, monotonic-clock-based scheduling is preferable, but this sample
    // keeps wall time due to current PHP/userland integration constraints.
    return (int) floor(microtime(true) * 1000);
}

function sendDatagram($udp, Datagram $datagram): void
{
    // This sample binds one UDP socket, so destination is selected by peer only.
    // `Datagram::getLocalAddress()` is intentionally unused here.
    $payload = $datagram->getPayload();
    $length = strlen($payload);
    $sent = stream_socket_sendto(
        $udp,
        $payload,
        0,
        (string)$datagram->getPeerAddress()
    );
    if ($sent === false) {
        fwrite(
            STDERR,
            "send warning: failed to send datagram to " . (string)$datagram->getPeerAddress() . PHP_EOL
        );
        return;
    }

    if ($sent !== $length) {
        fwrite(
            STDERR,
            "send warning: short write {$sent}/{$length} to " . (string)$datagram->getPeerAddress() . PHP_EOL
        );
    }
}

/**
 * Read one datagram from a non-blocking UDP stream.
 *
 * PHP streams may emit warnings for expected "would block" races even after
 * stream_select() reports readability. Handle that locally and return false.
 */
function recvDatagramNonBlocking($udp, ?string &$from): string|false
{
    set_error_handler(
        static function (int $severity): bool {
            return $severity === E_WARNING;
        }
    );

    try {
        return stream_socket_recvfrom($udp, 65535, 0, $from);
    } finally {
        restore_error_handler();
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
// Single-socket demo: local address is constant for all received datagrams.
$localName = stream_socket_get_name($udp, false);
$local = Address::fromString($localName === false ? "{$host}:{$port}" : $localName);

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
        // Convert absolute expiry (epoch ms) to relative wait for stream_select().
        // This demo uses wall time; a monotonic clock source is recommended in real loops.
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
        // Drain all pending datagrams when the socket becomes readable.
        while (true) {
            $packet = recvDatagramNonBlocking($udp, $from);
            if ($packet === false) {
                break;
            }

            if (is_string($packet) && $packet !== '' && is_string($from) && $from !== '') {
                try {
                    $endpoint->recv(new Datagram($packet, Address::fromString($from), $local));
                } catch (LogicException|Error $e) {
                    throw $e;
                } catch (Throwable $e) {
                    fwrite(STDERR, "endpoint recv warning: {$e->getMessage()}\n");
                }
            }
        }
    } else {
        try {
            $endpoint->handleTimers();
        } catch (LogicException|Error $e) {
            throw $e;
        } catch (Throwable $e) {
            fwrite(STDERR, "endpoint timer warning: {$e->getMessage()}\n");
        }
    }

    foreach ($endpoint->drainAcceptedConnections() as $conn) {
        $id = spl_object_id($conn);
        $active[$id] = $conn;
        fwrite(STDERR, "accepted connection id={$id} active=" . count($active) . PHP_EOL);
    }

    foreach ($endpoint->drainOutgoingDatagrams() as $outgoing) {
        sendDatagram($udp, $outgoing);
    }

    foreach ($endpoint->drainEvents() as $event) {
        fwrite(STDERR, "endpoint event=" . get_class($event) . PHP_EOL);
    }

    foreach ($active as $id => $conn) {
        foreach ($conn->drainEvents() as $event) {
            fwrite(STDERR, "conn={$id} event=" . get_class($event) . PHP_EOL);
        }

        if ($conn->isClosed()) {
            unset($active[$id]);
            fwrite(STDERR, "connection closed id={$id} active=" . count($active) . PHP_EOL);
        }
    }

    // Keep timer progression independent from receive frequency.
    $dueAt = $endpoint->getNextExpiry();
    if ($dueAt !== null && $dueAt <= nowMilliseconds()) {
        try {
            $endpoint->handleTimers();
        } catch (LogicException|Error $e) {
            throw $e;
        } catch (Throwable $e) {
            fwrite(STDERR, "endpoint timer warning: {$e->getMessage()}\n");
        }
    }
}
