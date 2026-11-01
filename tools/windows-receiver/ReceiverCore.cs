// SPDX-License-Identifier: Apache-2.0
// .NET Framework 4.x; the wire/storage core is also testable without WinForms.
using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace VelaPoka
{
    public sealed class ReceiverServer : IDisposable
    {
        public const long MaxFileSize = 512L * 1024 * 1024;
        private readonly string root;
        private readonly IPAddress allowedPeer;
        private readonly Action<string> report;
        private readonly TcpListener listener;
        private readonly int idleTimeoutMs;
        private readonly object gate = new object();
        private Thread worker;
        private TcpClient active;
        private volatile bool stopping;
        private static readonly Regex Device = new Regex("^[0-9a-f]{12}$");
        private static readonly Regex Image = new Regex("^fail/fail_[0-9]{8}\\.bmp$");

        public ReceiverServer(string directory, IPAddress bind, IPAddress peer,
                              int port, Action<string> log, int timeoutMs = 30000)
        {
            if (timeoutMs <= 0) throw new ArgumentOutOfRangeException("timeoutMs");
            root = Path.GetFullPath(directory);
            allowedPeer = peer;
            report = log ?? delegate(string text) { };
            idleTimeoutMs = timeoutMs;
            listener = new TcpListener(bind, port);
        }

        public int Port
        {
            get { return ((IPEndPoint)listener.LocalEndpoint).Port; }
        }

        public void Start()
        {
            Directory.CreateDirectory(root);
            RejectLink(root);
            listener.Start(2);
            worker = new Thread(Run);
            worker.IsBackground = true;
            worker.Start();
            report("Listening on " + listener.LocalEndpoint +
                   "; device " + allowedPeer + "; saving to " + root);
        }

        private void Run()
        {
            while (!stopping)
            {
                string phase = "Accepting connection";
                try
                {
                    using (TcpClient client = listener.AcceptTcpClient())
                    {
                        lock (gate)
                        {
                            if (stopping) break;
                            active = client;
                        }
                        IPAddress peer = ((IPEndPoint)client.Client.RemoteEndPoint).Address;
                        if (!peer.Equals(allowedPeer))
                        {
                            report("Rejected unexpected peer: " + peer);
                            continue;
                        }
                        client.ReceiveTimeout = idleTimeoutMs;
                        client.SendTimeout = idleTimeoutMs;
                        phase = "Waiting for handshake";
                        report("Connected: " + peer + "; idle timeout " + idleTimeoutMs + " ms");
                        Handle(client.GetStream(), ref phase);
                    }
                }
                catch (Exception ex)
                {
                    if (!stopping) report("Receive failed [" + phase + "]: " +
                        ex.Message + ". Tap Upload / Retry on device.");
                }
                finally
                {
                    lock (gate) { active = null; }
                }
            }
        }

        public static uint UpdateCrc(uint crc, byte[] bytes, int length)
        {
            for (int i = 0; i < length; i++)
            {
                crc ^= bytes[i];
                for (int bit = 0; bit < 8; bit++)
                    crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320U : 0U);
            }
            return crc;
        }

        private static uint FileCrc(string path, Stream peer)
        {
            uint crc = UInt32.MaxValue;
            byte[] buffer = new byte[4096];
            using (FileStream file = File.OpenRead(path))
            {
                int length;
                Stopwatch keepalive = Stopwatch.StartNew();
                while ((length = file.Read(buffer, 0, buffer.Length)) != 0)
                {
                    crc = UpdateCrc(crc, buffer, length);
                    if (keepalive.ElapsedMilliseconds >= 1000)
                    {
                        Reply(peer, "WAIT");
                        keepalive.Restart();
                    }
                }
            }
            return ~crc;
        }

        private static string Line(Stream stream)
        {
            // No StreamReader: buffering may consume bytes from a FILE body.
            StringBuilder line = new StringBuilder();
            for (int i = 0; i < 256; i++)
            {
                int value = stream.ReadByte();
                if (value < 0) return line.Length == 0 ? null : FailLine();
                if (value == 10) return line.ToString();
                if (value < 32 || value > 126) throw new InvalidDataException("Non-ASCII header");
                line.Append((char)value);
            }
            throw new InvalidDataException("Header too long");
        }

        private static string FailLine()
        {
            throw new EndOfStreamException("Truncated header");
        }

        private static void Reply(Stream stream, string reply)
        {
            byte[] data = Encoding.ASCII.GetBytes(reply + "\n");
            stream.Write(data, 0, data.Length);
            stream.Flush();
        }

        private static void RejectLink(string path)
        {
            if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Reparse points are not supported: " + path);
        }

        private static void Journal(string folder, string entry)
        {
            string path = Path.Combine(folder, "upload-journal.jsonl");
            if (File.Exists(path)) RejectLink(path);
            byte[] data = Encoding.UTF8.GetBytes("{\"utc\":\"" +
                DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture) +
                "\"," + entry + "}\n");
            using (FileStream output = new FileStream(path, FileMode.Append,
                FileAccess.Write, FileShare.Read, 4096, FileOptions.WriteThrough))
            {
                output.Write(data, 0, data.Length);
                output.Flush(true);
            }
        }

        private void Handle(NetworkStream stream, ref string phase)
        {
            string hello = Line(stream);
            if (hello == null ||
                !(hello.StartsWith("VKP1 ") || hello.StartsWith("VKP2 ")) ||
                !Device.IsMatch(hello.Substring(5)))
                throw new InvalidDataException("Invalid VKP1 handshake");
            bool resilient = hello.StartsWith("VKP2 ");
            string device = hello.Substring(5);
            string folder = Path.Combine(root, device);
            Directory.CreateDirectory(folder);
            RejectLink(folder);
            Reply(stream, resilient ? "OK2" : "OK");
            report("Handshake OK: " + device + "; waiting for upload commands");

            int saved = 0;
            int skipped = 0;
            bool snapshotConfirmed = false;
            while (!stopping)
            {
                phase = "Waiting for FILE / PING / DONE";
                string command = Line(stream);
                if (command == null) return; // Connection test, or cancelled batch.
                if (command == "PING")
                {
                    Reply(stream, "OK");
                    continue;
                }
                if (command == "DONE")
                {
                    Reply(stream, "OK");
                    report("Upload complete: " + saved + " saved, " + skipped +
                           " unchanged. Device " + device);
                    return;
                }

                // Bind the Framework-compatible overload even when built
                // on Mono, which also exposes newer optional overloads.
                string[] parts = command.Split(new char[] { ' ' },
                                                StringSplitOptions.None);
                if (resilient && parts.Length == 3 && parts[0] == "FAIL")
                {
                    int error;
                    if (!(parts[1] == "results.jsonl" || Image.IsMatch(parts[1])) ||
                        !Int32.TryParse(parts[2], NumberStyles.AllowLeadingSign,
                            CultureInfo.InvariantCulture, out error) ||
                        error >= 0 || error < -4095)
                        throw new InvalidDataException("Invalid FAIL report");
                    phase = "Recording failure " + parts[1];
                    Journal(folder, "\"event\":\"file_failed\",\"file\":\"" +
                        parts[1] + "\",\"error\":" + error);
                    report("File failed: " + parts[1] + " error=" + error +
                        "; recorded in upload-journal.jsonl");
                    Reply(stream, "OK");
                    continue;
                }
                if (resilient && parts.Length == 4 && parts[0] == "END")
                {
                    uint totalSaved, totalSkipped, failed;
                    if (!UInt32.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out totalSaved) ||
                        !UInt32.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out totalSkipped) ||
                        !UInt32.TryParse(parts[3], NumberStyles.None, CultureInfo.InvariantCulture, out failed))
                        throw new InvalidDataException("Invalid END summary");
                    if (failed == 0 && !snapshotConfirmed)
                        throw new InvalidDataException("Complete summary without confirmed results.jsonl");
                    phase = "Recording batch summary";
                    Journal(folder, "\"event\":\"batch_end\",\"saved\":" + totalSaved +
                        ",\"skipped\":" + totalSkipped + ",\"failed\":" + failed);
                    Reply(stream, "OK");
                    report((failed == 0 ? "Upload complete: " : "Upload INCOMPLETE: ") +
                        totalSaved + " saved, " + totalSkipped + " unchanged, " + failed +
                        " failed. Device " + device);
                    return;
                }
                long size;
                uint expected;
                if (parts.Length != 4 || parts[0] != "FILE" ||
                    !(parts[1] == "results.jsonl" || Image.IsMatch(parts[1])) ||
                    !Int64.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out size) ||
                    size < 0 || size > MaxFileSize || parts[3].Length != 8 ||
                    !UInt32.TryParse(parts[3], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out expected))
                {
                    Reply(stream, "ERROR");
                    throw new InvalidDataException("Invalid FILE header");
                }

                string relative = parts[1];
                phase = "Preparing " + relative;
                string destination = Path.Combine(folder, relative.Replace('/', Path.DirectorySeparatorChar));
                string parent = Path.GetDirectoryName(destination);
                Directory.CreateDirectory(parent);
                RejectLink(parent);
                if (File.Exists(destination))
                {
                    phase = "Checking existing " + relative;
                    report(phase);
                    RejectLink(destination);
                    if (new FileInfo(destination).Length == size && FileCrc(destination, stream) == expected)
                    {
                        Reply(stream, "HAVE");
                        skipped++;
                        if (relative == "results.jsonl") snapshotConfirmed = true;
                        report("Unchanged: " + relative);
                        continue;
                    }
                }

                string temporary = Path.Combine(parent, ".vkp-" + Guid.NewGuid().ToString("N") + ".part");
                try
                {
                    phase = "Receiving " + relative;
                    Reply(stream, "SEND");
                    uint crc = UInt32.MaxValue;
                    long received = 0;
                    byte[] buffer = new byte[4096];
                    report("Receiving " + relative + " (" + size + " bytes)");
                    using (FileStream output = new FileStream(temporary, FileMode.CreateNew,
                           FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough))
                    {
                        while (received < size)
                        {
                            if (stopping) throw new OperationCanceledException();
                            int count = stream.Read(buffer, 0, (int)Math.Min(buffer.Length, size - received));
                            if (count == 0) throw new EndOfStreamException("Interrupted file");
                            output.Write(buffer, 0, count);
                            crc = UpdateCrc(crc, buffer, count);
                            received += count;
                        }
                        phase = "Flushing " + relative;
                        output.Flush(true);
                    }

                    phase = "Verifying CRC " + relative;
                    if (~crc != expected)
                    {
                        Reply(stream, "ERROR");
                        throw new InvalidDataException("CRC32 mismatch: " + relative);
                    }

                    // Replace only after the complete file is flushed and verified.
                    // Preserve the last differing JSONL snapshot for recovery.
                    phase = "Saving " + relative;
                    if (File.Exists(destination))
                    {
                        RejectLink(destination);
                        string backup = Path.Combine(parent,
                            Path.GetFileNameWithoutExtension(destination) + "-" +
                            DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" +
                            Guid.NewGuid().ToString("N") + Path.GetExtension(destination));
                        File.Replace(temporary, destination, backup);
                    }
                    else File.Move(temporary, destination);
                    phase = "Acknowledging " + relative;
                    Reply(stream, "OK");
                    saved++;
                    if (relative == "results.jsonl") snapshotConfirmed = true;
                    report("Saved and verified " + relative);
                }
                finally
                {
                    if (File.Exists(temporary)) File.Delete(temporary);
                }
            }
        }

        public void Dispose()
        {
            stopping = true;
            listener.Stop();
            lock (gate) { if (active != null) active.Close(); }
            if (worker != null) worker.Join();
        }
    }
}
