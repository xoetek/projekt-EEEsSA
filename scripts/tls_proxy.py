#!/usr/bin/env python3
"""TLS terminator for the game's plaintext TCP protocol.

The game speaks raw TCP. This sample proxy adds TLS without touching the game:

  server mode: accept TLS connections and forward decrypted bytes to a local
               plaintext game server (or dedicated server).
  client mode: accept plaintext local connections from the game and forward
               them over TLS to a remote TLS proxy.

Examples:
  python3 scripts/tls_proxy.py server --listen 0.0.0.0:3443 \
      --target 127.0.0.1:27015 --cert cert.pem --key key.pem

  python3 scripts/tls_proxy.py client --listen 127.0.0.1:27016 \
      --target game.example.com:3443 --ca cert.pem

This is a sample, not a hardened production service. See docs/tls.md.
"""

from __future__ import annotations

import argparse
import math
import os
import select
import signal
import socket
import ssl
import sys
import threading
import time

DEFAULT_IDLE_TIMEOUT = 900.0
CONNECT_TIMEOUT = 10.0
HANDSHAKE_TIMEOUT = 15.0
ACCEPT_TIMEOUT = 0.5
HALF_CLOSE_GRACE = 5.0
RELAY_BUFSIZE = 64 * 1024
LISTEN_BACKLOG = 64

STOP = threading.Event()
_log_lock = threading.Lock()
_listener_lock = threading.Lock()
_listener: socket.socket | None = None
_workers_lock = threading.Lock()
_workers: set[threading.Thread] = set()


def log(message: str) -> None:
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    with _log_lock:
        print(f"[{stamp}] {message}", flush=True)


def format_peer(address: object) -> str:
    if isinstance(address, tuple) and len(address) >= 2:
        host, port = address[0], address[1]
        if ":" in str(host):
            return f"[{host}]:{port}"
        return f"{host}:{port}"
    return str(address)


def parse_endpoint(value: str) -> tuple[str, int]:
    """Parse host:port (and [IPv6]:port) with strict port validation."""
    if not value or value.strip() != value:
        raise argparse.ArgumentTypeError(
            f"invalid endpoint {value!r}: expected host:port"
        )

    if value.startswith("["):
        end = value.find("]")
        if end < 0:
            raise argparse.ArgumentTypeError(
                f"invalid endpoint {value!r}: missing ']'"
            )
        host = value[1:end]
        rest = value[end + 1 :]
        if not rest.startswith(":"):
            raise argparse.ArgumentTypeError(
                f"invalid endpoint {value!r}: expected [host]:port"
            )
        port_text = rest[1:]
    else:
        host, sep, port_text = value.rpartition(":")
        if not sep:
            raise argparse.ArgumentTypeError(
                f"invalid endpoint {value!r}: expected host:port"
            )
        if ":" in host:
            raise argparse.ArgumentTypeError(
                f"invalid endpoint {value!r}: bracket IPv6 as [address]:port"
            )

    if not host:
        raise argparse.ArgumentTypeError(
            f"invalid endpoint {value!r}: empty host"
        )
    if not port_text.isdigit():
        raise argparse.ArgumentTypeError(
            f"invalid endpoint {value!r}: port must be a number"
        )

    port = int(port_text, 10)
    if not 1 <= port <= 65535:
        raise argparse.ArgumentTypeError(
            f"invalid endpoint {value!r}: port must be 1-65535"
        )
    return host, port


def parse_timeout(value: str) -> float:
    try:
        seconds = float(value)
    except (TypeError, ValueError):
        raise argparse.ArgumentTypeError(
            f"invalid timeout {value!r}: expected seconds"
        ) from None
    if not math.isfinite(seconds) or seconds < 0:
        raise argparse.ArgumentTypeError(
            f"invalid timeout {value!r}: must be a finite value >= 0"
        )
    return seconds


def configure_socket(sock: socket.socket) -> None:
    """Enable keepalive and low-latency forwarding where supported."""
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    except OSError:
        pass
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass

    keepalive_options = (
        ("TCP_KEEPIDLE", 60),
        ("TCP_KEEPINTVL", 10),
        ("TCP_KEEPCNT", 6),
    )
    for name, value in keepalive_options:
        option = getattr(socket, name, None)
        if option is None:
            continue
        try:
            sock.setsockopt(socket.IPPROTO_TCP, option, value)
        except OSError:
            pass


