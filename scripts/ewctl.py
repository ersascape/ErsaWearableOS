#!/usr/bin/env python3
"""Host client for the Ersa Wearable NDJSON USB control protocol."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import os
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Optional
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

from rich import box
from rich.console import Console
from rich.panel import Panel
from rich.table import Table

PROTOCOL_VERSION = 1
MAX_FRAME_BYTES = 4096  # Host-side ceiling; firmware is expected to enforce 512 B.
DEFAULT_BAUD = 115200
console = Console()
COMMANDS = {
    "status": "system.status",
    "battery": "battery.read",
    "ble": "ble.status",
    "ota": "ota.status",
    "power": "power.status",
    "logs": "logs.read",
}


class EwctlError(Exception):
    """A user-facing connection or protocol error."""


def encode_request(request_id: int, command: str, args: Optional[dict] = None) -> bytes:
    if not command or "\n" in command or "\r" in command:
        raise EwctlError("command must be a non-empty single-line name")
    request: dict[str, Any] = {"v": PROTOCOL_VERSION, "id": request_id, "cmd": command}
    if args:
        request["args"] = args
    frame = json.dumps(request, separators=(",", ":"), ensure_ascii=True).encode("utf-8") + b"\n"
    if len(frame) > MAX_FRAME_BYTES:
        raise EwctlError("request exceeds host frame limit")
    return frame


def decode_response(line: bytes) -> Optional[dict]:
    """Decode a reply, returning None for console chatter or malformed lines."""
    if len(line) > MAX_FRAME_BYTES:
        return None
    try:
        value = json.loads(line.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def find_port(explicit: Optional[str]) -> str:
    if explicit:
        return explicit
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise EwctlError("pyserial is required; install with: python3 -m pip install -r requirements-ewctl.txt") from exc

    ports = list(list_ports.comports())
    # ESP32-C3 native USB Serial/JTAG commonly enumerates as Espressif VID 0x303A.
    esp = [p.device for p in ports if p.vid == 0x303A]
    if len(esp) == 1:
        return esp[0]
    if len(esp) > 1:
        raise EwctlError("multiple Espressif serial devices found; pass --port explicitly")
    candidates = [p.device for p in ports if p.device.startswith(("/dev/ttyACM", "/dev/ttyUSB"))]
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise EwctlError("no serial device found; connect the watch or pass --port")
    raise EwctlError("multiple serial devices found; pass --port explicitly")


class Session:
    def __init__(self, port: str, baud: int, timeout: float):
        try:
            import serial
        except ImportError as exc:
            raise EwctlError("pyserial is required; install with: python3 -m pip install -r requirements-ewctl.txt") from exc
        try:
            self.serial = serial.Serial(port=port, baudrate=baud, timeout=0.15, write_timeout=timeout)
        except Exception as exc:  # pyserial exposes platform-specific exception classes.
            raise EwctlError(f"could not open {port}: {exc}") from exc
        self.timeout = timeout
        self.next_id = int(time.monotonic_ns() & 0x7FFFFFFF) or 1

    def close(self) -> None:
        self.serial.close()

    def __enter__(self) -> "Session":
        return self

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> None:
        self.close()

    def request(self, command: str, args: Optional[dict] = None) -> dict:
        request_id = self.next_id
        self.next_id = (self.next_id + 1) & 0x7FFFFFFF or 1
        frame = encode_request(request_id, command, args)
        try:
            self.serial.write(frame)
            self.serial.flush()
        except Exception as exc:
            raise EwctlError(f"write failed: {exc}") from exc

        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            try:
                line = self.serial.readline(MAX_FRAME_BYTES + 1)
            except Exception as exc:
                raise EwctlError(f"read failed: {exc}") from exc
            if not line:
                continue
            if len(line) > MAX_FRAME_BYTES and not line.endswith(b"\n"):
                # Skip this chunk without an unbounded drain loop. Its eventual
                # tail will fail JSON parsing; the outer deadline remains active.
                continue
            reply = decode_response(line.strip())
            if reply is None or reply.get("id") != request_id:
                continue
            if reply.get("v") != PROTOCOL_VERSION:
                raise EwctlError(f"device replied with unsupported protocol version: {reply.get('v')!r}")
            return reply
        raise EwctlError(f"timeout waiting for reply to {command!r}; is the watch firmware control bridge enabled?")


def capture_raw_logs(port: str, baud: int, output: Optional[str]) -> int:
    """Copy the console stream read-only, without touching USB reset lines."""
    del baud  # Native USB Serial/JTAG ignores the configured UART baud rate.
    stream = None
    fd = None
    try:
        if output:
            log_path = os.path.abspath(os.path.expanduser(output))
            os.makedirs(os.path.dirname(log_path) or ".", exist_ok=True)
            stream = open(log_path, "wb", buffering=0)
            destination = stream
        else:
            destination = getattr(sys.stdout, "buffer", None)
            if destination is None:
                raise EwctlError("raw log capture needs a binary stdout stream or --output PATH")

        # Opening a serial port through pyserial changes DTR/RTS. On some
        # USB Serial/JTAG boards those modem-control lines reset the ESP32 or
        # select its ROM downloader. O_RDONLY leaves them untouched.
        fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
        print(f"ewctl: capturing raw USB logs from {port}; press Ctrl-C to stop", file=sys.stderr)
        if output:
            print(f"ewctl: writing raw logs to {log_path}", file=sys.stderr)
        try:
            while True:
                readable, _, _ = select.select([fd], [], [], 0.25)
                if not readable:
                    continue
                try:
                    chunk = os.read(fd, 1024)
                except BlockingIOError:
                    continue
                if not chunk:
                    continue
                destination.write(chunk)
                if not output:
                    destination.flush()
        except KeyboardInterrupt:
            return 0
    except EwctlError:
        raise
    except OSError as exc:
        raise EwctlError(f"raw log capture failed on {port}: {exc}") from exc
    finally:
        if fd is not None:
            os.close(fd)
        if stream is not None:
            stream.close()
    return 0


def parse_args_json(text: Optional[str]) -> Optional[dict]:
    if text is None:
        return None
    try:
        value = json.loads(text)
    except json.JSONDecodeError as exc:
        raise EwctlError(f"invalid --args JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise EwctlError("--args must be a JSON object")
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="ewctl", description="Inspect an Ersa Wearable over its USB serial control bridge.")
    parser.add_argument("--port", help="serial port (auto-detects a single ESP32 device)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="serial baud rate (default: %(default)s)")
    parser.add_argument("--timeout", type=float, default=3.0, help="per-command timeout in seconds")
    parser.add_argument("--json", action="store_true", help="print machine-readable JSON instead of Rich output")
    sub = parser.add_subparsers(dest="operation", required=True)
    flash = sub.add_parser("flash", help="flash a firmware image to the watch bootloader")
    flash.add_argument("target", nargs="?", help="image path, or 'release' to fetch a GitHub release")
    flash.add_argument("version", nargs="?", help="release tag after 'release' (or 'latest')")
    flash.add_argument("--factory", action="store_true", help="flash the factory image when fetching a release")
    flash.add_argument("--cache-dir", help="cache release images here")
    for alias in ("status", "battery", "ble"):
        item = sub.add_parser(alias, help=f"request {COMMANDS[alias]}")
        item.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    ota = sub.add_parser("ota", help="inspect or select OTA boot slots")
    ota.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    ota_actions = ota.add_subparsers(dest="ota_action")
    boot_other = ota_actions.add_parser("boot-other", help="boot from the other slot if it contains a valid image")
    boot_other.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    power = sub.add_parser("power", help="inspect or test CPU power settings")
    power.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    power_actions = power.add_subparsers(dest="power_action")
    power_log = power_actions.add_parser("log", help="record battery and power telemetry to CSV")
    power_log.add_argument("--interval", type=float, default=60.0, help="seconds between samples (default: %(default)s)")
    power_log.add_argument("--duration", type=float, default=3600.0, help="seconds to record; 0 runs until Ctrl-C")
    power_log.add_argument("--output", help="CSV path (defaults to a timestamped file in the current directory)")
    get_frequency = power_actions.add_parser("cpu-freq-get", help="show measured CPU frequency and override")
    get_frequency.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    set_frequency = power_actions.add_parser("cpu-freq-set", help="temporarily force CPU frequency (0 restores automatic scaling)")
    set_frequency.add_argument("mhz", choices=("0", "40", "80", "160"))
    set_frequency.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    power_actions.add_parser("get-dvfs-state", help="show ESP-IDF power locks and CPU sleep residency")
    config = sub.add_parser("config", help="read or provision saved watch settings over USB")
    config_actions = config.add_subparsers(dest="config_action", required=True)
    config_actions.add_parser("get", help="show saved settings (password values are never returned)")
    config_set = config_actions.add_parser("set", help="save one or more settings without using the hotspot")
    config_set.add_argument("--ssid")
    config_set.add_argument("--password")
    config_set.add_argument("--caldav-server")
    config_set.add_argument("--caldav-user")
    config_set.add_argument("--caldav-password")
    config_set.add_argument("--caldav-calendar")
    config_set.add_argument("--caldav-todo-path")
    config_set.add_argument("--timezone-offset-min", type=int)
    config_set.add_argument("--time-format", choices=("12h", "24h"))
    config_set.add_argument("--ap-ssid")
    config_set.add_argument("--ap-password")
    config_set.add_argument("--ap-timeout-sec", type=int)
    wifi = config_actions.add_parser("wifi", help="configure Wi-Fi credentials")
    wifi_actions = wifi.add_subparsers(dest="config_group_action", required=True)
    wifi_set = wifi_actions.add_parser("set", help="save Wi-Fi credentials")
    wifi_set.add_argument("--ssid")
    wifi_set.add_argument("--password")
    caldav = config_actions.add_parser("caldav", help="configure calendar and task sync")
    caldav_actions = caldav.add_subparsers(dest="config_group_action", required=True)
    caldav_set = caldav_actions.add_parser("set", help="save CalDAV account and collection settings")
    caldav_set.add_argument("--server", dest="caldav_server")
    caldav_set.add_argument("--user", dest="caldav_user")
    caldav_set.add_argument("--password", dest="caldav_password")
    caldav_set.add_argument("--calendar", dest="caldav_calendar")
    caldav_set.add_argument("--todo-path", dest="caldav_todo_path")
    clock_config = config_actions.add_parser("time", help="configure timezone and display format")
    clock_actions = clock_config.add_subparsers(dest="config_group_action", required=True)
    clock_set = clock_actions.add_parser("set", help="save timezone and display format")
    clock_set.add_argument("--timezone-offset-min", type=int)
    clock_set.add_argument("--time-format", choices=("12h", "24h"))
    hotspot = config_actions.add_parser("hotspot", help="configure the setup hotspot")
    hotspot_actions = hotspot.add_subparsers(dest="config_group_action", required=True)
    hotspot_set = hotspot_actions.add_parser("set", help="save hotspot name, password, and timeout")
    hotspot_set.add_argument("--ssid", dest="ap_ssid")
    hotspot_set.add_argument("--password", dest="ap_password")
    hotspot_set.add_argument("--timeout-sec", dest="ap_timeout_sec", type=int)
    clock = sub.add_parser("time", help="inspect or set the watch RTC over USB")
    clock_actions = clock.add_subparsers(dest="time_action", required=True)
    clock_actions.add_parser("status", help="show current epoch, RTC health, and chip drift")
    time_set = clock_actions.add_parser("set", help="set watch wall time using Unix epoch seconds")
    time_set.add_argument("epoch", type=int)
    logs = sub.add_parser("logs", help=f"request {COMMANDS['logs']}")
    logs.add_argument("--follow", action="store_true", help="stream live USB console output until Ctrl-C (does not use logs.read)")
    logs.add_argument("--interval", type=float, default=0.1, help="seconds between polls when caught up (default: %(default)s)")
    logs.add_argument("--batch-size", type=int, default=4, choices=range(1, 5), help="records requested per USB transaction (default: %(default)s)")
    logs.add_argument("--raw", action="store_true", help="capture the live USB console stream directly, without logs.read")
    logs.add_argument("--output", help="save raw console bytes to this file (also enables raw capture); otherwise write stdout")
    logs.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    raw = sub.add_parser("command", help="send a protocol command")
    raw.add_argument("name")
    raw.add_argument("--args", help="command arguments as a JSON object")
    raw.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    poll = sub.add_parser("poll", help="poll one or more status commands without reopening USB")
    poll.add_argument("commands", nargs="+", choices=tuple(COMMANDS))
    poll.add_argument("--interval", type=float, default=5.0, help="seconds between polls (minimum 0.5)")
    poll.add_argument("--count", type=int, default=0, help="number of polls; 0 runs until Ctrl-C")
    poll.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print machine-readable JSON")
    debug = sub.add_parser("debug", help="retrieve and decode firmware diagnostics")
    debug_actions = debug.add_subparsers(dest="debug_action", required=True)
    coredump = debug_actions.add_parser("coredump", help="read the saved flash coredump and decode tasks/backtraces")
    coredump.add_argument("--elf", help="matching firmware.elf (defaults to this checkout's build)")
    coredump.add_argument("--idf-path", help="ESP-IDF 4.4.7 source directory; auto-detected in this checkout")
    coredump.add_argument("--gdb", help="RISC-V GDB executable; auto-detected in this checkout")
    coredump.add_argument("--save-core", help="also save the extracted coredump ELF to this path")
    coredump.add_argument("--log-file", help="save decoder stdout/stderr (defaults to ewctl-coredump-<UTC timestamp>.log)")
    bundle = debug_actions.add_parser("bundle", help="create a support bundle with device status and recent logs")
    bundle.add_argument("--output", help="ZIP path (defaults to a timestamped bugreport file)")
    bundle.add_argument("--include-coredump", action="store_true", help="include a decoded dump and raw core (may contain private data)")
    bundle.add_argument("--elf", help="matching firmware.elf for the optional coredump")
    bundle.add_argument("--idf-path", help="ESP-IDF 4.4.7 source directory; auto-detected in this checkout")
    bundle.add_argument("--gdb", help="RISC-V GDB executable; auto-detected in this checkout")
    return parser


def print_json(value: Any) -> None:
    print(json.dumps(value, ensure_ascii=False, indent=2), flush=True)


def format_uptime(seconds: int) -> str:
    seconds = max(0, int(seconds))
    days, remainder = divmod(seconds, 86400)
    hours, remainder = divmod(remainder, 3600)
    minutes, secs = divmod(remainder, 60)
    if days:
        return f"{days}d {hours}h"
    if hours:
        return f"{hours}h {minutes:02d}m"
    if minutes:
        return f"{minutes}m {secs:02d}s"
    return f"{secs}s"


def display_reply(reply: dict, title: str, json_output: bool = False) -> None:
    if json_output:
        print_json(reply)
        return
    if not reply.get("ok"):
        error = reply.get("error", {})
        console.print(Panel(f"[bold red]{error.get('code', 'error')}[/]  {error.get('message', 'request failed')}", title=title, border_style="red"))
        return
    data = reply.get("data", {})
    if title == "power get-dvfs-state" and isinstance(data, dict):
        console.print(Panel(data.get("report", "No DVFS report returned"), title=title, border_style="blue"))
        return
    if isinstance(data, dict) and isinstance(data.get("records"), list):
        records = data["records"]
        if not records:
            console.print("[dim]No new log records.[/]")
        for record in records:
            if isinstance(record, dict):
                console.print(f"[dim]{record.get('sequence', '')}[/] {record.get('line', '')}", markup=False)
        return
    table = Table(box=box.SIMPLE, show_header=False, pad_edge=False)
    table.add_column("Field", style="cyan", no_wrap=True)
    table.add_column("Value", overflow="fold")
    if isinstance(data, dict):
        for key, value in data.items():
            if title == "status" and key == "uptime_seconds":
                key, value = "uptime", format_uptime(value)
            rendered = json.dumps(value, ensure_ascii=False) if isinstance(value, (dict, list)) else str(value)
            table.add_row(key.replace("_", " "), rendered)
    else:
        table.add_row("result", str(data))
    console.print(Panel(table, title=f"[bold]{title}[/]", border_style="blue", expand=False))


def request_data(session: Session, command: str) -> dict:
    reply = session.request(command)
    if not reply.get("ok"):
        error = reply.get("error", {})
        raise EwctlError(f"{command} failed: {error.get('message', 'device rejected request')}")
    data = reply.get("data")
    if not isinstance(data, dict):
        raise EwctlError(f"{command} returned malformed data")
    return data


def run_power_log(session: Session, args: argparse.Namespace) -> int:
    if args.interval < 1:
        raise EwctlError("power log --interval must be at least 1 second")
    if args.duration < 0:
        raise EwctlError("power log --duration cannot be negative")
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = Path(args.output or f"ewctl-power-{timestamp}.csv").expanduser()
    columns = [
        "timestamp_utc", "firmware_build", "uptime_seconds", "reset_reason", "active_app", "free_heap",
        "battery_available", "battery_mv", "battery_percent", "battery_sample_age_ms", "battery_connected",
        "charging", "cpu_mhz", "power_state", "power_locks_clear", "usb_blocks_sleep",
        "ble_connected", "advertising", "companion_source",
    ]
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        stream = output.open("x", newline="", encoding="utf-8")
    except FileExistsError as exc:
        raise EwctlError(f"refusing to overwrite {output}; choose another --output path") from exc
    except OSError as exc:
        raise EwctlError(f"cannot create {output}: {exc}") from exc

    deadline = time.monotonic() + args.duration if args.duration else None
    next_sample = time.monotonic()
    count = 0
    with stream:
        writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        stream.flush()
        print(f"ewctl: writing power samples to {output}", file=sys.stderr)
        while deadline is None or time.monotonic() <= deadline:
            system = request_data(session, "system.status")
            battery = request_data(session, "battery.read")
            power = request_data(session, "power.status")
            ble = request_data(session, "ble.status")
            row = {
                "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "firmware_build": system.get("build", ""),
                "uptime_seconds": system.get("uptime_seconds", ""),
                "reset_reason": system.get("reset_reason", ""),
                "active_app": system.get("active_app", ""),
                "free_heap": system.get("free_heap", ""),
                "battery_available": battery.get("available", ""),
                "battery_mv": battery.get("millivolts", ""),
                "battery_percent": battery.get("percent", ""),
                "battery_sample_age_ms": battery.get("sample_age_ms", ""),
                "battery_connected": battery.get("connected", ""),
                "charging": battery.get("charging", ""),
                "cpu_mhz": power.get("cpu_mhz", ""),
                "power_state": power.get("state", ""),
                "power_locks_clear": power.get("power_locks_clear", ""),
                "usb_blocks_sleep": power.get("usb_blocks_sleep", ""),
                "ble_connected": ble.get("ble_connected", ""),
                "advertising": ble.get("advertising", ""),
                "companion_source": ble.get("source", ""),
            }
            writer.writerow(row)
            stream.flush()
            count += 1
            if count == 1 and power.get("usb_blocks_sleep"):
                console.print("[yellow]USB is blocking the watch's normal sleep policy; this telemetry run is not a representative battery-life test.[/]")
            console.print(
                f"{row['timestamp_utc']}  {row['battery_mv']} mV ({row['battery_percent']}%)  "
                f"CPU {row['cpu_mhz']} MHz  BLE {'connected' if row['ble_connected'] else 'idle'}",
                markup=False,
            )
            if deadline is not None and time.monotonic() >= deadline:
                break
            next_sample += args.interval
            wake_at = min(next_sample, deadline) if deadline is not None else next_sample
            time.sleep(max(0.0, wake_at - time.monotonic()))
    console.print(f"Saved {count} samples to {output}")
    return 0


def prepare_coredump(args: argparse.Namespace, repo_root: str, port: str,
                     save_core: Optional[str] = None) -> tuple[list[str], dict[str, str], str]:
    idf_path = args.idf_path or os.environ.get("IDF_PATH") or os.path.join(
        repo_root, ".pio-core", "packages", "framework-espidf")
    coredump_tool = os.path.join(idf_path, "components", "espcoredump", "espcoredump.py")
    elf = args.elf or os.path.join(repo_root, ".pio", "build", "ErsaWearable", "firmware.elf")
    if not os.path.isfile(coredump_tool):
        raise EwctlError("ESP-IDF espcoredump.py not found; pass --idf-path to ESP-IDF 4.4.7")
    if not os.path.isfile(elf):
        raise EwctlError(f"firmware ELF not found: {elf}; pass --elf with the exact ELF used to build the watch firmware")
    missing = [name for name in ("serial", "construct", "pygdbmi")
               if importlib.util.find_spec(name) is None]
    if missing:
        raise EwctlError("coredump decoder dependencies are missing (" + ", ".join(missing) +
                         "); install them with: python3 -m pip install -r requirements-ewctl.txt")
    esptool_source = os.path.join(idf_path, "components", "esptool_py", "esptool")
    if not os.path.isdir(esptool_source):
        raise EwctlError(f"ESP-IDF esptool Python package not found under {idf_path}")
    command = [sys.executable, coredump_tool, "--chip", "esp32c3", "--port", port,
               "--baud", str(args.baud), "info_corefile", "--debug", "0"]
    toolchain = os.path.join(repo_root, ".pio-core", "packages", "toolchain-riscv32-esp", "bin")
    env = os.environ.copy()
    env["IDF_PATH"] = idf_path
    # ESP-IDF 4.4's decoder imports future.utils.with_metaclass only to
    # express ABC metaclasses. Keep that compatibility helper local so ewctl
    # does not depend on the unavailable Arch python-future package.
    compat_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ewctl_compat")
    if not os.path.isdir(compat_path):
        compat_path = "/usr/lib/ewctl/ewctl_compat"
    env["PYTHONPATH"] = compat_path + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    if os.path.isdir(toolchain):
        env["PATH"] = toolchain + os.pathsep + env.get("PATH", "")
        if not args.gdb:
            local_gdb = os.path.join(toolchain, "riscv32-esp-elf-gdb")
            if os.path.isfile(local_gdb):
                command.extend(["--gdb", local_gdb])
    if args.gdb:
        command.extend(["--gdb", args.gdb])
    if save_core:
        command.extend(["--save-core", save_core])
    command.append(elf)
    return command, env, elf


def github_get(url: str, limit: int = 32 * 1024 * 1024) -> bytes:
    request = Request(url, headers={"User-Agent": "ewctl", "Accept": "application/vnd.github+json"})
    try:
        with urlopen(request, timeout=30) as response:
            data = response.read(limit + 1)
    except (HTTPError, URLError, TimeoutError, OSError) as exc:
        raise EwctlError(f"GitHub download failed: {exc}") from exc
    if len(data) > limit:
        raise EwctlError("GitHub release asset exceeded the download size limit")
    return data


def download_release_image(version: str, factory: bool, cache_dir: Optional[str]) -> str:
    raw_version = version.strip()
    if raw_version.lower() != "latest" and not raw_version.startswith("ewp-"):
        raw_version = "ewp-" + raw_version
    if raw_version.lower() != "latest" and not re.fullmatch(r"[A-Za-z0-9._-]+", raw_version):
        raise EwctlError("release tag may contain only letters, digits, '.', '_' and '-'")
    api = "https://api.github.com/repos/ersascape/ErsaWearableOS/releases/"
    release_url = api + "latest" if raw_version.lower() == "latest" else "tags/" + quote(raw_version, safe="")
    release = json.loads(github_get(release_url, 2 * 1024 * 1024).decode("utf-8"))
    tag = release.get("tag_name", "")
    if not isinstance(tag, str) or not re.fullmatch(r"[A-Za-z0-9._-]+", tag):
        raise EwctlError("GitHub returned an invalid release tag")
    asset_name = "ewp-factory.bin" if factory else "firmware.bin"
    assets = {asset.get("name"): asset for asset in release.get("assets", []) if isinstance(asset, dict)}
    if asset_name not in assets or "SHA256SUMS" not in assets:
        raise EwctlError(f"release {tag} is missing {asset_name} or SHA256SUMS")
    checksums = github_get(assets["SHA256SUMS"].get("browser_download_url", ""), 65536).decode("ascii", "replace")
    expected = None
    for line in checksums.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1].lstrip("*") == asset_name:
            expected = parts[0].lower()
            break
    if not expected or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise EwctlError(f"release {tag} has no valid SHA-256 entry for {asset_name}")

    root = Path(cache_dir).expanduser() if cache_dir else Path.home() / ".cache" / "ewctl" / "releases"
    target_dir = root / tag
    target = target_dir / asset_name
    if target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest() == expected:
        return str(target)
    payload = github_get(assets[asset_name].get("browser_download_url", ""))
    if hashlib.sha256(payload).hexdigest() != expected:
        raise EwctlError(f"SHA-256 verification failed for {asset_name} from release {tag}")
    try:
        target_dir.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix=asset_name + ".", dir=target_dir)
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
        os.replace(temporary, target)
    except OSError as exc:
        raise EwctlError(f"could not save release image under {target_dir}: {exc}") from exc
    print(f"ewctl: downloaded {asset_name} from {tag}; SHA-256 verified", file=sys.stderr)
    return str(target)


def collect_bugreport(session: Session) -> tuple[dict[str, dict], list[dict]]:
    snapshot = {name: request_data(session, command) for name, command in (
        ("system", "system.status"), ("battery", "battery.read"),
        ("ble", "ble.status"), ("power", "power.status"),
    )}
    records: list[dict] = []
    cursor = 0
    while len(records) < 16:
        reply = session.request("logs.read", {"limit": min(4, 16 - len(records)), "cursor": cursor})
        if not reply.get("ok"):
            break
        data = reply.get("data", {})
        page = data.get("records", []) if isinstance(data, dict) else []
        if not isinstance(page, list) or not page:
            break
        records.extend(record for record in page if isinstance(record, dict))
        next_cursor = data.get("next_cursor", cursor) if isinstance(data, dict) else cursor
        if not isinstance(next_cursor, int) or next_cursor <= cursor or len(page) < 4:
            break
        cursor = next_cursor
    return snapshot, records[-16:]


def make_bugreport(args: argparse.Namespace) -> int:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    port = find_port(args.port)
    with Session(port, args.baud, args.timeout) as session:
        snapshot, records = collect_bugreport(session)
    records = [{**record, "line": redact_log_line(str(record.get("line", "")))} for record in records]
    timestamp = datetime.now(timezone.utc)
    output = Path(args.output or f"ewctl-bugreport-{timestamp.strftime('%Y%m%dT%H%M%SZ')}.zip").expanduser()
    report: dict[str, Any] = {
        "generated_at_utc": timestamp.isoformat(),
        "device": snapshot,
        "recent_logs": records,
    }
    coredump_text = None
    saved_core = None
    if args.include_coredump:
        with tempfile.TemporaryDirectory(prefix="ewctl-coredump-") as temporary_dir:
            saved_core = os.path.join(temporary_dir, "coredump.elf")
            try:
                command, env, elf = prepare_coredump(args, repo_root, port, saved_core)
                completed = subprocess.run(command, capture_output=True, text=True, errors="replace",
                                           check=False, env=env, timeout=180)
                coredump_text = completed.stdout + completed.stderr
                report["coredump"] = {"returncode": completed.returncode, "elf": elf}
            except (EwctlError, OSError, subprocess.TimeoutExpired) as exc:
                coredump_text = f"Could not decode coredump: {exc}\n"
                report["coredump"] = {"error": str(exc)}
            if os.path.isfile(saved_core):
                core_payload = Path(saved_core).read_bytes()
            else:
                core_payload = None
            write_bugreport(output, report, coredump_text, core_payload)
    else:
        write_bugreport(output, report, None, None)
    console.print(f"Support bundle saved to {output}")
    console.print("The bundle includes recent logs and device state; inspect it before sharing for personal data.")
    return 0


def write_bugreport(output: Path, report: dict, coredump_text: Optional[str], core_payload: Optional[bytes]) -> None:
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("bugreport.json", json.dumps(report, ensure_ascii=False, indent=2) + "\n")
            lines = ["Ersa Wearable bug report", f"Captured: {report['generated_at_utc']}", "",
                     "Device status:", json.dumps(report["device"], ensure_ascii=False, indent=2), "",
                     "Recent firmware logs:"]
            lines.extend(str(record.get("line", "")) for record in report["recent_logs"])
            archive.writestr("bugreport.txt", "\n".join(lines) + "\n")
            if coredump_text is not None:
                archive.writestr("coredump.txt", coredump_text or "Decoder produced no output.\n")
            if core_payload is not None:
                archive.writestr("coredump.elf", core_payload)
    except FileExistsError as exc:
        raise EwctlError(f"refusing to overwrite {output}; choose another --output path") from exc
    except OSError as exc:
            raise EwctlError(f"could not write support bundle {output}: {exc}") from exc


def redact_log_line(line: str) -> str:
    line = re.sub(r"(?i)(ssid\s*=\s*')[^']*(')", r"\1<redacted>\2", line)
    line = re.sub(r"(?i)((?:password|token|cookie|authorization)\s*[=:]\s*)\S+", r"\1<redacted>", line)
    line = re.sub(r"https?://[^\s'\"]+", "<url-redacted>", line)
    return line


def run(args: argparse.Namespace) -> int:
    if args.timeout <= 0:
        raise EwctlError("--timeout must be positive")
    if args.operation == "poll":
        if args.interval < 0.5:
            raise EwctlError("--interval must be at least 0.5 seconds")
        if args.count < 0:
            raise EwctlError("--count cannot be negative")
    if args.operation == "power" and args.power_action == "log":
        if args.interval < 1:
            raise EwctlError("power log --interval must be at least 1 second")
        if args.duration < 0:
            raise EwctlError("power log --duration cannot be negative")

    if args.operation == "flash":
        # Reuse the repository's established esptool wrapper: it selects the
        # correct app/factory offset and handles the pinned PlatformIO tool.
        repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        script = os.path.join(repo_root, "scripts", "flash_firmware.sh")
        if args.target == "release":
            if not args.version:
                raise EwctlError("use 'ewctl flash release <tag|latest>'")
            image = download_release_image(args.version, args.factory, args.cache_dir)
        elif args.target is None:
            if args.version or args.factory:
                raise EwctlError("--factory and a release version can only be used with 'flash release'")
            image = None
        else:
            if args.version:
                raise EwctlError("a second positional argument is only valid after 'flash release'")
            if args.factory:
                raise EwctlError("--factory is only valid with 'flash release'")
            image = args.target
        if os.path.isfile(script):
            command = [script]
            if image:
                command.append(image)
            flash_env = os.environ.copy()
            if args.port:
                flash_env["PORT"] = args.port
        else:
            if not image or not os.path.isfile(image):
                raise EwctlError("installed ewctl flash requires an existing firmware image path")
            if not args.port:
                args.port = find_port(None)
            image_name = os.path.basename(image)
            offset = "0x0" if "factory" in image_name.lower() else "0x10000"
            esptool = next((path for path in ("esptool", "esptool.py")
                            if shutil.which(path)), None)
            if not esptool:
                raise EwctlError("esptool is required; install the Arch 'esptool' package")
            command = [esptool, "--chip", "esp32c3", "--port", args.port,
                       "--baud", "460800", "write-flash", offset, image]
            flash_env = None
        print(f"ewctl: flashing {image or 'local build'}", file=sys.stderr)
        try:
            result = subprocess.run(command, check=False, env=flash_env)
        except OSError as exc:
            raise EwctlError(f"could not start firmware flasher: {exc}") from exc
        return result.returncode

    if args.operation == "debug":
        repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        if args.debug_action == "bundle":
            return make_bugreport(args)
        port = find_port(args.port)
        command, env, elf = prepare_coredump(args, repo_root, port, args.save_core)
        log_file = args.log_file or f"ewctl-coredump-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}.log"
        log_path = os.path.abspath(os.path.expanduser(log_file))
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        print(f"ewctl: reading flash coredump from {port} using {os.path.basename(elf)}", file=sys.stderr)
        print(f"ewctl: saving decoder output to {log_path}", file=sys.stderr)
        try:
            with open(log_path, "w", encoding="utf-8", errors="replace") as log:
                result = subprocess.run(command, check=False, env=env, stdout=log, stderr=subprocess.STDOUT)
        except OSError as exc:
            raise EwctlError(f"could not start ESP-IDF coredump decoder: {exc}") from exc
        if result.returncode:
            print(f"ewctl: coredump decoder failed (exit {result.returncode}); see {log_path}", file=sys.stderr)
        else:
            print(f"ewctl: coredump decoded successfully; see {log_path}", file=sys.stderr)
        return result.returncode

    port = find_port(args.port)
    if args.operation == "logs" and (args.raw or args.output or args.follow):
        return capture_raw_logs(port, args.baud, args.output)
    session = Session(port, args.baud, args.timeout)
    print(f"ewctl: connected to {port}", file=sys.stderr)
    try:
        if args.operation == "power" and args.power_action == "log":
            return run_power_log(session, args)
        if args.operation == "poll":
            iteration = 0
            while args.count == 0 or iteration < args.count:
                started = time.monotonic()
                for alias in args.commands:
                    reply = session.request(COMMANDS[alias])
                    if args.json:
                        print_json({"observed_at": datetime.now(timezone.utc).isoformat(), "command": alias, "reply": reply})
                    else:
                        display_reply(reply, f"{alias} · {datetime.now(timezone.utc).astimezone().strftime('%H:%M:%S')}")
                iteration += 1
                if args.count == 0 or iteration < args.count:
                    time.sleep(max(0.0, args.interval - (time.monotonic() - started)))
            return 0

        if args.operation == "logs" and args.follow:
            if args.interval < 0.05:
                raise EwctlError("--interval must be at least 0.05 seconds")
            cursor = None
            while True:
                request_args = {"limit": args.batch_size}
                if cursor is not None:
                    request_args["cursor"] = cursor
                reply = session.request(COMMANDS["logs"], request_args)
                display_reply(reply, "logs", args.json)
                data = reply.get("data")
                if isinstance(data, dict) and isinstance(data.get("next_cursor"), int):
                    cursor = data["next_cursor"]
                records = data.get("records", []) if isinstance(data, dict) else []
                if not isinstance(records, list) or len(records) < args.batch_size:
                    time.sleep(args.interval)

        if args.operation == "power":
            if args.power_action == "cpu-freq-get":
                command, command_args = "power.cpu-freq-get", None
            elif args.power_action == "cpu-freq-set":
                command, command_args = "power.cpu-freq-set", {"cpu_mhz": int(args.mhz)}
            elif args.power_action == "get-dvfs-state":
                command, command_args = "power.get-dvfs-state", None
            else:
                command, command_args = "power.status", None
        elif args.operation == "ota" and args.ota_action == "boot-other":
            command, command_args = "ota.boot-other", None
        elif args.operation == "config":
            command = "config.get" if args.config_action == "get" else "config.set"
            command_args = None
            if args.config_action == "set" or getattr(args, "config_group_action", None) == "set":
                field_map = {
                    "ssid": "ssid", "password": "password", "caldav_server": "caldav_server",
                    "caldav_user": "caldav_user", "caldav_password": "caldav_password",
                    "caldav_calendar": "caldav_calendar", "caldav_todo_path": "caldav_todo_path",
                    "timezone_offset_min": "timezone_offset_min", "ap_ssid": "ap_ssid",
                    "ap_password": "ap_password", "ap_timeout_sec": "ap_timeout_sec",
                }
                command_args = {key: getattr(args, key, None) for key in field_map if getattr(args, key, None) is not None}
                if getattr(args, "time_format", None) is not None:
                    command_args["military_time"] = args.time_format == "24h"
                if not command_args:
                    raise EwctlError("config set requires at least one setting option")
        elif args.operation == "time":
            command = "time.status" if args.time_action == "status" else "time.set"
            command_args = None if args.time_action == "status" else {"epoch": args.epoch}
        elif args.operation == "logs":
            command, command_args = COMMANDS["logs"], {"limit": args.batch_size}
        else:
            command = COMMANDS[args.operation] if args.operation in COMMANDS else args.name
            command_args = parse_args_json(getattr(args, "args", None))
        reply = session.request(command, command_args)
        if args.operation == "power" and args.power_action == "get-dvfs-state":
            title = "power get-dvfs-state"
        elif args.operation == "ota" and args.ota_action:
            title = f"ota {args.ota_action}"
        elif args.operation in ("config", "time"):
            if args.operation == "config":
                suffix = args.config_action
                if getattr(args, "config_group_action", None):
                    suffix += f" {args.config_group_action}"
                title = f"config {suffix}"
            else:
                title = f"time {args.time_action}"
        else:
            title = args.operation if args.operation != "command" else command
        display_reply(reply, title, args.json)
        return 0 if reply.get("ok") else 2
    finally:
        session.close()


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return run(args)
    except KeyboardInterrupt:
        print("ewctl: interrupted", file=sys.stderr)
        return 130
    except EwctlError as exc:
        print(f"ewctl: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
