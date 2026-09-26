#!/usr/bin/env python3
"""
Turn .env into a C header (obj/config.h) so credentials are compiled into
the program without ever being in the source tree. .env and obj/ are both
git-ignored; .env.example shows what's expected.

    tools/gen_config.py .env obj/config.h
"""
import sys

REQUIRED = ["AIG_ACCOUNT_ID", "AIG_GATEWAY", "AIG_MODEL"]
OPTIONAL = ["AIG_TOKEN", "PROVIDER_API_KEY"]


def parse(path):
    values = {}
    for n, line in enumerate(open(path), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            sys.exit(f"{path}:{n}: expected NAME=value")
        name, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[name.strip()] = value
    return values


def c_string(s):
    out = []
    for ch in s:
        if ch in '"\\':
            out.append("\\" + ch)
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        else:
            sys.exit(f"non-ASCII or control character in value: {s!r}")
    return '"' + "".join(out) + '"'


def main():
    src, dst = sys.argv[1], sys.argv[2]
    try:
        values = parse(src)
    except FileNotFoundError:
        sys.exit(f"{src} not found: copy .env.example to .env and fill it in")
    missing = [k for k in REQUIRED if not values.get(k)]
    if missing:
        sys.exit(f"{src}: missing {', '.join(missing)}")

    lines = ["/* Generated from .env by tools/gen_config.py -- do not commit. */",
             "#ifndef CONFIG_H", "#define CONFIG_H", ""]
    for k in REQUIRED + OPTIONAL:
        v = values.get(k, "")
        lines.append(f"#define {k} {c_string(v)}")
    lines += ["", "#endif", ""]
    open(dst, "w").write("\n".join(lines))


if __name__ == "__main__":
    main()