def create_listener(host: str, port: int) -> socket.socket:
    """Bind a TCP listener, trying the addresses getaddrinfo returns."""
    infos = socket.getaddrinfo(
        host, port, type=socket.SOCK_STREAM, flags=socket.AI_PASSIVE
    )
    last_error: OSError | None = None
    for family, socktype, proto, _canonname, sockaddr in infos:
        sock = socket.socket(family, socktype, proto)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if family == socket.AF_INET6:
                try:
                    sock.setsockopt(
                        socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0
                    )
                except OSError:
                    pass
            sock.bind(sockaddr)
            sock.listen(LISTEN_BACKLOG)
            sock.settimeout(ACCEPT_TIMEOUT)
            return sock
        except OSError as exc:
            last_error = exc
            sock.close()
    if last_error is not None:
        raise last_error
    raise OSError(f"could not resolve {host}:{port}")


def set_listener(sock: socket.socket) -> None:
    global _listener
    with _listener_lock:
        _listener = sock


def shutdown_listener() -> None:
    with _listener_lock:
        listener = _listener
        _listener = None
    if listener is not None:
        try:
            listener.close()
        except OSError:
            pass


def install_signal_handlers() -> None:
    def handle(signum: int, _frame: object) -> None:
        if STOP.is_set():
            return
        log(f"received signal {signum}; shutting down")
        STOP.set()
        shutdown_listener()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(sig, handle)
        except (ValueError, OSError):
            pass


def spawn_worker(target: object, *args: object) -> None:
    thread: threading.Thread

    def run() -> None:
        try:
            target(*args)  # type: ignore[operator]
        finally:
            with _workers_lock:
                _workers.discard(thread)

    thread = threading.Thread(target=run, daemon=True)
    with _workers_lock:
        _workers.add(thread)
    thread.start()


def join_workers(timeout: float = 3.0) -> None:
    deadline = time.monotonic() + timeout
    while True:
        with _workers_lock:
            alive = [thread for thread in _workers if thread.is_alive()]
        if not alive:
            break
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            log(f"{len(alive)} connection(s) still active at shutdown")
            break
        alive[0].join(remaining)


def close_socket(sock: socket.socket | None) -> None:
    if sock is None:
        return
    try:
        sock.close()
    except OSError:
        pass


def relay(
    left: socket.socket,
    right: socket.socket,
    label: str,
    idle_timeout: float,
) -> None:
    """Copy bytes both ways until EOF, shutdown is requested, or idle timeout.

    An EOF is not propagated with shutdown(SHUT_WR) on the TLS side: Python's
    SSLSocket.shutdown() detaches the SSL object, after which reads would
    return raw ciphertext. Instead the other direction is drained for a short
    grace period and the connection is then closed.
    """
    peers = {left: right, right: left}
    names = {left: "local", right: "remote"}
    readable: set[socket.socket] = {left, right}
    last_activity = time.monotonic()
    close_deadline: float | None = None

    for sock in (left, right):
        configure_socket(sock)

    try:
        while readable and not STOP.is_set():
            now = time.monotonic()
            if idle_timeout > 0 and now - last_activity > idle_timeout:
                log(f"{label}: idle timeout after {idle_timeout:.0f}s")
                break
            if close_deadline is not None and now >= close_deadline:
                break

            # Decrypted TLS bytes may already be buffered inside the SSL
            # object, where select() on the raw fd cannot see them.
            ready = []
            for sock in readable:
                if isinstance(sock, ssl.SSLSocket):
                    try:
                        if sock.pending():
                            ready.append(sock)
                    except (OSError, ValueError):
                        pass
            if not ready:
                wait = 1.0
                if close_deadline is not None:
                    wait = max(0.0, min(wait, close_deadline - now))
                try:
                    ready, _, _ = select.select(list(readable), [], [], wait)
                except (OSError, ValueError):
                    break

            for source in ready:
                if source not in readable:
                    continue
                try:
                    data = source.recv(RELAY_BUFSIZE)
                except (ssl.SSLError, OSError) as exc:
                    log(f"{label}: {names[source]} recv error: {exc}")
                    data = b""

                if not data:
                    readable.discard(source)
                    destination = peers[source]
                    if not isinstance(destination, ssl.SSLSocket):
                        try:
                            destination.shutdown(socket.SHUT_WR)
                        except OSError:
                            pass
                    if close_deadline is None:
                        close_deadline = time.monotonic() + HALF_CLOSE_GRACE
                    log(f"{label}: {names[source]} closed")
                    continue

                last_activity = time.monotonic()
                try:
                    peers[source].sendall(data)
                except (ssl.SSLError, OSError) as exc:
                    log(f"{label}: {names[source]} send error: {exc}")
                    return
    finally:
        close_socket(left)
        close_socket(right)


