#!/usr/bin/env python3
"""Focused regression tests for the Arduino upload wrapper."""

import importlib.util
import io
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
UPLOAD_PATH = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "tools"
    / "upload.py"
)
PLATFORM_PATH = UPLOAD_PATH.parents[1] / "platform.txt"
BOARDS_PATH = PLATFORM_PATH.with_name("boards.txt")
PLATFORM_TOOLS = UPLOAD_PATH.parent
HOST_TOOLS = ROOT / "tools" / "board_manager" / "nrf54l15hosttools"
LEGACY_HOST_TOOLS = PLATFORM_TOOLS / "nrf54l15hosttools" / "1.1.3"
UF2_EMITTER = UPLOAD_PATH.parent / "uf2" / "uf2_emit.py"
UF2_CONV = UPLOAD_PATH.parent / "uf2" / "uf2conv.py"
SPEC = importlib.util.spec_from_file_location("nrf54_upload", UPLOAD_PATH)
UPLOAD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = UPLOAD
SPEC.loader.exec_module(UPLOAD)
UF2_SPEC = importlib.util.spec_from_file_location("nrf54_uf2conv", UF2_CONV)
UF2_CONV_MODULE = importlib.util.module_from_spec(UF2_SPEC)
assert UF2_SPEC.loader is not None
sys.modules[UF2_SPEC.name] = UF2_CONV_MODULE
UF2_SPEC.loader.exec_module(UF2_CONV_MODULE)


def result(returncode: int) -> subprocess.CompletedProcess:
    return subprocess.CompletedProcess(["pyocd", "erase"], returncode, "", "")


