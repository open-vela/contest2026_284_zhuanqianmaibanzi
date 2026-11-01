"""SPDX-License-Identifier: Apache-2.0
Compile real device C sources and exercise them against the C# receiver.
Usage: python3 tests/host/test_upload.py --mono MONO --mcs MCS_EXE
Mono's assembly/config paths may be supplied through MONO_PATH/MONO_CFG_DIR.
"""
import argparse
import binascii
import contextlib
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import time
import threading
import unittest
from upload_fault_proxy import FaultProxy

ROOT = pathlib.Path(__file__).resolve().parents[2]


class UploadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="velapoka-upload-")
        cls.work = pathlib.Path(cls.tmp.name)
        cls.sd = cls.work / "sd"
        cls.sd.mkdir()
        cls.destination = cls.work / "pc"
        cls.client = cls.work / "client"
        cls.receiver = cls.work / "ReceiverHarness.exe"
        cls.compile_command = [
            "gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-I" + str(ROOT / "tests/host/include"),
            "-I" + str(ROOT / "app/velapoka"),
            str(ROOT / "tests/host/upload_client.c"),
            str(ROOT / "app/velapoka/velapoka_storage.c"),
            str(ROOT / "app/velapoka/velapoka_upload.c"),
            str(ROOT / "tests/host/upload_slow_io.c"),
            "-Wl,--wrap=statfs", "-Wl,--wrap=read",
            "-Wl,--wrap=pthread_mutex_timedlock", "-Wl,--wrap=malloc",
            "-o", str(cls.client)]
        subprocess.run(cls.compile_command, check=True)
        subprocess.run([ARGS.mono, ARGS.mcs, "-warnaserror", "-r:System.Core",
                        "-out:" + str(cls.receiver),
                        str(ROOT / "tools/windows-receiver/ReceiverCore.cs"),
                        str(ROOT / "tests/host/ReceiverHarness.cs")], check=True)
        cls.server = subprocess.Popen([ARGS.mono, str(cls.receiver), str(cls.destination)],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      text=True)
        cls.port = int(cls.server.stdout.readline().strip())

    @classmethod
    def tearDownClass(cls):
        cls.server.communicate("\n", timeout=10)
        cls.tmp.cleanup()

    def connect(self, device="deadbeef0002"):
        connection = socket.create_connection(("127.0.0.1", self.port), timeout=8)
        connection.settimeout(8)
        stream = connection.makefile("rwb", buffering=0)
        stream.write(("VKP1 " + device + "\n").encode())
        self.assertEqual(stream.readline(), b"OK\n")
        return connection, stream

    @contextlib.contextmanager
    def receiver_session(self, name):
        destination = self.work / name
        server = subprocess.Popen(
            [ARGS.mono, str(self.receiver), str(destination)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        try:
            yield int(server.stdout.readline().strip()), destination
        finally:
            server.communicate("\n", timeout=10)

    def fault_sd(self, name, missing=False):
        sd = self.work / name
        folder = sd / "velapoka"
        (folder / "fail").mkdir(parents=True)
        payload = (self.sd / "velapoka/fail/fail_00000001.bmp").read_bytes()
        rows = []
        for number in (1, 2):
            image = "fail/fail_%08d.bmp" % number
            if not (number == 1 and missing):
                (folder / image).write_bytes(payload)
            rows.append({"id": number, "frame": number, "result": "FAIL",
                         "score_tenths": 100, "image": image})
        (folder / "results.jsonl").write_text(
            "".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows))
        return sd

    def run_client(self, sd, port, mode="ok", env=None):
        return subprocess.check_output(
            [str(self.client), str(sd), str(port), "reuse", mode],
            text=True, env=env, timeout=25)

    def test_01_real_client_and_repeat(self):
        first = subprocess.check_output([str(self.client), str(self.sd), str(self.port), "seed", "ok"], text=True)
        self.assertIn("saved=2 skipped=0", first)
        device = self.destination / "deadbeef0001"
        self.assertEqual((device / "results.jsonl").read_bytes(),
                         (self.sd / "velapoka/results.jsonl").read_bytes())
        self.assertEqual((device / "fail/fail_00000001.bmp").read_bytes(),
                         (self.sd / "velapoka/fail/fail_00000001.bmp").read_bytes())
        repeat = subprocess.check_output([str(self.client), str(self.sd), str(self.port), "reuse", "ok"], text=True)
        self.assertIn("saved=0 skipped=2", repeat)

    def test_02_crc_failure_and_retry(self):
        connection, stream = self.connect()
        with connection, stream:
            stream.write(b"FILE results.jsonl 3 00000000\n")
            self.assertEqual(stream.readline(), b"SEND\n")
            stream.write(b"bad")
            self.assertEqual(stream.readline(), b"ERROR\n")
        self.assertFalse((self.destination / "deadbeef0002/results.jsonl").exists())
        connection, stream = self.connect()
        with connection, stream:
            payload = b"{\"id\":1}\n"
            stream.write(("FILE results.jsonl %d %08x\n" % (len(payload), binascii.crc32(payload))).encode())
            self.assertEqual(stream.readline(), b"SEND\n")
            stream.write(payload)
            self.assertEqual(stream.readline(), b"OK\n")
            stream.write(b"DONE\n")
            self.assertEqual(stream.readline(), b"OK\n")
        self.assertEqual((self.destination / "deadbeef0002/results.jsonl").read_bytes(), payload)

    def test_03_paths_and_limits(self):
        for name, size in [("../oops", 1), ("config.json", 1), ("fail/FAIL.bmp", 1),
                           ("results.jsonl", -1), ("results.jsonl", 536870913)]:
            connection, stream = self.connect()
            with connection, stream:
                stream.write(("FILE %s %d 00000000\n" % (name, size)).encode())
                self.assertEqual(stream.readline(), b"ERROR\n")

    def test_04_disconnect_preserves_previous(self):
        previous = (self.destination / "deadbeef0002/results.jsonl").read_bytes()
        connection, stream = self.connect()
        stream.write(b"FILE results.jsonl 100 00000000\n")
        self.assertEqual(stream.readline(), b"SEND\n")
        stream.write(b"short")
        stream.close()
        connection.close()
        # A handshake on the next connection ensures the prior session cleaned up.
        connection, stream = self.connect()
        stream.close()
        connection.close()
        self.assertEqual((self.destination / "deadbeef0002/results.jsonl").read_bytes(), previous)
        self.assertEqual(list(self.destination.rglob("*.part")), [])

    def test_05_new_snapshot_backs_up_previous(self):
        connection, stream = self.connect()
        payload = b"{\"id\":1}\n{\"id\":2}\n"
        with connection, stream:
            stream.write(("FILE results.jsonl %d %08x\n" % (len(payload), binascii.crc32(payload))).encode())
            self.assertEqual(stream.readline(), b"SEND\n")
            # Headers and payload may arrive in fragmented TCP packets.
            for part in payload:
                stream.write(bytes([part]))
            self.assertEqual(stream.readline(), b"OK\n")
            stream.write(b"DONE\n")
            self.assertEqual(stream.readline(), b"OK\n")
        self.assertTrue(list((self.destination / "deadbeef0002").glob("results-*.jsonl")))

    def test_06_log_larger_than_old_4mib_limit(self):
        sd = self.work / "large-sd"
        (sd / "velapoka").mkdir(parents=True)
        log = sd / "velapoka/results.jsonl"
        with log.open("w") as file:
            for i in range(6500):
                row = {"id": i + 1, "frame": i, "result": "PASS",
                       "score_tenths": 1000, "image": "", "padding": "x" * 700}
                file.write(json.dumps(row, separators=(",", ":")) + "\n")
        self.assertGreater(log.stat().st_size, 4 * 1024 * 1024)
        first = subprocess.check_output([str(self.client), str(sd), str(self.port), "reuse", "ok"], text=True, timeout=30)
        self.assertIn("saved=1 skipped=0", first)
        repeat = subprocess.check_output([str(self.client), str(sd), str(self.port), "reuse", "ok"], text=True, timeout=30)
        self.assertIn("saved=0 skipped=1", repeat)
        self.assertEqual((self.destination / "deadbeef0001/results.jsonl").read_bytes(), log.read_bytes())

    def test_07_exit_cancels_silent_peer(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        def silent_peer():
            with listener:
                connection, _ = listener.accept()
                with connection:
                    while connection.recv(256):
                        pass
        worker = threading.Thread(target=silent_peer, daemon=True)
        worker.start()
        started = time.monotonic()
        result = subprocess.check_output([str(self.client), str(self.sd), str(listener.getsockname()[1]), "reuse", "cancel"], text=True, timeout=5)
        self.assertIn("cancelled", result)
        self.assertLess(time.monotonic() - started, 2)
        worker.join(2)
        self.assertFalse(worker.is_alive())

    def test_08_absent_receiver_reports_error(self):
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            port = unused.getsockname()[1]
        result = subprocess.check_output([str(self.client), str(self.sd), str(port), "reuse", "error"], text=True, timeout=8)
        self.assertIn("state=4", result)

    def test_09_slow_small_bmp_keeps_strict_receiver_alive(self):
        # Keep the OLD 5-second receiver limit to prove the device heartbeat
        # fix, not merely the new PC timeout. A BMP is smaller than 64 KiB;
        # 400 ms per 4 KiB makes its CRC scan alone take about 6 seconds.
        destination = self.work / "slow-pc"
        server = subprocess.Popen(
            [ARGS.mono, str(self.receiver), str(destination), "5000"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        try:
            port = int(server.stdout.readline().strip())
            env = dict(os.environ, VKP_TEST_READ_DELAY_US="400000")
            args = [str(self.client), str(self.sd), str(port), "reuse", "ok"]
            started = time.monotonic()
            first = subprocess.check_output(args, env=env, text=True, timeout=30)
            # Small-file caching removes the second SD read, but CRC still
            # exceeds the old 5-second receiver limit and needs heartbeats.
            self.assertGreater(time.monotonic() - started, 5)
            self.assertIn("reads=14", first)
            self.assertIn("saved=2 skipped=0", first)
            image = "deadbeef0001/fail/fail_00000001.bmp"
            self.assertEqual((destination / image).read_bytes(),
                             (self.sd / "velapoka/fail/fail_00000001.bmp").read_bytes())
            repeat = subprocess.check_output(args, env=env, text=True, timeout=20)
            self.assertIn("saved=0 skipped=2", repeat)
        finally:
            _, log = server.communicate("\n", timeout=10)
        self.assertIn("Handshake OK", log)
        self.assertNotIn("Receive failed", log)

    def test_10_receiver_tolerates_sd_pause_over_old_timeout(self):
        connection, stream = self.connect("deadbeef0003")
        payload = b"{\"id\":3}\n"
        with connection, stream:
            stream.write(("FILE results.jsonl %d %08x\n" %
                          (len(payload), binascii.crc32(payload))).encode())
            self.assertEqual(stream.readline(), b"SEND\n")
            # Raw payload cannot contain protocol PINGs. A single long SD
            # read needs the receiver's bounded but more forgiving timeout.
            time.sleep(5.5)
            stream.write(payload)
            self.assertEqual(stream.readline(), b"OK\n")
            stream.write(b"DONE\n")
            self.assertEqual(stream.readline(), b"OK\n")
        self.assertEqual((self.destination / "deadbeef0003/results.jsonl").read_bytes(), payload)

    def test_11_exit_cancels_slow_crc_scan(self):
        env = dict(os.environ, VKP_TEST_READ_DELAY_US="400000")
        started = time.monotonic()
        result = subprocess.check_output(
            [str(self.client), str(self.sd), str(self.port), "reuse", "cancel"],
            env=env, text=True, timeout=5)
        self.assertIn("cancelled", result)
        self.assertLess(time.monotonic() - started, 2)

    def test_12_device_waits_for_slow_pc_confirmation(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        port = listener.getsockname()[1]
        failures = []

        def slow_pc():
            try:
                with listener:
                    connection, _ = listener.accept()
                    connection.settimeout(8)
                    with connection, connection.makefile("rwb", buffering=0) as stream:
                        self.assertTrue(stream.readline().startswith(b"VKP2 "))
                        stream.write(b"OK2\n")
                        delayed = False
                        while True:
                            command = stream.readline().split()
                            if command == [b"PING"]:
                                stream.write(b"OK\n")
                            elif command[0] == b"END":
                                stream.write(b"OK\n")
                                return
                            else:
                                self.assertEqual(command[0], b"FILE")
                                remaining = int(command[2])
                                stream.write(b"SEND\n")
                                while remaining:
                                    data = stream.read(min(4096, remaining))
                                    self.assertTrue(data)
                                    remaining -= len(data)
                                if not delayed:
                                    time.sleep(5.5)
                                    delayed = True
                                stream.write(b"OK\n")
            except Exception as error:
                failures.append(error)

        worker = threading.Thread(target=slow_pc, daemon=True)
        worker.start()
        result = subprocess.check_output(
            [str(self.client), str(self.sd), str(port), "reuse", "ok"],
            text=True, timeout=20)
        worker.join(10)
        self.assertFalse(worker.is_alive())
        self.assertEqual(failures, [])
        self.assertIn("saved=2 skipped=0", result)

    def test_13_idle_timeout_is_bounded_and_reports_stage(self):
        destination = self.work / "idle-pc"
        server = subprocess.Popen(
            [ARGS.mono, str(self.receiver), str(destination), "250"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        try:
            port = int(server.stdout.readline().strip())
            with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
                connection.settimeout(3)
                with connection.makefile("rwb", buffering=0) as stream:
                    stream.write(b"VKP1 deadbeef0004\n")
                    self.assertEqual(stream.readline(), b"OK\n")
                    stream.write(b"FILE results.jsonl 3 00000000\n")
                    self.assertEqual(stream.readline(), b"SEND\n")
                    self.assertEqual(stream.readline(), b"")
            # Accepting another handshake is a barrier: the previous Run()
            # catch must have emitted its phase log before Stop can suppress
            # shutdown-time errors. EOF alone can race that catch.
            with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
                connection.settimeout(3)
                with connection.makefile("rwb", buffering=0) as stream:
                    stream.write(b"VKP1 deadbeef0004\n")
                    self.assertEqual(stream.readline(), b"OK\n")
            self.assertFalse((destination / "deadbeef0004/results.jsonl").exists())
            self.assertEqual(list(destination.rglob("*.part")), [])
        finally:
            _, log = server.communicate("\n", timeout=5)
        self.assertIn("Receive failed [Receiving results.jsonl]", log)

    def test_14_permanent_crc_failure_continues_and_later_recovers(self):
        sd = self.fault_sd("permanent-crc-sd")
        with self.receiver_session("permanent-crc-pc") as (port, destination):
            with FaultProxy(port, corrupt=3) as proxy:
                result = self.run_client(sd, proxy.port, "partial")
            self.assertEqual(proxy.attempts["fail/fail_00000001.bmp"], 3)
            self.assertIn("saved=1 skipped=0 failed=1 retries=2 withheld=1", result)
            device = destination / "deadbeef0001"
            self.assertFalse((device / "results.jsonl").exists())
            self.assertFalse((device / "fail/fail_00000001.bmp").exists())
            self.assertTrue((device / "fail/fail_00000002.bmp").exists())
            self.assertEqual(list(destination.rglob("*.part")), [])
            failures = (sd / "velapoka/upload-errors.jsonl").read_text()
            self.assertIn('"file":"fail/fail_00000001.bmp"', failures)
            journal = [json.loads(line) for line in (device / "upload-journal.jsonl").read_text().splitlines()]
            self.assertEqual(journal[-1]["failed"], 1)
            self.assertEqual(journal[0]["event"], "file_failed")
            recovered = self.run_client(sd, port)
            self.assertIn("saved=2 skipped=1 failed=0", recovered)
            self.assertEqual((device / "results.jsonl").read_bytes(),
                             (sd / "velapoka/results.jsonl").read_bytes())

    def test_15_transient_crc_error_retries_same_file(self):
        sd = self.fault_sd("transient-crc-sd")
        with self.receiver_session("transient-crc-pc") as (port, destination):
            with FaultProxy(port, corrupt=1) as proxy:
                result = self.run_client(sd, proxy.port)
            self.assertEqual(proxy.attempts["fail/fail_00000001.bmp"], 2)
            self.assertIn("saved=3 skipped=0 failed=0 retries=1", result)
            self.assertFalse((sd / "velapoka/upload-errors.jsonl").exists())
            self.assertEqual(list(destination.rglob("*.part")), [])

    def test_16_missing_image_continues_preserves_previous_manifest(self):
        sd = self.fault_sd("missing-image-sd", missing=True)
        with self.receiver_session("missing-image-pc") as (port, destination):
            device = destination / "deadbeef0001"
            device.mkdir()
            previous = b"previous complete snapshot\n"
            (device / "results.jsonl").write_bytes(previous)
            result = self.run_client(sd, port, "partial")
            self.assertIn("saved=1 skipped=0 failed=1 retries=0 withheld=1", result)
            self.assertEqual((device / "results.jsonl").read_bytes(), previous)
            self.assertTrue((device / "fail/fail_00000002.bmp").exists())

    def test_17_lost_save_ack_reconnects_and_deduplicates(self):
        sd = self.fault_sd("lost-ack-sd")
        with self.receiver_session("lost-ack-pc") as (port, destination):
            with FaultProxy(port, drop_ack=True) as proxy:
                result = self.run_client(sd, proxy.port)
            self.assertTrue(proxy.dropped)
            self.assertIn("saved=2 skipped=1 failed=0 retries=1", result)
            self.assertTrue((destination / "deadbeef0001/results.jsonl").exists())

    def test_18_old_receiver_is_rejected_before_upload(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        port = listener.getsockname()[1]
        def old_pc():
            with listener:
                connection, _ = listener.accept()
                with connection, connection.makefile("rwb", buffering=0) as stream:
                    stream.readline()
                    stream.write(b"OK\n")
        worker = threading.Thread(target=old_pc, daemon=True)
        worker.start()
        result = self.run_client(self.sd, port, "error")
        worker.join(5)
        self.assertFalse(worker.is_alive())
        self.assertIn("saved=0 skipped=0 failed=0", result)

    def test_19_failure_log_error_is_visible_without_false_success(self):
        sd = self.fault_sd("log-error-sd", missing=True)
        (sd / "velapoka/upload-errors.jsonl").mkdir()
        with self.receiver_session("log-error-pc") as (port, destination):
            result = self.run_client(sd, port, "partial")
            self.assertIn("log_error=1", result)
            self.assertTrue((destination / "deadbeef0001/upload-journal.jsonl").exists())
            self.assertFalse((destination / "deadbeef0001/results.jsonl").exists())

    def test_20_transient_sd_read_error_reopens_and_retries(self):
        sd = self.fault_sd("transient-read-sd")
        with self.receiver_session("transient-read-pc") as (port, destination):
            result = self.run_client(sd, port, env=dict(os.environ, VKP_TEST_READ_FAILS="1"))
            self.assertIn("saved=3 skipped=0 failed=0 retries=1", result)
            self.assertTrue((destination / "deadbeef0001/results.jsonl").exists())

    def test_21_exit_cancels_retry_backoff(self):
        sd = self.fault_sd("cancel-retry-sd")
        with self.receiver_session("cancel-retry-pc") as (port, _):
            started = time.monotonic()
            result = self.run_client(sd, port, "cancel",
                                     dict(os.environ, VKP_TEST_READ_FAILS="1000"))
            self.assertIn("cancelled", result)
            self.assertLess(time.monotonic() - started, 2)

    def test_22_failure_log_survives_application_reinitialization(self):
        sd = self.fault_sd("persistent-log-sd", missing=True)
        with self.receiver_session("persistent-log-pc") as (port, _):
            self.run_client(sd, port, "partial")
            self.run_client(sd, port, "partial")
            entries = [json.loads(line) for line in
                       (sd / "velapoka/upload-errors.jsonl").read_text().splitlines()]
            self.assertEqual(len(entries), 2)
            self.assertTrue(all(entry["file"] == "fail/fail_00000001.bmp" for entry in entries))

    def test_23_malformed_manifest_aborts_instead_of_skipping_validation(self):
        sd = self.fault_sd("malformed-log-sd")
        (sd / "velapoka/results.jsonl").write_text('{"id":1,"image":"../oops"}\n')
        with self.receiver_session("malformed-log-pc") as (port, destination):
            result = self.run_client(sd, port, "partial")
            self.assertIn("saved=0 skipped=0 failed=1", result)
            self.assertFalse((destination / "deadbeef0001/results.jsonl").exists())
            self.assertIn('"file":"results.jsonl"',
                          (sd / "velapoka/upload-errors.jsonl").read_text())

    def test_24_storage_lock_wait_has_a_deadline_and_recovers(self):
        sd = self.fault_sd("busy-lock-sd")
        with self.receiver_session("busy-lock-pc") as (port, _):
            started = time.monotonic()
            result = self.run_client(sd, port, env=dict(os.environ, VKP_TEST_LOCK_BUSY="1"))
            self.assertGreater(time.monotonic() - started, 2.8)
            self.assertLess(time.monotonic() - started, 8)
            self.assertIn("saved=3 skipped=0 failed=0", result)

    def test_25_wait_messages_cannot_extend_reply_forever(self):
        client = self.work / "short-reply-client"
        command = list(self.compile_command)
        command[command.index("-o") + 1] = str(client)
        subprocess.run(command + ["-DUPLOAD_REPLY_MAX_MS=500"], check=True)
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        port = listener.getsockname()[1]
        def waiting_pc():
            with listener:
                connection, _ = listener.accept()
                try:
                    with connection, connection.makefile("rwb", buffering=0) as stream:
                        stream.readline()
                        for _ in range(40):
                            stream.write(b"WAIT\n")
                            time.sleep(0.05)
                except (BrokenPipeError, ConnectionResetError):
                    pass
        worker = threading.Thread(target=waiting_pc, daemon=True)
        worker.start()
        started = time.monotonic()
        result = subprocess.check_output(
            [str(client), str(self.sd), str(port), "reuse", "error"],
            text=True, timeout=4)
        worker.join(3)
        self.assertFalse(worker.is_alive())
        self.assertLess(time.monotonic() - started, 2)
        self.assertIn("error=-110", result)

    def test_26_pc_rejects_complete_summary_without_manifest(self):
        with self.receiver_session("false-complete-pc") as (port, destination):
            with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
                connection.settimeout(3)
                with connection.makefile("rwb", buffering=0) as stream:
                    stream.write(b"VKP2 deadbeef0006\n")
                    self.assertEqual(stream.readline(), b"OK2\n")
                    stream.write(b"END 0 0 0\n")
                    self.assertEqual(stream.readline(), b"")
            self.assertFalse((destination / "deadbeef0006/upload-journal.jsonl").exists())

    def test_27_small_cache_allocation_failure_falls_back_to_chunks(self):
        sd = self.fault_sd("cache-oom-sd")
        with self.receiver_session("cache-oom-pc") as (port, destination):
            result = self.run_client(sd, port, env=dict(os.environ, VKP_TEST_CACHE_OOM="1"))
            self.assertIn("saved=3 skipped=0 failed=0", result)
            self.assertIn("reads=56", result)
            self.assertTrue((destination / "deadbeef0001/results.jsonl").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mono", default="mono")
    parser.add_argument("--mcs", required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__] + remaining)
