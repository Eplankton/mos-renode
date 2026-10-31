#!/usr/bin/env python3
"""Minimal telnet UART console for Renode.

Renode's CreateServerSocketTerminal speaks telnet.  This client strips the
negotiation bytes, prints UART output, and forwards stdin to one selected
port.  It can multiplex several ports and prefix each line with a label.
"""

import argparse
import os
import selectors
import socket
import sys
import time

IAC = 255
DONT = 254
DO = 253
WONT = 252
WILL = 251


class TelnetFilter:
    def __init__(self):
        self._pending = bytearray()

    def feed(self, data):
        self._pending.extend(data)
        out = bytearray()
        reply = bytearray()
        i = 0
        n = len(self._pending)

        while i < n:
            byte = self._pending[i]
            if byte != IAC:
                out.append(byte)
                i += 1
                continue

            if i + 1 >= n:
                break

            command = self._pending[i + 1]
            if command == IAC:
                out.append(IAC)
                i += 2
                continue

            if command in (DO, DONT, WILL, WONT):
                if i + 2 >= n:
                    break
                option = self._pending[i + 2]
                if command == DO:
                    reply.extend((IAC, WONT, option))
                elif command == WILL:
                    reply.extend((IAC, DONT, option))
                i += 3
                continue

            i += 2

        del self._pending[:i]
        return bytes(out), bytes(reply)


def parse_endpoint(spec):
    if ":" in spec:
        port, label = spec.split(":", 1)
    else:
        port = spec
        label = f"UART{port}"
    return int(port), label


def connect_with_retry(port, timeout):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1.0)
            sock.setblocking(False)
            return sock
        except OSError as exc:
            last_error = exc
            time.sleep(0.2)
    raise last_error


def main():
    parser = argparse.ArgumentParser(description="Renode UART console")
    parser.add_argument(
        "--port",
        action="append",
        required=True,
        metavar="PORT[:LABEL]",
        help="UART socket to connect; repeat for multiple machines",
    )
    parser.add_argument(
        "--send-port",
        type=int,
        help="port that receives stdin (default: first --port)",
    )
    parser.add_argument(
        "--connect-timeout",
        type=float,
        default=15.0,
        help="seconds to wait for Renode sockets",
    )
    args = parser.parse_args()

    endpoints = [parse_endpoint(spec) for spec in args.port]
    send_index = 0
    if args.send_port is not None:
        for index, (port, _) in enumerate(endpoints):
            if port == args.send_port:
                send_index = index
                break
        else:
            parser.error(f"--send-port {args.send_port} is not in --port")

    uarts = []
    try:
        for port, label in endpoints:
            sock = connect_with_retry(port, args.connect_timeout)
            uarts.append(
                {
                    "port": port,
                    "label": label,
                    "sock": sock,
                    "telnet": TelnetFilter(),
                    "buffer": "",
                    "last_rx": 0.0,
                }
            )
    except OSError as exc:
        print(f"Could not connect to Renode UART: {exc}", file=sys.stderr)
        for uart in uarts:
            uart["sock"].close()
        return 1

    multi = len(uarts) > 1
    selector = selectors.DefaultSelector()
    for index, uart in enumerate(uarts):
        selector.register(uart["sock"], selectors.EVENT_READ, ("uart", index))

    stdin_fd = None
    if sys.stdin.isatty():
        stdin_fd = sys.stdin.fileno()
        selector.register(stdin_fd, selectors.EVENT_READ, ("stdin", -1))

    if multi:
        ports_text = ", ".join(f"{u['label']}@{u['port']}" for u in uarts)
    else:
        ports_text = str(uarts[0]["port"])
    print(f"Connected: {ports_text}", file=sys.stderr)
    print("Ctrl-C to exit.\n", file=sys.stderr)

    def flush_lines(index, force=False):
        uart = uarts[index]
        while "\n" in uart["buffer"]:
            line, uart["buffer"] = uart["buffer"].split("\n", 1)
            print(f"[{uart['label']}] {line.rstrip(chr(13))}", flush=True)
        if force and uart["buffer"]:
            print(f"[{uart['label']}] {uart['buffer']}", flush=True)
            uart["buffer"] = ""

    def emit(index, text):
        uart = uarts[index]
        if not multi:
            sys.stdout.write(text)
            sys.stdout.flush()
            return

        uart["buffer"] += text
        uart["last_rx"] = time.time()
        flush_lines(index)

    try:
        while uarts:
            for key, _ in selector.select(timeout=0.5):
                kind, index = key.data

                if kind == "stdin":
                    try:
                        data = os.read(stdin_fd, 1024)
                    except OSError:
                        data = b""
                    if not data:
                        selector.unregister(stdin_fd)
                        stdin_fd = None
                        continue
                    try:
                        uarts[send_index]["sock"].sendall(data)
                    except OSError:
                        return 0
                    continue

                uart = uarts[index]
                try:
                    data = uart["sock"].recv(4096)
                except BlockingIOError:
                    continue
                except OSError:
                    data = b""

                if not data:
                    if multi:
                        flush_lines(index, force=True)
                    selector.unregister(uart["sock"])
                    uart["sock"].close()
                    uarts.pop(index)
                    # Rebuild registrations because indices have shifted.
                    selector.close()
                    selector = selectors.DefaultSelector()
                    for new_index, current in enumerate(uarts):
                        selector.register(
                            current["sock"], selectors.EVENT_READ, ("uart", new_index)
                        )
                    if stdin_fd is not None:
                        selector.register(stdin_fd, selectors.EVENT_READ, ("stdin", -1))
                    if send_index >= len(uarts):
                        send_index = 0
                    break

                cleaned, reply = uart["telnet"].feed(data)
                if reply:
                    try:
                        uart["sock"].sendall(reply)
                    except OSError:
                        pass
                if cleaned:
                    emit(index, cleaned.decode("utf-8", "replace"))

            now = time.time()
            for index, uart in enumerate(uarts):
                if multi and uart["buffer"] and now - uart["last_rx"] > 0.2:
                    flush_lines(index, force=True)
    except KeyboardInterrupt:
        print()
    finally:
        for uart in uarts:
            try:
                uart["sock"].close()
            except OSError:
                pass
        selector.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
