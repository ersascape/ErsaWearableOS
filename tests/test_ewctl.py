import importlib.util
import hashlib
import io
import json
import pathlib
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from unittest.mock import patch


MODULE_PATH = pathlib.Path(__file__).parents[1] / "scripts" / "ewctl.py"
SPEC = importlib.util.spec_from_file_location("ewctl", MODULE_PATH)
ewctl = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ewctl)


class ProtocolTests(unittest.TestCase):
    def test_version_command_displays_release_and_master_revision(self):
        with patch.object(ewctl, "get_build_info", return_value={
            "version": "0.3.2", "git_hash": "a1b2c3", "branch": "master",
        }), redirect_stdout(io.StringIO()) as output:
            self.assertEqual(ewctl.main(["version"]), 0)
        self.assertEqual(output.getvalue().strip(), "ewctl 0.3.2+ga1b2c3 (master)")

    def test_uptime_formatting(self):
        self.assertEqual(ewctl.format_uptime(7), "7s")
        self.assertEqual(ewctl.format_uptime(125), "2m 05s")
        self.assertEqual(ewctl.format_uptime(3600 + 4 * 60), "1h 04m")
        self.assertEqual(ewctl.format_uptime(2 * 86400 + 3 * 3600), "2d 3h")

    def test_request_is_versioned_and_newline_terminated(self):
        frame = ewctl.encode_request(7, "battery.read", {"fresh": True})
        self.assertTrue(frame.endswith(b"\n"))
        self.assertEqual(
            ewctl.decode_response(frame),
            {"v": 1, "id": 7, "cmd": "battery.read", "args": {"fresh": True}},
        )

    def test_rejects_multiline_or_empty_command(self):
        for command in ("", "a\nb", "a\rb"):
            with self.subTest(command=command), self.assertRaises(ewctl.EwctlError):
                ewctl.encode_request(1, command)

    def test_rejects_oversized_request(self):
        with self.assertRaises(ewctl.EwctlError):
            ewctl.encode_request(1, "x", {"data": "z" * ewctl.MAX_FRAME_BYTES})

    def test_invalid_or_non_object_reply_is_ignored(self):
        self.assertIsNone(ewctl.decode_response(b"boot log text\n"))
        self.assertIsNone(ewctl.decode_response(b"[]\n"))
        self.assertIsNone(ewctl.decode_response(b"x" * (ewctl.MAX_FRAME_BYTES + 1)))

    def test_args_must_be_json_object(self):
        self.assertEqual(ewctl.parse_args_json('{"limit":10}'), {"limit": 10})
        for text in ("[1,2]", '"text"', "no-json"):
            with self.subTest(text=text), self.assertRaises(ewctl.EwctlError):
                ewctl.parse_args_json(text)

    def test_power_frequency_commands_have_cli_forms(self):
        parser = ewctl.build_parser()
        set_args = parser.parse_args(["power", "cpu-freq-set", "40"])
        self.assertEqual((set_args.operation, set_args.power_action, set_args.mhz), ("power", "cpu-freq-set", "40"))
        get_args = parser.parse_args(["power", "cpu-freq-get"])
        self.assertEqual((get_args.operation, get_args.power_action), ("power", "cpu-freq-get"))
        self.assertTrue(parser.parse_args(["--json", "power"]).json)
        self.assertTrue(parser.parse_args(["power", "cpu-freq-get", "--json"]).json)

    def test_ota_status_has_a_concise_cli_form(self):
        args = ewctl.build_parser().parse_args(["ota"])
        self.assertEqual(args.operation, "ota")
        self.assertEqual(ewctl.COMMANDS[args.operation], "ota.status")
        boot = ewctl.build_parser().parse_args(["ota", "boot-other"])
        self.assertEqual((boot.operation, boot.ota_action), ("ota", "boot-other"))

    def test_dvfs_state_has_a_power_cli_form(self):
        args = ewctl.build_parser().parse_args(["power", "get-dvfs-state"])
        self.assertEqual((args.operation, args.power_action), ("power", "get-dvfs-state"))

    def test_config_and_time_provisioning_arguments(self):
        config = ewctl.build_parser().parse_args([
            "config", "set", "--ssid", "Ersa", "--password", "wifi-secret",
            "--timezone-offset-min", "330", "--time-format", "24h",
        ])
        self.assertEqual((config.operation, config.config_action), ("config", "set"))
        self.assertEqual(config.timezone_offset_min, 330)
        clock = ewctl.build_parser().parse_args(["time", "set", "1791475200"])
        self.assertEqual((clock.operation, clock.time_action, clock.epoch), ("time", "set", 1791475200))

    def test_grouped_config_commands(self):
        parser = ewctl.build_parser()
        wifi = parser.parse_args(["config", "wifi", "set", "--ssid", "Ersa", "--password", "secret"])
        self.assertEqual((wifi.operation, wifi.config_action, wifi.config_group_action), ("config", "wifi", "set"))
        self.assertEqual((wifi.ssid, wifi.password), ("Ersa", "secret"))
        caldav = parser.parse_args(["config", "caldav", "set", "--server", "https://dav.example", "--todo-path", "tasks"])
        self.assertEqual(caldav.caldav_server, "https://dav.example")
        self.assertEqual(caldav.caldav_todo_path, "tasks")
        clock = parser.parse_args(["config", "time", "set", "--timezone-offset-min", "330", "--time-format", "24h"])
        self.assertEqual((clock.timezone_offset_min, clock.time_format), (330, "24h"))
        hotspot = parser.parse_args(["config", "hotspot", "set", "--ssid", "Ersa Setup", "--timeout-sec", "600"])
        self.assertEqual((hotspot.ap_ssid, hotspot.ap_timeout_sec), ("Ersa Setup", 600))

    def test_flash_command_accepts_an_image_or_local_default(self):
        parser = ewctl.build_parser()
        self.assertEqual(parser.parse_args(["flash", "firmware.bin"]).target, "firmware.bin")
        self.assertIsNone(parser.parse_args(["flash"]).target)

    def test_debug_coredump_accepts_matching_elf_and_idf_paths(self):
        parser = ewctl.build_parser()
        args = parser.parse_args([
            "--port", "/dev/ttyACM0", "debug", "coredump",
            "--elf", "firmware.elf", "--idf-path", "/opt/esp-idf", "--save-core", "panic.elf",
        ])
        self.assertEqual(args.debug_action, "coredump")
        self.assertEqual(args.elf, "firmware.elf")
        self.assertEqual(args.idf_path, "/opt/esp-idf")
        self.assertEqual(args.save_core, "panic.elf")

    def test_debug_coredump_accepts_log_file(self):
        args = ewctl.build_parser().parse_args([
            "debug", "coredump", "--log-file", "/tmp/coredump.log",
        ])
        self.assertEqual(args.log_file, "/tmp/coredump.log")

    def test_power_log_options_and_release_flash(self):
        parser = ewctl.build_parser()
        power = parser.parse_args(["power", "log", "--interval", "15", "--duration", "120", "--output", "run.csv"])
        self.assertEqual((power.power_action, power.interval, power.duration, power.output),
                         ("log", 15.0, 120.0, "run.csv"))
        release = parser.parse_args(["flash", "release", "0.1.2", "--factory", "--cache-dir", "/tmp/fw"])
        self.assertEqual((release.target, release.version, release.factory, release.cache_dir),
                         ("release", "0.1.2", True, "/tmp/fw"))

    def test_logs_raw_capture_options(self):
        parser = ewctl.build_parser()
        follow = parser.parse_args(["logs", "--follow"])
        self.assertTrue(follow.follow)
        self.assertFalse(follow.raw)
        raw = parser.parse_args(["logs", "--raw"])
        self.assertTrue(raw.raw)
        self.assertIsNone(raw.output)
        output = parser.parse_args(["logs", "--output", "watch.log"])
        self.assertEqual(output.output, "watch.log")

    def test_raw_log_capture_opens_usb_stream_read_only(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "watch.log"
            with patch.object(ewctl.os, "open", return_value=17) as open_device, \
                 patch.object(ewctl.select, "select", return_value=([17], [], [])), \
                 patch.object(ewctl.os, "read", side_effect=[b"boot log\n", KeyboardInterrupt]), \
                 patch.object(ewctl.os, "close"):
                self.assertEqual(ewctl.capture_raw_logs("/dev/test", 115200, str(output)), 0)
            flags = open_device.call_args.args[1]
            self.assertEqual(flags & (ewctl.os.O_WRONLY | ewctl.os.O_RDWR), 0)
            self.assertEqual(output.read_bytes(), b"boot log\n")

    def test_logs_follow_uses_raw_capture_without_bridge_requests(self):
        with patch.object(ewctl, "find_port", return_value="/dev/test"), \
             patch.object(ewctl, "Session", side_effect=AssertionError("control bridge selected")), \
             patch.object(ewctl, "capture_raw_logs", return_value=0) as capture:
            result = ewctl.main(["--port", "/dev/test", "logs", "--follow"])
        self.assertEqual(result, 0)
        capture.assert_called_once_with("/dev/test", ewctl.DEFAULT_BAUD, None)

    def test_debug_bundle_options_and_log_redaction(self):
        parser = ewctl.build_parser()
        bundle = parser.parse_args(["debug", "bundle", "--include-coredump", "--output", "report.zip"])
        self.assertTrue(bundle.include_coredump)
        self.assertEqual(bundle.output, "report.zip")
        line = "CONFIG ssid='private-wifi' token=secret https://user:pass@example.test/dav"
        safe = ewctl.redact_log_line(line)
        self.assertNotIn("private-wifi", safe)
        self.assertNotIn("secret", safe)
        self.assertNotIn("user:pass", safe)

    def test_release_download_verifies_manifest_before_caching(self):
        firmware = b"image contents"
        release = {
            "tag_name": "ewp-0.1.2",
            "assets": [
                {"name": "firmware.bin", "browser_download_url": "https://example.test/firmware"},
                {"name": "SHA256SUMS", "browser_download_url": "https://example.test/sums"},
            ],
        }
        manifest = f"{hashlib.sha256(firmware).hexdigest()}  firmware.bin\n".encode()
        with tempfile.TemporaryDirectory() as cache, patch.object(
            ewctl, "github_get", side_effect=[json.dumps(release).encode(), manifest, firmware]
        ):
            image = pathlib.Path(ewctl.download_release_image("0.1.2", False, cache))
            self.assertEqual(image.read_bytes(), firmware)

    def test_power_log_writes_csv_telemetry(self):
        class FakeSession:
            def request(self, command, _args=None):
                data = {
                    "system.status": {"build": "test", "uptime_seconds": 99, "reset_reason": "POWERON",
                                      "active_app": "watchface_clock", "free_heap": 12345},
                    "battery.read": {"available": True, "millivolts": 4000, "percent": 70,
                                     "sample_age_ms": 1000, "connected": True, "charging": False},
                    "power.status": {"cpu_mhz": 40, "state": "idle", "power_locks_clear": True,
                                     "usb_blocks_sleep": False},
                    "ble.status": {"ble_connected": True, "advertising": False, "source": "apple"},
                }[command]
                return {"ok": True, "data": data}

        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "power.csv"
            args = type("Args", (), {"interval": 1.0, "duration": 0.05, "output": str(output)})()
            self.assertEqual(ewctl.run_power_log(FakeSession(), args), 0)
            contents = output.read_text()
            self.assertIn("battery_mv", contents.splitlines()[0])
            self.assertIn(",4000,70,", contents)

    def test_bugreport_bundle_contains_snapshot_and_redacted_logs(self):
        class FakeSession:
            def __init__(self, *_args):
                pass

            def __enter__(self):
                return self

            def __exit__(self, *_args):
                pass

            def request(self, command, _args=None):
                if command == "logs.read":
                    return {"ok": True, "data": {"records": [
                        {"sequence": 1, "line": "NET: ssid='private' token=secret https://secret.test/"},
                    ], "next_cursor": 1}}
                names = {"system.status": "system", "battery.read": "battery",
                         "ble.status": "ble", "power.status": "power"}
                return {"ok": True, "data": {"available": True, "state": names[command]}}

        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "bugreport.zip"
            args = type("Args", (), {"port": "/dev/test", "baud": 115200, "timeout": 1.0,
                                     "output": str(output), "include_coredump": False})()
            with patch.object(ewctl, "Session", FakeSession):
                self.assertEqual(ewctl.make_bugreport(args), 0)
            with zipfile.ZipFile(output) as archive:
                report = json.loads(archive.read("bugreport.json"))
                text = archive.read("bugreport.txt").decode()
            self.assertEqual(report["device"]["battery"]["state"], "battery")
            self.assertNotIn("private", text)
            self.assertNotIn("secret", text)


if __name__ == "__main__":
    unittest.main()