def wrap_tls_server(sock: socket.socket, context: ssl.SSLContext) -> ssl.SSLSocket:
    sock.settimeout(HANDSHAKE_TIMEOUT)
    tls = context.wrap_socket(sock, server_side=True)
    tls.settimeout(None)
    return tls


def handle_server_connection(
    connection: socket.socket,
    address: object,
    context: ssl.SSLContext,
    target: tuple[str, int],
    idle_timeout: float,
) -> None:
    peer = format_peer(address)
    tls: ssl.SSLSocket | None = None
    upstream: socket.socket | None = None
    try:
        log(f"server: {peer} connected; starting TLS handshake")
        tls = wrap_tls_server(connection, context)
        log(f"server: {peer} TLS established; connecting to "
            f"{target[0]}:{target[1]}")

        upstream = socket.create_connection(target, timeout=CONNECT_TIMEOUT)
        configure_socket(upstream)
        upstream.settimeout(None)
        log(f"server: {peer} forwarding")

        relay(upstream, tls, f"server {peer}", idle_timeout)
        log(f"server: {peer} closed")
    except ssl.SSLError as exc:
        log(f"server: {peer} TLS error: {exc}")
    except OSError as exc:
        log(f"server: {peer} error: {exc}")
    except Exception as exc:  # keep one bad connection from killing the proxy
        log(f"server: {peer} unexpected error: {exc!r}")
    finally:
        close_socket(upstream)
        close_socket(tls)
        if tls is None:
            close_socket(connection)


def handle_client_connection(
    connection: socket.socket,
    address: object,
    context: ssl.SSLContext,
    target: tuple[str, int],
    server_hostname: str,
    idle_timeout: float,
) -> None:
    peer = format_peer(address)
    tls: ssl.SSLSocket | None = None
    upstream: socket.socket | None = None
    try:
        log(f"client: {peer} connected; connecting to "
            f"{target[0]}:{target[1]}")
        upstream = socket.create_connection(target, timeout=CONNECT_TIMEOUT)
        configure_socket(upstream)
        upstream.settimeout(HANDSHAKE_TIMEOUT)
        tls = context.wrap_socket(upstream, server_hostname=server_hostname)
        tls.settimeout(None)
        log(f"client: {peer} TLS established (SNI {server_hostname!r}); "
            f"forwarding")

        relay(connection, tls, f"client {peer}", idle_timeout)
        log(f"client: {peer} closed")
    except ssl.SSLCertVerificationError as exc:
        log(f"client: {peer} certificate verification failed: {exc}")
    except ssl.SSLError as exc:
        log(f"client: {peer} TLS error: {exc}")
    except OSError as exc:
        log(f"client: {peer} error: {exc}")
    except Exception as exc:  # keep one bad connection from killing the proxy
        log(f"client: {peer} unexpected error: {exc!r}")
    finally:
        close_socket(tls)
        close_socket(upstream)
        if tls is None:
            close_socket(connection)


def serve(
    listen: tuple[str, int],
    handler: object,
    handler_args: tuple[object, ...],
) -> int:
    host, port = listen
    try:
        listener = create_listener(host, port)
    except OSError as exc:
        log(f"error: cannot listen on {host}:{port}: {exc}")
        return 1

    set_listener(listener)
    bound = format_peer(listener.getsockname())
    log(f"listening on {bound}")

    try:
        while not STOP.is_set():
            try:
                connection, address = listener.accept()
            except socket.timeout:
                continue
            except OSError as exc:
                if STOP.is_set():
                    break
                log(f"accept error: {exc}")
                continue

            if STOP.is_set():
                close_socket(connection)
                break
            configure_socket(connection)
            spawn_worker(handler, connection, address, *handler_args)
    finally:
        shutdown_listener()
        stop_workers()

    return 0


def stop_workers() -> None:
    if not STOP.is_set():
        STOP.set()
    join_workers()


def make_server_context(cert: str, key: str) -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certfile=cert, keyfile=key)
    return context


def make_client_context(ca: str | None, insecure: bool) -> ssl.SSLContext:
    if insecure:
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
    else:
        context = ssl.create_default_context(cafile=ca)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    return context


def check_readable_file(parser: argparse.ArgumentParser, path: str,
                        option: str) -> None:
    if not os.path.isfile(path):
        parser.error(f"{option}: file not found: {path}")
    if not os.access(path, os.R_OK):
        parser.error(f"{option}: file is not readable: {path}")


