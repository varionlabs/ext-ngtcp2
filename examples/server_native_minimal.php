<?php

declare(strict_types=1);

fwrite(
    STDERR,
    "examples/server_native_minimal.php is deprecated; use examples/server_minimal.php\n"
);

require __DIR__ . '/server_minimal.php';