class NrfOcdTransportTests(unittest.TestCase):
    def test_bundled_binary_wins_over_shorter_old_cache_path(self):
        executable_name = "nrf_ocd.exe" if sys.platform.startswith("win") else "nrf_ocd"
        with tempfile.TemporaryDirectory(prefix="nrf54-ocd-selection-") as directory:
            root = Path(directory)
            script_dir = root / "current-release-with-a-deliberately-long-path" / "tools"
            script_dir.mkdir(parents=True)
            bundled = script_dir / executable_name
            bundled.touch()

            old_cache = root / "old"
            old_cache.mkdir()
            (old_cache / executable_name).touch()

            with (
                mock.patch.object(UPLOAD, "__file__", str(script_dir / "upload.py")),
                mock.patch.dict(
                    os.environ,
                    {"NRF54_NRF_OCD": "", "OPEN_NRF_OCD": ""},
                ),
            ):
                actual = UPLOAD.detect_nrf_ocd_command(
                    host_tools_path=old_cache,
                    allow_download=False,
                )

        self.assertEqual(actual, [str(bundled)])

    def test_environment_override_wins_over_bundled_binary(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-ocd-override-") as directory:
            script_dir = Path(directory)
            (script_dir / "nrf_ocd").touch()
            with (
                mock.patch.object(UPLOAD, "__file__", str(script_dir / "upload.py")),
                mock.patch.dict(
                    os.environ,
                    {"NRF54_NRF_OCD": "/custom/nrf_ocd --probe exact"},
                    clear=False,
                ),
            ):
                actual = UPLOAD.detect_nrf_ocd_command(allow_download=False)

        self.assertEqual(actual, ["/custom/nrf_ocd", "--probe", "exact"])

    def test_zero_exit_transport_error_requests_pyocd_fallback(self):
        fake_nrf_ocd = [
            sys.executable,
            "-c",
            (
                "print('08:10:55 ERROR cmsis_dap.c:54 bulk write failed "
                "for cmd 0x03: I/O error', flush=True)"
            ),
        ]

        with mock.patch.object(UPLOAD, "run", return_value=subprocess.CompletedProcess(
            ["nrf_ocd", "list"], 0, "  0   Test CMSIS-DAP    140EBF71    nrf54lm20a\n", ""
        )):
            actual = UPLOAD.upload_nrf_ocd(
                "fixture.hex", "nrf54lm20a", None, nrf_ocd_cmd=fake_nrf_ocd,
            )

        self.assertEqual(actual, UPLOAD.NRF_OCD_TRANSPORT_FALLBACK)
        self.assertNotEqual(actual, 0)

    def test_uid_selected_native_call_fails_before_any_native_command(self):
        with mock.patch.object(UPLOAD, "run") as run, mock.patch.object(UPLOAD.subprocess, "Popen") as popen:
            status = UPLOAD.upload_nrf_ocd("fixture.hex", "nrf54lm20a", "missing-uid", nrf_ocd_cmd=["nrf_ocd"])
        self.assertNotEqual(status, 0)
        run.assert_not_called()
        popen.assert_not_called()

    def test_native_autoselect_rejects_empty_ambiguous_or_failed_inventory(self):
        row = "  0   Test CMSIS-DAP    140EBF71    nrf54lm20a\n"
        for status, output in (
            (0, ""), (0, row + row), (1, row), (0, "unrecognized output"),
            (0, row + "  1   Other CMSIS-DAP    OTHER    nrf52840\n"),
            (0, "  0   invalid row\n"),
        ):
            with self.subTest(status=status, output=output), mock.patch.object(UPLOAD, "run", return_value=subprocess.CompletedProcess(["nrf_ocd", "list"], status, output, "")), mock.patch.object(UPLOAD.subprocess, "Popen") as popen:
                self.assertNotEqual(UPLOAD.upload_nrf_ocd("fixture.hex", "nrf54lm20a", None, nrf_ocd_cmd=["nrf_ocd"]), 0)
                popen.assert_not_called()


class ProbeIdentitySafetyTests(unittest.TestCase):
    def test_inferred_uid_is_never_dropped_when_probe_disappears(self):
        no_probe = subprocess.CompletedProcess(["pyocd", "load"], 1, "", "No connected debug probe matches unique ID")
        with mock.patch.object(UPLOAD, "flash_hex", return_value=no_probe) as flash, mock.patch.object(UPLOAD, "print_linux_probe_permission_hint"):
            status = UPLOAD.upload_pyocd(
                "fixture.hex", "nrf54lm20a", "absent-probe", 2, 0,
                allow_uid_fallback=True, pyocd_cmd=["pyocd"], probe_type="cmsisdap",
            )
        self.assertNotEqual(status, 0)
        self.assertEqual(flash.call_count, 2)
        for call in flash.call_args_list:
            self.assertEqual(call.args[2], "cmsisdap:absent-probe")

    def test_postupload_reset_uses_same_uid_and_never_native(self):
        for target in ("nrf54l", "nrf54lm20a"):
            with self.subTest(target=target), mock.patch.object(UPLOAD, "flash_hex", return_value=result(0)), mock.patch.object(UPLOAD, "run", return_value=result(0)) as run, mock.patch.object(UPLOAD, "detect_nrf_ocd_command") as native:
                status = UPLOAD.upload_pyocd("fixture.hex", target, "selected-probe", 1, 0, pyocd_cmd=["pyocd"], probe_type="cmsisdap")
                self.assertEqual(status, 0)
                native.assert_not_called()
                command = run.call_args.args[0]
                self.assertEqual(command[:2], ["pyocd", "reset"])
                self.assertIn("cmsisdap:selected-probe", command)
                self.assertIn("auto_unlock=false", command)
                self.assertEqual("--script" in command, target == "nrf54lm20a")

    def test_postupload_reset_failure_is_reported(self):
        with mock.patch.object(UPLOAD, "flash_hex", return_value=result(0)), mock.patch.object(UPLOAD, "run", return_value=result(9)):
            status = UPLOAD.upload_pyocd("fixture.hex", "nrf54l", "selected", 1, 0, pyocd_cmd=["pyocd"], probe_type="cmsisdap")
        self.assertEqual(status, 9)

    def test_safe_mode_still_skips_reset(self):
        with mock.patch.object(UPLOAD, "flash_hex", return_value=result(0)), mock.patch.object(UPLOAD, "run") as run:
            self.assertEqual(UPLOAD.upload_pyocd("fixture.hex", "nrf54l", "selected", 1, 0, pyocd_cmd=["pyocd"], safe_mode=True, probe_type="cmsisdap"), 0)
            run.assert_not_called()

    def test_selected_uid_routes_native_to_pyocd_and_disables_native_fallback(self):
        for explicit in (True, False):
            for requested_runner in ("nrf_ocd", "pyocd"):
                with self.subTest(explicit=explicit, runner=requested_runner):
                    argv = ["upload.py", "--hex", "fixture.hex", "--runner", requested_runner, "--probe-type", "cmsisdap"]
                    argv.extend(["--uid", "selected-probe"] if explicit else ["--port", "COM7"])
                    with mock.patch.object(UPLOAD.sys, "argv", argv), mock.patch.object(UPLOAD.os.path, "isfile", return_value=True), mock.patch.object(UPLOAD, "infer_uid_from_port", return_value="selected-probe"), mock.patch.object(UPLOAD, "preflight_linux_probe_access", return_value=False), mock.patch.object(UPLOAD, "detect_pyocd_command", return_value=["pyocd"]), mock.patch.object(UPLOAD, "upload_pyocd", return_value=6) as pyocd, mock.patch.object(UPLOAD, "upload_nrf_ocd") as native:
                        self.assertEqual(UPLOAD.main(), 6)
                        native.assert_not_called()
                        self.assertEqual(pyocd.call_args.args[2], "selected-probe")
                        self.assertFalse(pyocd.call_args.kwargs["allow_uid_fallback"])

    def test_unmapped_serial_port_cannot_fall_back_to_another_board(self):
        argv = ["upload.py", "--hex", "fixture.hex", "--runner", "nrf_ocd", "--port", "COM7"]
        with mock.patch.object(UPLOAD.sys, "argv", argv), mock.patch.object(UPLOAD, "infer_uid_from_port", return_value=None), mock.patch.object(UPLOAD, "upload_pyocd") as pyocd, mock.patch.object(UPLOAD, "upload_nrf_ocd") as native:
            self.assertNotEqual(UPLOAD.main(), 0)
            pyocd.assert_not_called()
            native.assert_not_called()

    def test_explicit_uf2_upload_never_falls_back_to_a_debug_probe(self):
        argv = ["upload.py", "--hex", "fixture.hex", "--uf2", "fixture.uf2", "--runner", "uf2"]
        for uf2_status in (0, 1, 6):
            with self.subTest(uf2_status=uf2_status), mock.patch.object(UPLOAD.sys, "argv", argv), mock.patch.object(UPLOAD.os.path, "isfile", return_value=True), mock.patch.object(UPLOAD, "infer_uid_from_port", return_value=None), mock.patch.object(UPLOAD, "upload_uf2", return_value=uf2_status) as uf2, mock.patch.object(UPLOAD, "upload_pyocd") as pyocd, mock.patch.object(UPLOAD, "upload_nrf_ocd") as native:
                self.assertEqual(UPLOAD.main(), uf2_status)
                uf2.assert_called_once()
                pyocd.assert_not_called()
                native.assert_not_called()


class MacOsPortIdentityTests(unittest.TestCase):
    CALLOUT = "/dev/cu.usbmodemD613F64E1"
    DIALIN = "/dev/tty.usbmodemD613F64E1"
    SELECTED_UID = "D613F64E"
    IOREG_COMMAND = ["/usr/sbin/ioreg", "-a", "-l", "-i", "-p", "IOService"]

    @classmethod
    def serial_node(cls, callout=None, dialin=None):
        return {
            "IOObjectClass": "IOSerialBSDClient",
            "IOCalloutDevice": callout or cls.CALLOUT,
            "IODialinDevice": dialin or cls.DIALIN,
        }

    @classmethod
    def usb_node(cls, serial=SELECTED_UID, child=None, usb_class="IOUSBHostDevice"):
        node = {
            "IOObjectClass": usb_class,
            "IORegistryEntryChildren": [{
                "IOObjectClass": "IOUSBHostInterface",
                "IORegistryEntryChildren": [child or cls.serial_node()],
            }],
        }
        if serial is not None:
            node["USB Serial Number"] = serial
        return node

    @classmethod
    def two_board_registry(cls):
        return {
            "IOObjectClass": "IOService",
            "IORegistryEntryChildren": [{
                "IOObjectClass": "IOUSBHostDevice",
                "USB Serial Number": "UPSTREAM-HUB-SERIAL",
                "IORegistryEntryChildren": [
                    cls.usb_node(),
                    cls.usb_node("OTHER-BOARD", cls.serial_node(
                        "/dev/cu.usbmodemOTHER1", "/dev/tty.usbmodemOTHER1")),
                ],
            }],
        }

    def infer_from_registry(self, registry, port=None):
        output = plistlib.dumps(registry).decode("utf-8")
        with (
            mock.patch.object(UPLOAD.sys, "platform", "darwin"),
            mock.patch.object(UPLOAD, "import_serial_list_ports", return_value=None) as serial_import,
            mock.patch.object(UPLOAD, "run", return_value=subprocess.CompletedProcess(
                self.IOREG_COMMAND, 0, output, "")) as run,
            mock.patch.object(UPLOAD, "tool_available", return_value=False),
        ):
            uid = UPLOAD.infer_uid_from_port(port or self.CALLOUT)
        serial_import.assert_called_once()
        run.assert_called_once_with(self.IOREG_COMMAND, timeout=10.0)
        return uid

    def test_cu_and_tty_aliases_resolve_the_selected_board_without_pyserial(self):
        registry = self.two_board_registry()
        for port in (self.CALLOUT, self.DIALIN, "  " + self.CALLOUT + "  "):
            with self.subTest(port=port):
                self.assertEqual(self.infer_from_registry(registry, port), self.SELECTED_UID)
        self.assertEqual(
            self.infer_from_registry(registry, "/dev/cu.usbmodemOTHER1"), "OTHER-BOARD")

    def test_legacy_usb_class_and_subclass_inheritance_are_usb_boundaries(self):
        legacy = self.usb_node(usb_class="IOUSBDevice")
        inherited = []
        for usb_class in ("IOUSBHostDevice", "IOUSBDevice"):
            node = self.usb_node(usb_class="VendorSpecificUsbDevice")
            node["IOObjectInheritance"] = [
                "IORegistryEntry", "IOService", usb_class, "VendorSpecificUsbDevice"]
            inherited.append(node)
        for node in (legacy, *inherited):
            with self.subTest(node=node):
                self.assertEqual(self.infer_from_registry([node]), self.SELECTED_UID)

    def test_interface_serial_does_not_replace_the_device_serial(self):
        registry = self.usb_node()
        interface = registry["IORegistryEntryChildren"][0]
        interface["USB Serial Number"] = "NOT-THE-DEVICE-SERIAL"
        self.assertEqual(self.infer_from_registry(registry), self.SELECTED_UID)

    def test_single_callout_or_dialin_property_can_identify_the_port(self):
        for property_name, port in (("IOCalloutDevice", self.CALLOUT),
                                    ("IODialinDevice", self.DIALIN)):
            with self.subTest(property_name=property_name):
                child = {"IOObjectClass": "IOSerialBSDClient", property_name: port}
                self.assertEqual(
                    self.infer_from_registry(self.usb_node(child=child), port), self.SELECTED_UID)

    def test_no_matching_live_registry_port_never_guesses_from_usbmodem_suffix(self):
        registry = self.two_board_registry()
        for port in (
            "/dev/cu.usbmodemD613F64E", "/dev/cu.usbmodemD613F64E10",
            "/dev/cu.usbmodemDISCONNECTED1", "/dev/tty.usbmodemOTHER2",
        ):
            with self.subTest(port=port):
                self.assertIsNone(self.infer_from_registry(registry, port))

    def test_missing_child_usb_serial_does_not_inherit_hub_identity(self):
        for serial in (None, "", " \t\n", 1234):
            for inherited_class in (False, True):
                with self.subTest(serial=serial, inherited_class=inherited_class):
                    child = self.usb_node(serial)
                    if inherited_class:
                        child["IOObjectClass"] = "VendorUsbDevice"
                        child["IOObjectInheritance"] = [
                            "IORegistryEntry", "IOService", "IOUSBHostDevice", "VendorUsbDevice"]
                    registry = self.usb_node("HUB-MUST-NOT-BECOME-TARGET", child)
                    self.assertIsNone(self.infer_from_registry(registry))

    def test_non_usb_ancestor_serial_is_not_a_probe_identity(self):
        registry = {
            "IOObjectClass": "IOService",
            "USB Serial Number": "NOT-A-USB-DEVICE",
            "IORegistryEntryChildren": [self.serial_node()],
        }
        self.assertIsNone(self.infer_from_registry(registry))

    def test_multiple_matching_registry_entries_are_rejected(self):
        for second_serial in (self.SELECTED_UID, "SECOND-MATCH", None):
            with self.subTest(second_serial=second_serial):
                self.assertIsNone(self.infer_from_registry([
                    self.usb_node(), self.usb_node(second_serial)]))

    def test_malformed_registry_or_ioreg_failure_returns_no_identity(self):
        for status, output in (
            (0, "not a plist"), (0, "<?xml version='1.0'?><plist><dict>"),
            (0, plistlib.dumps("not a registry").decode("utf-8")),
            (0, plistlib.dumps([]).decode("utf-8")),
            (1, plistlib.dumps(self.two_board_registry()).decode("utf-8")),
            (127, ""),
        ):
            with (
                self.subTest(status=status, output=output),
                mock.patch.object(UPLOAD.sys, "platform", "darwin"),
                mock.patch.object(UPLOAD, "import_serial_list_ports", return_value=None),
                mock.patch.object(UPLOAD, "run", return_value=subprocess.CompletedProcess(
                    self.IOREG_COMMAND, status, output, "ioreg failed")),
                mock.patch.object(UPLOAD, "tool_available", return_value=False),
            ):
                self.assertIsNone(UPLOAD.infer_uid_from_port(self.CALLOUT))
        with (
            mock.patch.object(UPLOAD.sys, "platform", "darwin"),
            mock.patch.object(UPLOAD, "import_serial_list_ports", return_value=None),
            mock.patch.object(UPLOAD, "run", side_effect=FileNotFoundError("ioreg unavailable")),
            mock.patch.object(UPLOAD, "tool_available", return_value=False),
        ):
            self.assertIsNone(UPLOAD.infer_uid_from_port(self.CALLOUT))

    def test_successful_pyserial_identity_skips_registry(self):
        info = mock.Mock(device=self.CALLOUT, serial_number=self.SELECTED_UID, hwid="")
        info.name = "usbmodemD613F64E1"
        list_ports = mock.Mock()
        list_ports.comports.return_value = [info]
        with (
            mock.patch.object(UPLOAD.sys, "platform", "darwin"),
            mock.patch.object(UPLOAD, "import_serial_list_ports", return_value=list_ports),
            mock.patch.object(UPLOAD, "port_uid_from_macos_registry") as registry,
            mock.patch.object(UPLOAD, "run") as run,
        ):
            self.assertEqual(UPLOAD.infer_uid_from_port(self.CALLOUT), self.SELECTED_UID)
        registry.assert_not_called()
        run.assert_not_called()

    def test_non_macos_resolution_never_invokes_ioreg(self):
        for platform in ("linux", "win32"):
            with (
                self.subTest(platform=platform),
                mock.patch.object(UPLOAD.sys, "platform", platform),
                mock.patch.object(UPLOAD, "port_uid_from_list_ports", return_value=None),
                mock.patch.object(UPLOAD, "port_uid_from_wmic", return_value=None),
                mock.patch.object(UPLOAD, "port_uid_from_macos_registry") as registry,
                mock.patch.object(UPLOAD, "tool_available", return_value=False),
                mock.patch.object(UPLOAD, "run") as run,
            ):
                self.assertIsNone(UPLOAD.infer_uid_from_port("/dev/nonexistent-issue119-port"))
                registry.assert_not_called()
                run.assert_not_called()

    def test_empty_uid_recipe_argument_still_pins_upload_to_registry_identity(self):
        output = plistlib.dumps(self.two_board_registry()).decode("utf-8")
        for requested_runner, uid_arguments in (
            (runner, uid) for runner in ("pyocd", "nrf_ocd")
            for uid in (["--uid"], ["--uid", ""])
        ):
            argv = ["upload.py", "--hex", "fixture.hex", "--port", self.CALLOUT,
                    "--target", "nrf54lm20a", "--runner", requested_runner,
                    *uid_arguments, "--probe-type", "cmsisdap"]
            with (
                self.subTest(runner=requested_runner, uid_arguments=uid_arguments),
                mock.patch.object(UPLOAD.sys, "argv", argv),
                mock.patch.object(UPLOAD.sys, "platform", "darwin"),
                mock.patch.object(UPLOAD, "import_serial_list_ports", return_value=None),
                mock.patch.object(UPLOAD, "run", return_value=subprocess.CompletedProcess(
                    self.IOREG_COMMAND, 0, output, "")),
                mock.patch.object(UPLOAD, "tool_available", return_value=False),
                mock.patch.object(UPLOAD.os.path, "isfile", return_value=True),
                mock.patch.object(UPLOAD, "choose_runner", return_value=requested_runner),
                mock.patch.object(UPLOAD, "preflight_linux_probe_access", return_value=False),
                mock.patch.object(UPLOAD, "detect_pyocd_command", return_value=["pyocd"]),
                mock.patch.object(UPLOAD, "upload_pyocd", return_value=6) as pyocd,
                mock.patch.object(UPLOAD, "upload_nrf_ocd") as native,
            ):
                self.assertEqual(UPLOAD.main(), 6)
                pyocd.assert_called_once()
                self.assertEqual(pyocd.call_args.args[1], "nrf54lm20a")
                self.assertEqual(pyocd.call_args.args[2], self.SELECTED_UID)
                self.assertEqual(pyocd.call_args.kwargs["port"], self.CALLOUT)
                self.assertFalse(pyocd.call_args.kwargs["allow_uid_fallback"])
                native.assert_not_called()

    def test_unresolved_macos_recipe_refuses_all_flash_transports(self):
        argv = ["upload.py", "--hex", "fixture.hex", "--port", self.CALLOUT,
                "--runner", "nrf_ocd", "--uid", "--probe-type", "cmsisdap"]
        output = plistlib.dumps(self.usb_node(None)).decode("utf-8")
        with (
            mock.patch.object(UPLOAD.sys, "argv", argv),
            mock.patch.object(UPLOAD.sys, "platform", "darwin"),
            mock.patch.object(UPLOAD, "import_serial_list_ports", return_value=None),
            mock.patch.object(UPLOAD, "run", return_value=subprocess.CompletedProcess(
                self.IOREG_COMMAND, 0, output, "")),
            mock.patch.object(UPLOAD, "tool_available", return_value=False),
            mock.patch.object(UPLOAD, "upload_pyocd") as pyocd,
            mock.patch.object(UPLOAD, "upload_nrf_ocd") as native,
            mock.patch.object(UPLOAD, "upload_uf2") as uf2,
        ):
            self.assertEqual(UPLOAD.main(), 8)
        pyocd.assert_not_called()
        native.assert_not_called()
        uf2.assert_not_called()


class JLinkUploadTests(unittest.TestCase):
    def test_jlink_uid_is_plugin_bound_and_leading_zeroes_are_removed(self):
        self.assertEqual(
            UPLOAD.qualify_pyocd_probe_uid("000960177309", "jlink"),
            "jlink:960177309",
        )
        self.assertEqual(
            UPLOAD.qualify_pyocd_probe_uid(None, "jlink"),
            "jlink:",
        )

    def test_conflicting_probe_prefix_is_rejected(self):
        with self.assertRaises(ValueError):
            UPLOAD.qualify_pyocd_probe_uid("cmsisdap:probe", "jlink")

    @mock.patch.object(UPLOAD, "run")
    def test_discovery_uses_only_segger_jlink_inventory_entries(self, run):
        run.return_value = subprocess.CompletedProcess(
            ["pyocd", "json", "--probes"],
            0,
            (
                '{"status":0,"boards":['
                '{"vendor_name":"SEGGER","product_name":"J-Link Plus",'
                '"unique_id":"960177309"},'
                '{"vendor_name":"Arm","product_name":"DAPLink CMSIS-DAP",'
                '"unique_id":"cmsis"}]}'
            ),
            "",
        )

        self.assertEqual(
            UPLOAD.discover_jlink_probe_uids(["pyocd"]), ["960177309"]
        )

    @mock.patch.object(UPLOAD, "discover_jlink_probe_uids")
    def test_multiple_jlinks_fail_without_interactive_selection(self, discover):
        discover.return_value = ["111", "222"]
        stderr = io.StringIO()
        with mock.patch.object(UPLOAD.sys, "stderr", stderr):
            selected, ambiguous = UPLOAD.select_jlink_probe_uid(["pyocd"])

        self.assertIsNone(selected)
        self.assertTrue(ambiguous)
        self.assertIn("NRF54L15_JLINK_UID", stderr.getvalue())

    @mock.patch.object(UPLOAD, "run")
    def test_jlink_load_disables_power_control_and_resets_in_pyocd(self, run):
        run.return_value = subprocess.CompletedProcess(["pyocd", "load"], 0, "", "")

        completed = UPLOAD.flash_hex(
            ["pyocd"],
            "nrf54l",
            "jlink:960177309",
            "fixture.hex",
            probe_type="jlink",
        )

        self.assertEqual(completed.returncode, 0)
        command = run.call_args.args[0]
        self.assertIn("jlink:960177309", command)
        self.assertIn("jlink.power=false", command)
        self.assertIn("jlink.non_interactive=true", command)
        self.assertNotIn("--no-reset", command)

    @mock.patch.object(UPLOAD, "run")
    def test_cmsis_dap_load_keeps_existing_no_reset_path(self, run):
        run.return_value = subprocess.CompletedProcess(["pyocd", "load"], 0, "", "")

        UPLOAD.flash_hex(
            ["pyocd"],
            "nrf54l",
            "cmsisdap:E91217E8",
            "fixture.hex",
            probe_type="cmsisdap",
        )

        command = run.call_args.args[0]
        self.assertIn("--no-reset", command)
        self.assertNotIn("jlink.power=false", command)

    @mock.patch.object(UPLOAD, "detect_nrf_ocd_command")
    @mock.patch.object(UPLOAD, "flash_hex")
    def test_successful_jlink_upload_never_calls_nrf_ocd(self, flash, detect_nrf_ocd):
        flash.return_value = subprocess.CompletedProcess(["pyocd", "load"], 0, "", "")

        completed = UPLOAD.upload_pyocd(
            "fixture.hex",
            "nrf54l",
            "000960177309",
            retries=1,
            retry_delay=0,
            pyocd_cmd=["pyocd"],
            safe_mode=True,
            probe_type="jlink",
        )

        self.assertEqual(completed, 0)
        self.assertEqual(flash.call_args.args[2], "jlink:960177309")
        self.assertFalse(flash.call_args.kwargs["safe_mode"])
        self.assertEqual(flash.call_args.kwargs["probe_type"], "jlink")
        detect_nrf_ocd.assert_not_called()

    @mock.patch.object(UPLOAD, "flash_hex")
    @mock.patch.object(UPLOAD, "select_jlink_probe_uid", return_value=(None, False))
    def test_missing_auto_selected_jlink_fails_before_flash(self, _select, flash):
        stderr = io.StringIO()
        with mock.patch.object(UPLOAD.sys, "stderr", stderr):
            completed = UPLOAD.upload_pyocd(
                "fixture.hex",
                "nrf54l",
                None,
                retries=1,
                retry_delay=0,
                pyocd_cmd=["pyocd"],
                probe_type="jlink",
            )

        self.assertEqual(completed, 6)
        flash.assert_not_called()
        self.assertIn("No J-Link probe", stderr.getvalue())

    def test_missing_jlink_hint_requires_official_driver_and_hardware_test(self):
        failed = subprocess.CompletedProcess(
            ["pyocd", "load"], 1, "", "No connected debug probe matches unique ID 'jlink:'"
        )
        stderr = io.StringIO()
        with mock.patch.object(UPLOAD.sys, "stderr", stderr):
            UPLOAD.print_jlink_probe_hint(failed)

        details = stderr.getvalue()
        self.assertIn("official SEGGER J-Link Software", details)
        self.assertIn("pyocd list --probes", details)
        self.assertIn("not yet been validated on maintainer hardware", details)


class ProtectedTargetRecoveryTests(unittest.TestCase):
    @mock.patch.object(UPLOAD, "time")
    @mock.patch.object(UPLOAD, "recover_target")
    def test_retries_after_first_erase_failure(self, recover, _time):
        recover.side_effect = [result(1), result(0)]

        actual = UPLOAD.recover_target_with_retries(
            ["pyocd"], "nrf54l", "probe", retry_delay=0
        )

        self.assertEqual(actual.returncode, 0)
        self.assertEqual(recover.call_count, 2)
        self.assertIsNone(recover.call_args_list[1].kwargs["connect_mode"])

    @mock.patch.object(UPLOAD, "recover_target")
    def test_stops_after_first_success(self, recover):
        recover.return_value = result(0)

        actual = UPLOAD.recover_target_with_retries(
            ["pyocd"], "nrf54l", "probe", attempts=3
        )

        self.assertEqual(actual.returncode, 0)
        recover.assert_called_once()


class LinuxProbePermissionTests(unittest.TestCase):
    def test_lm20a_pyocd_uses_external_registration_without_mutating_pyocd(self):
        source = UPLOAD_PATH.read_text(encoding="utf-8")
        self.assertFalse((PLATFORM_TOOLS / "patch_lm20b_target.py").exists())
        self.assertNotIn("patch_lm20b_target", source)
        command = UPLOAD.append_pyocd_target_script(["pyocd", "load"], "nrf54lm20a")
        self.assertEqual(
            command,
            [
                "pyocd",
                "load",
                "--script",
                str(PLATFORM_TOOLS / "pyocd_register_lm20b.py"),
            ],
        )

    def test_every_shipped_udev_rule_covers_both_xiao_probe_ids(self):
        for rule in (
            HOST_TOOLS / "setup/60-seeed-xiao-nrf54-cmsis-dap.rules",
            PLATFORM_TOOLS / "setup/60-seeed-xiao-nrf54-cmsis-dap.rules",
            LEGACY_HOST_TOOLS / "setup/60-seeed-xiao-nrf54-cmsis-dap.rules",
        ):
            source = rule.read_text(encoding="utf-8")
            with self.subTest(rule=rule):
                for product in ("0066", "0068"):
                    self.assertEqual(source.count(f'idProduct}}=="{product}"'), 3)

    def test_every_linux_installer_retriggers_both_xiao_probe_ids(self):
        for installer in (
            HOST_TOOLS / "setup/install_linux_host_deps.sh",
            PLATFORM_TOOLS / "setup/install_linux_host_deps.sh",
            LEGACY_HOST_TOOLS / "setup/install_linux_host_deps.sh",
        ):
            source = installer.read_text(encoding="utf-8")
            with self.subTest(installer=installer):
                for product in ("0066", "0068"):
                    trigger = (
                        "trigger --attr-match=idVendor=2886 "
                        f"--attr-match=idProduct={product}"
                    )
                    self.assertGreaterEqual(source.count(trigger), 2)

    def test_canonical_udev_installer_executes_both_triggers(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-udev-test-") as directory:
            root = Path(directory)
            rule_dir = root / "rules.d"
            rule_dir.mkdir()
            log = root / "udevadm.log"
            fake_udevadm = root / "udevadm"
            fake_udevadm.write_text(
                "#!/usr/bin/env bash\n"
                'printf "%s\\n" "$*" >>"${UDEVADM_TEST_LOG}"\n',
                encoding="utf-8",
            )
            fake_udevadm.chmod(0o755)
            destination = rule_dir / "60-seeed-xiao-nrf54-cmsis-dap.rules"
            environment = os.environ.copy()
            environment.update(
                {
                    "RULE_DST": str(destination),
                    "UDEVADM_BIN": str(fake_udevadm),
                    "UDEVADM_TEST_LOG": str(log),
                }
            )

            completed = subprocess.run(
                [str(HOST_TOOLS / "setup/install_linux_host_deps.sh"), "--udev"],
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            commands = log.read_text(encoding="utf-8")
            self.assertIn("idProduct=0066", commands)
            self.assertIn("idProduct=0068", commands)
            self.assertEqual(destination.read_bytes(), (
                HOST_TOOLS / "setup/60-seeed-xiao-nrf54-cmsis-dap.rules"
            ).read_bytes())

    def test_permission_hint_reports_the_connected_lm20a_product_id(self):
        denied = subprocess.CompletedProcess(
            ["pyocd", "list"], 1, "", "No connected debug probes"
        )
        stderr = io.StringIO()
        with (
            mock.patch.object(UPLOAD.sys, "platform", "linux"),
            mock.patch.object(UPLOAD, "tool_available", return_value=True),
            mock.patch.object(
                UPLOAD,
                "run",
                return_value=subprocess.CompletedProcess(
                    ["lsusb"], 0, "Bus 001 Device 002: ID 2886:0068 Seeed\n", ""
                ),
            ),
            mock.patch.object(
                UPLOAD, "matching_probe_hidraw_nodes", return_value=[Path("/dev/hidraw0")]
            ),
            mock.patch.object(UPLOAD, "probe_hidraw_nodes_accessible", return_value=False),
            mock.patch.object(UPLOAD.sys, "stderr", stderr),
        ):
            UPLOAD.print_linux_probe_permission_hint(denied)

        self.assertIn("2886:0068", stderr.getvalue())
        self.assertNotIn("2886:0066 is present", stderr.getvalue())

    def test_preflight_reports_the_product_id_from_hidraw_sysfs(self):
        node = Path("/dev/hidraw0")
        stderr = io.StringIO()
        with (
            mock.patch.object(UPLOAD.sys, "platform", "linux"),
            mock.patch.object(UPLOAD, "matching_probe_hidraw_nodes", return_value=[node]),
            mock.patch.object(UPLOAD, "probe_hidraw_nodes_accessible", return_value=False),
            mock.patch.object(
                UPLOAD,
                "_sysfs_usb_identity_for_hidraw",
                return_value=("2886", "0068"),
            ),
            mock.patch.object(UPLOAD.sys, "stderr", stderr),
        ):
            blocked = UPLOAD.preflight_linux_probe_access(None)

        self.assertTrue(blocked)
        self.assertIn("2886:0068", stderr.getvalue())


class PlatformRecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.properties = {}
        for raw_line in PLATFORM_PATH.read_text(encoding="utf-8").splitlines():
            key, separator, value = raw_line.partition("=")
            if separator:
                cls.properties[key] = value
        cls.board_properties = {}
        for raw_line in BOARDS_PATH.read_text(encoding="utf-8").splitlines():
            key, separator, value = raw_line.partition("=")
            if separator:
                cls.board_properties[key] = value
        cls.board_ids = sorted(
            key.removesuffix(".name")
            for key in cls.board_properties
            if key.endswith(".name")
        )

    def test_legacy_ide_verbosity_properties_are_defined_as_no_ops(self):
        property_names = (
            "tools.nrf54ocd.upload.params.verbose",
            "tools.nrf54ocd.upload.params.quiet",
            "tools.nrf54upload.upload.params.verbose",
            "tools.nrf54upload.upload.params.quiet",
            "tools.nrf54program.program.params.verbose",
            "tools.nrf54program.program.params.quiet",
            "tools.nrf54jlinkprogram.program.params.verbose",
            "tools.nrf54jlinkprogram.program.params.quiet",
        )
        for property_name in property_names:
            with self.subTest(property=property_name):
                self.assertIn(property_name, self.properties)
                self.assertEqual("", self.properties[property_name])

    def test_python_recipes_do_not_depend_on_legacy_unresolved_tool_properties(self):
        recipe_keys = (
            "recipe.hooks.prebuild.1.pattern",
            "recipe.objcopy.uf2.pattern",
            "tools.nrf54ocd.upload.pattern",
            "tools.nrf54ocd.upload.pattern.macosx",
            "tools.nrf54upload.upload.pattern",
            "tools.nrf54program.program.pattern",
            "tools.nrf54jlinkprogram.program.pattern",
        )
        for recipe_key in recipe_keys:
            with self.subTest(recipe=recipe_key):
                self.assertNotIn("{tools.python3", self.properties[recipe_key])
                self.assertIn("python3", self.properties[recipe_key])

    def test_python_recipes_use_runtime_platform_paths_for_arduino_ide_1(self):
        recipe_keys = (
            "recipe.objcopy.uf2.pattern",
            "recipe.objcopy.uf2.pattern.windows",
            "tools.nrf54ocd.upload.pattern",
            "tools.nrf54ocd.upload.pattern.macosx",
            "tools.nrf54upload.upload.pattern",
            "tools.nrf54program.program.pattern",
            "tools.nrf54jlinkprogram.program.pattern",
        )
        for recipe_key in recipe_keys:
            with self.subTest(recipe=recipe_key):
                pattern = self.properties[recipe_key]
                self.assertNotIn("{tools.", pattern)
                self.assertIn("{runtime.platform.path}/tools/", pattern)

        self.assertEqual(
            self.properties["tools.nrf54upload.cmd.path"],
            "{runtime.platform.path}/tools/upload.py",
        )
        self.assertEqual(
            self.properties["tools.nrf54program.cmd.path"],
            "{runtime.platform.path}/tools/upload.py",
        )
        self.assertEqual(
            self.properties["tools.nrf54jlinkprogram.cmd.path"],
            "{runtime.platform.path}/tools/upload.py",
        )

    def test_upload_recipe_properties_are_board_scoped_for_arduino_ide_1(self):
        recipe_keys = (
            "tools.nrf54ocd.upload.pattern",
            "tools.nrf54ocd.upload.pattern.macosx",
            "tools.nrf54upload.upload.pattern",
            "tools.nrf54program.program.pattern",
            "tools.nrf54jlinkprogram.program.pattern",
        )
        upload_properties = set()
        for recipe_key in recipe_keys:
            upload_properties.update(
                re.findall(r"\{upload\.([^}]+)\}", self.properties[recipe_key])
            )

        self.assertEqual(
            {
                "runner",
                "target",
                "uid",
                "openocd_script",
                "openocd_speed",
                "uf2_drive",
                "uf2_labels",
                "uf2_timeout",
                "pyocd_safe",
                "probe_type",
            },
            upload_properties,
        )
        self.assertGreater(len(self.board_ids), 0)
        for board_id in self.board_ids:
            with self.subTest(board=board_id):
                for upload_property in upload_properties:
                    self.assertIn(
                        f"{board_id}.upload.{upload_property}",
                        self.board_properties,
                    )
                self.assertEqual(
                    self.board_properties[f"{board_id}.upload.uf2_timeout"],
                    self.properties["upload.uf2_timeout"],
                )
                self.assertEqual(
                    self.board_properties[f"{board_id}.upload.uf2_labels"],
                    self.properties["upload.uf2_labels"],
                )

    def test_every_board_exposes_fail_closed_experimental_jlink_upload(self):
        for board_id in self.board_ids:
            prefix = f"{board_id}.menu.clean_upload.jlink"
            with self.subTest(board=board_id):
                self.assertEqual(
                    self.board_properties[prefix],
                    "J-Link via pyOCD (Experimental)",
                )
                self.assertEqual(
                    self.board_properties[f"{prefix}.upload.runner"], "pyocd"
                )
                self.assertEqual(
                    self.board_properties[f"{prefix}.upload.tool.default"],
                    "nrf54upload",
                )
                self.assertEqual(
                    self.board_properties[f"{prefix}.upload.protocol"], "default"
                )
                self.assertEqual(
                    self.board_properties[f"{prefix}.upload.probe_type"], "jlink"
                )
                self.assertEqual(
                    self.board_properties[f"{prefix}.upload.pyocd_safe"], "false"
                )
                self.assertEqual(
                    self.board_properties[f"{board_id}.upload.probe_type"],
                    "cmsisdap",
                )

    def test_windows_native_upload_routes_through_recovery_wrapper(self):
        pattern = self.properties["tools.nrf54ocd.upload.pattern.windows"]
        self.assertIn("powershell", pattern.lower())
        self.assertIn("upload_windows.ps1", pattern)
        self.assertIn('-HexPath "{build.path}/{build.project_name}.hex"', pattern)
        # Every shipped board leaves upload.uid empty. Passing it through the
        # native command line renders `-Uid ""`, which powershell.exe treats as
        # a missing parameter argument before upload_windows.ps1 can run.
        self.assertNotIn("-Uid", pattern)
        self.assertNotIn("{upload.uid}", pattern)
        self.assertNotIn("{tools.python3", pattern)
        self.assertNotIn("upload.py", pattern)

    def test_windows_native_upload_stops_after_the_resetting_load(self):
        source = (PLATFORM_TOOLS / "upload_windows.ps1").read_text(encoding="utf-8")
        self.assertNotIn("0xE000EDF0", source)
        self.assertNotIn("$detachArgs", source)
        self.assertNotIn("$detachExitCode", source)

    def test_uf2_recipe_runs_the_real_emitter(self):
        pattern = self.properties["recipe.objcopy.uf2.pattern"]
        self.assertIn("{runtime.platform.path}/tools/uf2/uf2_emit.py", pattern)
        self.assertIn('--input "{build.path}/{build.project_name}.hex"', pattern)
        self.assertIn('--output "{build.path}/{build.project_name}.uf2"', pattern)

    def test_windows_uf2_recipe_is_native_and_uses_binary_input(self):
        pattern = self.properties["recipe.objcopy.uf2.pattern.windows"]
        self.assertIn("powershell", pattern.lower())
        self.assertIn("uf2_emit.ps1", pattern)
        self.assertIn('-InputPath "{build.path}/{build.project_name}.bin"', pattern)
        self.assertNotIn("{tools.python3", pattern)

    def test_windows_uf2_emitter_uses_unsigned_constants(self):
        script = (UF2_EMITTER.parent / "uf2_emit.ps1").read_text(encoding="utf-8")
        self.assertIn('$uf2MagicStart1 = Convert-ToUInt32 "0x9E5D5157"', script)
        self.assertNotIn("Set-UInt32LE $block 4 0x9E5D5157", script)
        self.assertNotIn("Set-UInt32LE $block 28 0xADA54B15", script)

    def test_programmer_recipe_selects_pyocd(self):
        pattern = self.properties["tools.nrf54program.program.pattern"]
        self.assertIn('--runner "pyocd"', pattern)
        self.assertNotIn("{programmer.", pattern)

        jlink_pattern = self.properties["tools.nrf54jlinkprogram.program.pattern"]
        self.assertIn('--runner "pyocd"', jlink_pattern)
        self.assertIn('--probe-type "jlink"', jlink_pattern)
        self.assertNotIn("--port", jlink_pattern)
        self.assertNotIn("{programmer.", jlink_pattern)

    @unittest.skipUnless(
        shutil.which("arduino-cli") and Path("/bin/echo").is_file(),
        "Arduino CLI expansion test requires arduino-cli and /bin/echo",
    )
    def test_arduino_cli_expands_programmer_recipe(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-programmer-test-") as directory:
            root = Path(directory)
            user = root / "user"
            data = root / "data"
            downloads = root / "downloads"
            fake_bin = root / "bin"
            hardware = user / "hardware" / "localnrf54"
            hardware.mkdir(parents=True)
            data.mkdir()
            downloads.mkdir()
            fake_bin.mkdir()
            fake_python = fake_bin / "python3"
            fake_python.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\"\n", encoding="utf-8")
            fake_python.chmod(0o755)
            (hardware / "nrf54l15clean").symlink_to(
                UPLOAD_PATH.parents[1], target_is_directory=True
            )
            config = root / "arduino-cli.yaml"
            config.write_text(
                "directories:\n"
                f"  data: {data}\n"
                f"  downloads: {downloads}\n"
                f"  user: {user}\n",
                encoding="utf-8",
            )
            image = root / "RecipeProbe.ino.hex"
            image.write_text(":00000001FF\n", encoding="ascii")
            completed = subprocess.run(
                [
                    "arduino-cli",
                    "upload",
                    "--config-file",
                    str(config),
                    "--fqbn",
                    "localnrf54:nrf54l15clean:xiao_nrf54l15",
                    "-P",
                    "pyocd",
                    "--input-file",
                    str(image),
                    "-p",
                    "/dev/null",
                    "-v",
                ],
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "PATH": f"{fake_bin}{os.pathsep}{os.environ.get('PATH', '')}",
                },
            )
            output = completed.stdout + completed.stderr
            self.assertEqual(completed.returncode, 0, output)
            self.assertIn("--runner pyocd", output.replace('"', ""))
            self.assertNotIn("{programmer.", output)
            self.assertNotIn("{upload.", output)

            completed = subprocess.run(
                [
                    "arduino-cli",
                    "upload",
                    "--config-file",
                    str(config),
                    "--fqbn",
                    "localnrf54:nrf54l15clean:xiao_nrf54l15",
                    "-P",
                    "jlink",
                    "--input-file",
                    str(image),
                    "-v",
                ],
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "PATH": f"{fake_bin}{os.pathsep}{os.environ.get('PATH', '')}",
                },
            )
            output = completed.stdout + completed.stderr
            self.assertEqual(completed.returncode, 0, output)
            normalized = output.replace('"', "")
            self.assertRegex(normalized, r"--runner\s+pyocd")
            self.assertRegex(normalized, r"--probe-type\s+jlink")
            self.assertNotIn("--port", normalized)
            self.assertNotIn("{upload.", output)

    @unittest.skipUnless(
        shutil.which("arduino-cli") and Path("/bin/echo").is_file(),
        "Arduino CLI expansion test requires arduino-cli and /bin/echo",
    )
    def test_arduino_cli_expands_default_upload_in_quiet_and_verbose_modes(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-upload-test-") as directory:
            root = Path(directory)
            user = root / "user"
            data = root / "data"
            downloads = root / "downloads"
            fake_bin = root / "bin"
            hardware = user / "hardware" / "localnrf54"
            hardware.mkdir(parents=True)
            data.mkdir()
            downloads.mkdir()
            fake_bin.mkdir()
            fake_python = fake_bin / "python3"
            fake_python.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\"\n", encoding="utf-8")
            fake_python.chmod(0o755)
            (hardware / "nrf54l15clean").symlink_to(
                UPLOAD_PATH.parents[1], target_is_directory=True
            )
            config = root / "arduino-cli.yaml"
            config.write_text(
                "directories:\n"
                f"  data: {data}\n"
                f"  downloads: {downloads}\n"
                f"  user: {user}\n",
                encoding="utf-8",
            )
            image = root / "RecipeProbe.ino.hex"
            image.write_text(":00000001FF\n", encoding="ascii")
            command = [
                "arduino-cli",
                "upload",
                "--config-file",
                str(config),
                "--fqbn",
                "localnrf54:nrf54l15clean:xiao_nrf54l15",
                "--input-file",
                str(image),
                "-p",
                "/dev/null",
            ]

            for verbose_args in ([], ["-v"]):
                with self.subTest(verbose=bool(verbose_args)):
                    completed = subprocess.run(
                        [*command, *verbose_args],
                        capture_output=True,
                        text=True,
                        env={
                            **os.environ,
                            "PATH": f"{fake_bin}{os.pathsep}{os.environ.get('PATH', '')}",
                        },
                    )
                    output = completed.stdout + completed.stderr
                    self.assertEqual(0, completed.returncode, output)
                    self.assertRegex(output.replace('"', ""), r"--runner\s+nrf_ocd")
                    self.assertIn("/tools/upload.py", output)
                    self.assertNotIn("{tools.", output)
                    self.assertNotIn("{upload.", output)
                    self.assertRegex(output.replace('"', ""), r"--uf2-timeout\s+12")
                    self.assertNotIn("--openocd-bin", output)
                    self.assertNotIn("{upload.verbose}", output)

            completed = subprocess.run(
                [*command, "--board-options", "clean_upload=jlink", "-v"],
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "PATH": f"{fake_bin}{os.pathsep}{os.environ.get('PATH', '')}",
                },
            )
            output = completed.stdout + completed.stderr
            self.assertEqual(0, completed.returncode, output)
            normalized = output.replace('"', "")
            self.assertRegex(normalized, r"--runner\s+pyocd")
            self.assertRegex(normalized, r"--probe-type\s+jlink")
            self.assertNotIn("{upload.", output)


    def test_uf2_emitter_produces_valid_blocks(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-uf2-test-") as directory:
            source = Path(directory) / "probe.hex"
            output = Path(directory) / "probe.uf2"
            source.write_text(
                ":020000040000FA\n:0400000001020304F2\n:00000001FF\n",
                encoding="ascii",
            )
            subprocess.run(
                [
                    sys.executable,
                    str(UF2_EMITTER),
                    "--input",
                    str(source),
                    "--output",
                    str(output),
                    "--family",
                    "0xADA54B15",
                    "--base-address",
                    "0",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            data = output.read_bytes()
            self.assertGreaterEqual(len(data), 512)
            self.assertEqual(len(data) % 512, 0)
            magic0, magic1 = struct.unpack_from("<II", data, 0)
            magic_end = struct.unpack_from("<I", data, 508)[0]
            self.assertEqual(magic0, 0x0A324655)
            self.assertEqual(magic1, 0x9E5D5157)
            self.assertEqual(magic_end, 0x0AB16F30)

    def test_uf2_deploy_write_rejects_device_root_escape(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-uf2-root-") as directory:
            root = Path(directory) / "drive"
            root.mkdir()
            outside = Path(directory) / "NEW.UF2"

            with self.assertRaises(ValueError):
                UF2_CONV_MODULE.write_file(str(root / ".." / "NEW.UF2"), b"bad", root=root)

            self.assertFalse(outside.exists())

    def test_uf2_deploy_write_allows_device_root_child(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-uf2-root-") as directory:
            root = Path(directory) / "drive"
            root.mkdir()
            output = root / "NEW.UF2"

            UF2_CONV_MODULE.write_file(str(output), b"ok", root=root)

            self.assertEqual(output.read_bytes(), b"ok")


if __name__ == "__main__":
    unittest.main()