def warn_weak_key_permissions(key_path: str) -> None:
    try:
        mode = os.stat(key_path).st_mode
    except OSError:
        return
    if mode & 0o077:
        log(f"warning: {key_path} is readable by group/others; "
            f"restrict it with: chmod 600 {key_path}")


def add_common_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--listen",
        required=True,
        metavar="HOST:PORT",
        type=parse_endpoint,
        help="local address to listen on (e.g. 0.0.0.0:3443)",
    )
    parser.add_argument(
        "--target",
        required=True,
        metavar="HOST:PORT",
        type=parse_endpoint,
        help="address to forward to",
    )
    parser.add_argument(
        "--idle-timeout",
        type=parse_timeout,
        default=DEFAULT_IDLE_TIMEOUT,
        metavar="SECONDS",
        help="close a connection after this much silence; 0 disables "
        f"(default: {DEFAULT_IDLE_TIMEOUT:.0f})",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="tls_proxy.py",
        description="TLS terminator for the game's plaintext TCP protocol.",
        epilog=(
            "Server example:\n"
            "  python3 scripts/tls_proxy.py server "
            "--listen 0.0.0.0:3443 --target 127.0.0.1:27015 "
            "--cert cert.pem --key key.pem\n\n"
            "Client example:\n"
            "  python3 scripts/tls_proxy.py client "
            "--listen 127.0.0.1:27016 --target game.example.com:3443 "
            "--ca cert.pem\n\n"
            "Use --insecure only for local testing with self-signed certs."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest="mode", required=True)

    server = subparsers.add_parser(
        "server",
        help="accept TLS and forward plaintext to a local game server",
        description="Accept TLS connections and forward decrypted bytes to "
        "a plaintext target.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    add_common_options(server)
    server.add_argument(
        "--cert",
        required=True,
        metavar="PATH",
        help="PEM certificate chain",
    )
    server.add_argument(
        "--key",
        required=True,
        metavar="PATH",
        help="PEM private key",
    )

    client = subparsers.add_parser(
        "client",
        help="accept plaintext locally and forward over TLS",
        description="Accept plaintext local game connections and forward "
        "them to a remote TLS terminator.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    add_common_options(client)
    client.add_argument(
        "--ca",
        metavar="PATH",
        default=None,
        help="PEM CA file used to verify the server (default: system store)",
    )
    client.add_argument(
        "--insecure",
        action="store_true",
        help="disable certificate verification (testing only; insecure)",
    )
    client.add_argument(
        "--server-hostname",
        metavar="NAME",
        default=None,
        help="hostname for SNI/certificate verification when --target is an IP",
    )
    return parser


def run_server(parser: argparse.ArgumentParser, args: argparse.Namespace) -> int:
    check_readable_file(parser, args.cert, "--cert")
    check_readable_file(parser, args.key, "--key")
    warn_weak_key_permissions(args.key)

    try:
        context = make_server_context(args.cert, args.key)
    except (ssl.SSLError, OSError) as exc:
        log(f"error: could not load certificate/key: {exc}")
        return 1

    log(f"server mode: {args.listen[0]}:{args.listen[1]} (TLS) -> "
        f"{args.target[0]}:{args.target[1]} (plaintext)")
    return serve(
        args.listen,
        handle_server_connection,
        (context, args.target, args.idle_timeout),
    )


def run_client(parser: argparse.ArgumentParser, args: argparse.Namespace) -> int:
    if args.ca is not None:
        check_readable_file(parser, args.ca, "--ca")
    if args.insecure:
        log("warning: --insecure disables certificate verification; "
            "use only for testing")

    try:
        context = make_client_context(args.ca, args.insecure)
    except (ssl.SSLError, OSError) as exc:
        log(f"error: could not configure TLS client: {exc}")
        return 1

    server_hostname = args.server_hostname or args.target[0]
    log(f"client mode: {args.listen[0]}:{args.listen[1]} (plaintext) -> "
        f"{args.target[0]}:{args.target[1]} (TLS, SNI {server_hostname!r})")
    return serve(
        args.listen,
        handle_client_connection,
        (context, args.target, server_hostname, args.idle_timeout),
    )


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    install_signal_handlers()

    if args.mode == "server":
        return run_server(parser, args)
    return run_client(parser, args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        STOP.set()
        shutdown_listener()
        sys.exit(0)
    except BrokenPipeError:
        sys.exit(0)
