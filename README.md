# README #

Building the www app:
--------------------------------------------------
1. prereqs: clang 5, cmake 3, make
2. building:  under src, create a directory called build
    from the build directory, run cmake .. -DAARCH_TOOLCHAIN_DIR=<full path of linaro tool chain root>
3. run make

Testing the HTTP server (httptest + basher):
--------------------------------------------------
The repository includes a probabilistic load-testing framework for the HTTP
server, made of two executables:

* `httptest` — a test HTTP service that exercises the request/response body
  paths in `http_request` / `http_response`. It registers handlers under
  `/echo` and a default "raw" controller under any other path:
    * `POST /echo/echo`     — reads the full body and echoes it back. Response
                              framing is chosen by `?mode=chunked|cl` and
                              defaults to mirroring the request framing.
    * `POST /echo/checksum` — reads the body as byte arrays and returns
                              `{"length":N,"checksum":H}` (FNV-1a).
    * `POST /echo/sink`     — consumes the body and returns 204 No Content.
    * `GET  /echo/stream`   — generates a deterministic body of `?size=` bytes
                              seeded by `?seed=`, framed by `?mode=chunked|cl`.
    * `POST /raw/bytes`     — default controller; reads the body with fixed-size
                              byte-array reads and echoes it back.
* `basher` — a multi-threaded client that reuses or re-creates connections,
  varies request sizes and methods (GET / POST / DELETE), sends both chunked
  and content-length framed bodies, verifies every response, and can inject
  malformed requests to exercise boundary conditions. It exits 0 on success and
  1 if any mismatch or server-liveness failure is detected.

### 1. Build the test tools

From your build directory (configured with the `src` tree as the source dir):

```sh
# configure once (only needed the first time, or after adding files)
cmake ../src

# build just the two test targets
make httptest basher
```

### 2. Start the test server

Run the server on a port of your choice (defaults to 8080). Use `setsid` /
`disown` so it keeps running independently of your shell:

```sh
setsid ./httptest/httptest --port 8099 --log-level 4 </dev/null >/tmp/httptest.log 2>&1 &
disown
```

`httptest` options:

| option              | description                          | default |
| ------------------- | ------------------------------------ | ------- |
| `--port N`          | HTTP listen port                     | 8080    |
| `--log-level L`     | log level 0 (none) .. 6 (fatal)      | 3       |

### 3. Run a comprehensive test

The `basher` client takes a thread count and a total request count, and
randomizes request types, sizes, framing, methods, and connection reuse.

```sh
# Smoke test: single thread, well-formed requests only
./basher/basher --host 127.0.0.1 --port 8099 --threads 1 --count 100 --seed 1

# Concurrency: 16 threads, 20,000 requests, mixed framing/methods/sizes
./basher/basher --host 127.0.0.1 --port 8099 --threads 16 --count 20000 --seed 7

# Boundary / fault injection: 8 threads, 25% malformed requests
./basher/basher --host 127.0.0.1 --port 8099 --threads 8 --count 5000 \
                --fault-rate 0.25 --seed 11

# Full comprehensive run: high concurrency, large bodies, 10% faults,
# 60% connection reuse
./basher/basher --host 127.0.0.1 --port 8099 --threads 24 --count 50000 \
                --fault-rate 0.1 --max-size 131072 --keepalive-rate 0.6 --seed 99
```

`basher` options:

| option                 | description                                    | default     |
| ---------------------- | ---------------------------------------------- | ----------- |
| `--host H`             | target host                                    | 127.0.0.1   |
| `--port N`             | target port                                    | 8080        |
| `--threads N`          | worker threads                                 | 4           |
| `--count N`            | total requests to send across all threads      | 1000        |
| `--seed N`             | base RNG seed (use a fixed value to reproduce) | 0           |
| `--fault-rate F`       | probability 0..1 of sending a malformed request| 0           |
| `--max-size N`         | maximum body size in bytes                     | 65536       |
| `--keepalive-rate F`   | probability 0..1 of reusing a connection       | 0.5         |
| `--timeout N`          | per-request socket timeout in milliseconds     | 30000       |

At the end of a run `basher` prints a summary (requests sent, verified ok,
mismatches, transport errors, status-code distribution, connection reuse,
faults handled, throughput) followed by a server liveness check and an overall
`PASS` / `FAIL` result. A passing run reports `mismatches: 0` and
`server liveness: OK`. Use the process exit code (`echo $?`) in scripts:
`0` = pass, `1` = fail.

### 4. Stop the test server

```sh
pkill -f 'httptest --port 8099'
```
