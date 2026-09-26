# atari-chat: notes for agents

This is a Claude chat client for the Atari ST, written in C. All protocol
work runs on the 8 MHz 68000: TLS, HTTP and JSON. See README.md for the
user-facing overview.

## Hard rules

- **TLS, HTTP and JSON stay on the Atari.** Never add a proxy, bridge or
  Worker that terminates TLS or speaks the API for the ST. The only thing
  allowed off the device is a dumb serial↔TCP pipe: `tools/modem.py`, or a
  real WiFi modem. The owner has been explicit about this.
- **Never print or commit secrets.**
  - `.env` holds the Cloudflare token.
  - `obj/config.h` and everything in `build/` contain it (compiled in).
  - All three are git-ignored. Keep it that way.
  - To check config, report which fields are set, not their values.
- **Nothing secret goes on the wire before `tls_verify()` returns 0**
  (see "Cloudflare timing" below).

## Build

- **Toolchain:** `~/.local/opt/cross-mint/usr/bin/m68k-atari-mintelf-gcc`,
  GCC 15.2, installed by `tools/install-toolchain.sh`.
- **`make`** builds the test programs.
- **`make chat`** builds `ATCHAT.PRG` and `CHAT.TOS`. These need `.env`.
- **`make run`** starts `tools/modem.py` and Hatari. `build/` is drive C:.
- **Emulated machine:** Hatari as a Mega STE with `--cpuclock 8`. The Mega
  STE is only for its RTC (certificate checks need the date); the 8 MHz
  clock keeps timings honest for a 1040 STE. The ROM is EmuTOS
  (`emu/etos256us.img`).
- **BearSSL** is an unmodified git submodule, built as `obj/libbearssl.a`
  with `-DBR_LOMUL -DBR_SLOW_MUL -DBR_SLOW_MUL15`.
- **Generated sources:**
  - `obj/fe16_mul.S` comes from `tools/gen_fe16.py`.
  - `obj/config.h` comes from `.env` via `tools/gen_config.py`.
  - `src/tls/trust_anchors.h` was generated with BearSSL's `brssl ta` from
    ISRG Root X1. Regenerate it if the gateway's chain changes.

## Testing

- **Host-side differential tests** check the C code against Python
  references. Build each one with host gcc and feed it cases from Python:
  - `tests/x25519_host.c`: RFC 7748 vectors.
  - `tests/rsa16_host.c`: reads `n e x` hex lines, compare with `pow()`.
  - `tests/json_host.c`: target path in argv, JSON on stdin.

  The rsa16 test links against `third_party/bearssl/build/libbearssl.a`
  (host build: `make -C third_party/bearssl`).
- **On the Atari:** `tools/snap.py PROG --wait N --out shot.png [--every S]
  [--keys 'text\n' --keys-at T]` boots a hidden Hatari, runs a program,
  types keys, and saves screenshots. It uses its own modem instance. Read
  the PNGs to check results.
- **Emulator runs are slow.** A handshake takes ~40 s including boot; a
  first full chain check takes minutes.
  - Run anything longer than about a minute in the background.
  - Take checkpoint screenshots (`--every`) and report progress.
  - Don't block the session on one long command.
- **Timings to expect (8 MHz):**

  | Operation | Time |
  |---|---|
  | X25519 | 8 s |
  | RSA-2048 verify | 7 s |
  | RSA-4096 verify | 27 s |
  | Handshake | 12.1 s |
  | Connection ready (cached chain) | ~21 s |

## Cloudflare AI Gateway timing (measured; don't rediscover)

- The TLS handshake must complete within ~15 s of the TCP connect, even if
  the client trickles handshake fragments.
- The **first HTTP request** must also arrive within ~15 s of connecting.
- An idle keep-alive connection survives at least 60 s.
- So the flow is:
  1. precompute the X25519 key pair before dialing;
  2. do the handshake, with signature and chain checks deferred;
  3. immediately send a harmless `GET /` with keep-alive;
  4. run `tls_verify()`;
  5. only then send the POST with the token.
- The server supports only ECDHE (no plain RSA key exchange). Offering only
  the RSA suite gets an RSA certificate chain, which is far cheaper to
  verify than ECDSA on a 68000.

## Gotchas

- **Serial devices:** `Iorec()` numbers devices differently from
  `Bcon*()`. RS-232 is `Iorec(0)` but `Bconin(1)`. Mixing them up silently
  enlarged the keyboard buffer instead of the serial one.
- **Dates and times:** `Tgetdate()`/`Tgettime()` must be masked with
  `& 0xffff`, or they sign-extend after 16:00.
- **mintlib stack:** the default stack is too small for the crypto code.
  Programs set `long _stksize = 65536;`.
- **GEM text cells:** get the cell size from `vqt_attributes` (8x16 on
  mono), not from `graf_handle`'s box size.
- **Hatari's serial port** delivers a byte only after the ST reads the
  previous one, so it never overruns. Data loss was always on the ST side.
- **Rebuilding while Hatari runs** is fine: programs are loaded from C: at
  launch.

## Layout

| Path | Contains |
|---|---|
| `src/app/` | GEM app: main.c (window, event loop), transcript.c (messages, wrapping), claude.c (connection manager, request/response) |
| `src/tls/` | TLS client, deferred verification and chain cache, entropy |
| `src/x25519/` | X25519 field arithmetic and ladder, BearSSL `br_ec_impl` |
| `src/rsa/` | rsa16: Montgomery RSA with the assembly `addmul_1` |
| `src/` | serial, modem, http, json, charset, menu, test programs |
| `tools/` | modem.py, snap.py, gen_fe16.py, gen_config.py, install-toolchain.sh |
