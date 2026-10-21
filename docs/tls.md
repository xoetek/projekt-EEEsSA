# TLS termination for the game protocol

The game's network protocol is **plaintext raw TCP**. It has no native TLS
support. `scripts/tls_proxy.py` is a small Python 3 (standard library only) TLS
terminator that wraps the existing protocol without changing the game or the
server:

- **server mode** listens with TLS, decrypts each connection and forwards the
  bytes to a local plaintext game server.
- **client mode** listens locally in plaintext, opens a TLS connection to a
  remote proxy and forwards bytes in both directions.

This is TLS **termination**, not native TLS: traffic is encrypted between the
proxies, while the game's own socket remains plaintext on loopback (or the
private hop between proxy and game server).

## 1. Create a certificate

For testing, a self-signed certificate is enough. The hostname in `CN`/SAN must
match the name clients connect to:

```sh
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem -out cert.pem -days 365 \
  -subj "/CN=game.example.com" \
  -addext "subjectAltName=DNS:game.example.com"
chmod 600 key.pem
```

`cert.pem` is the certificate to distribute/verify; `key.pem` stays on the TLS
server host and must not be shared.

## 2. Server side

Run the game server (or `eee_server`) locally and bind it to loopback so only
the TLS proxy can reach it:

```sh
./build/eee_server --bind=127.0.0.1 --port=27015 --password=our-secret
```

Then start the TLS proxy in server mode:

```sh
python3 scripts/tls_proxy.py server \
  --listen 0.0.0.0:3443 \
  --target 127.0.0.1:27015 \
  --cert cert.pem --key key.pem
```

Expose only TCP **3443**; each player's client-mode proxy connects to it with
TLS. The proxy handles many clients, one thread per connection, and relays
until either side closes.

Example with the dedicated server and a password (the password still applies;
TLS only protects the transport):

```sh
./build/eee_server --port=27015 --password=our-secret
python3 scripts/tls_proxy.py server \
  --listen 0.0.0.0:3443 --target 127.0.0.1:27015 \
  --cert cert.pem --key key.pem
```

## 3. Client side

Each player runs a local proxy in client mode. It accepts plaintext from the
game on loopback and connects outward with TLS:

```sh
python3 scripts/tls_proxy.py client \
  --listen 127.0.0.1:27016 \
  --target game.example.com:3443 \
  --ca cert.pem
```

Then start the game against the **local** proxy port:

```sh
./build/eee_demo --join=127.0.0.1:27016 --password=our-secret --class=warrior
```

When connecting by IP but the certificate names a host, pass the certificate
name for SNI and verification:

```sh
python3 scripts/tls_proxy.py client \
  --listen 127.0.0.1:27016 --target 203.0.113.10:3443 \
  --ca cert.pem --server-hostname game.example.com
```

Multiple game connections are supported, though normally there is one per
player. `--idle-timeout SECONDS` closes silent relays (default 900; `0`
disables it).

## 4. Certificate verification

By default the client verifies the server certificate using `--ca` (or the
system trust store when `--ca` is omitted) and checks the hostname. Use a
certificate issued by a real CA, or pin the exact self-signed certificate with
`--ca cert.pem`.

`--insecure` disables verification and is **only** for local testing with
self-signed certificates. It makes the connection vulnerable to
man-in-the-middle attacks; never use it on the internet.

## 5. Firewall and NAT

- Expose only the TLS proxy port (**3443/tcp** in the examples) to the internet.
- Keep the plaintext game port (27015) bound to `127.0.0.1` or a private
  interface; never forward it through the router or firewall.
- Allow inbound 3443/tcp on the server host and outbound 3443/tcp on clients.
  `--listen 0.0.0.0:3443` listens on all IPv4 interfaces; use a specific
  address if the host is multi-homed.
- If the proxy and game server are on different machines, that hop is still
  plaintext; keep it on a trusted/private network.

## 6. Limits

`scripts/tls_proxy.py` is a sample, not a hardened service:

- No DoS hardening, rate limiting, connection caps or authentication.
- One thread per connection; fine for small co-op sessions, not for scale.
- TLS security depends entirely on certificate handling: protect `key.pem`,
  distribute the CA correctly, and do not use `--insecure` in production.
- It terminates TLS; the game protocol, password handling and server logic are
  unchanged and remain the game's responsibility.
