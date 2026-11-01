// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Net;
using System.Windows.Forms;

namespace VelaPoka
{
    public sealed class ReceiverWindow : Form
    {
        private readonly TextBox directory = new TextBox();
        private readonly TextBox bind = new TextBox();
        private readonly TextBox peer = new TextBox();
        private readonly TextBox port = new TextBox();
        private readonly TextBox log = new TextBox();
        private readonly Button start = new Button();
        private readonly Button browse = new Button();
        private readonly Label status = new Label();
        private readonly ConcurrentQueue<string> messages = new ConcurrentQueue<string>();
        private readonly string config;
        private ReceiverServer server;

        public ReceiverWindow()
        {
            Text = "VelaPoka - Ethernet Receiver (2026-10-31 resilient)";
            ClientSize = new Size(760, 520);
            MinimumSize = new Size(776, 559);
            Font = new Font("Segoe UI", 10);
            config = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                  "VelaPokaReceiver", "settings.txt");
            AddLabel("Save folder", 16, 20);
            directory.SetBounds(110, 16, 510, 28);
            browse.Text = "Choose...";
            browse.SetBounds(630, 14, 110, 32);
            browse.Click += delegate
            {
                using (FolderBrowserDialog dialog = new FolderBrowserDialog())
                {
                    dialog.SelectedPath = directory.Text;
                    if (dialog.ShowDialog() == DialogResult.OK) directory.Text = dialog.SelectedPath;
                }
            };
            AddLabel("PC Ethernet IP", 16, 68);
            bind.SetBounds(125, 64, 140, 28);
            AddLabel("Device IP", 284, 68);
            peer.SetBounds(365, 64, 140, 28);
            AddLabel("Port", 522, 68);
            port.SetBounds(562, 64, 75, 28);
            start.SetBounds(16, 110, 180, 38);
            start.Click += delegate { if (server == null) StartReceiver(); else StopReceiver(); };
            Button open = new Button();
            open.Text = "Open save folder";
            open.SetBounds(210, 110, 180, 38);
            open.Click += delegate
            {
                try
                {
                    Directory.CreateDirectory(directory.Text);
                    Process.Start("explorer.exe", directory.Text);
                }
                catch (Exception ex) { MessageBox.Show(ex.Message); }
            };
            status.SetBounds(16, 160, 720, 42);
            status.Text = "Set the PC wired adapter to 10.0.0.1 / 255.255.255.0.\nThen tap Data / Upload on the device.";
            log.Multiline = true;
            log.ReadOnly = true;
            log.ScrollBars = ScrollBars.Vertical;
            log.SetBounds(16, 210, 724, 290);
            log.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            Controls.AddRange(new Control[] { directory, bind, peer, port, browse, start, open, status, log });
            LoadSettings();
            SetRunning(false);
            System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
            timer.Interval = 200;
            timer.Tick += delegate
            {
                string text;
                while (messages.TryDequeue(out text))
                {
                    if (log.TextLength > 60000) log.Clear();
                    log.AppendText(DateTime.Now.ToString("HH:mm:ss") + "  " + text + Environment.NewLine);
                }
            };
            timer.Start();
            FormClosing += delegate { StopReceiver(); timer.Stop(); timer.Dispose(); };
            Shown += delegate { StartReceiver(); };
        }

        private void AddLabel(string text, int x, int y)
        {
            Label label = new Label();
            label.Text = text;
            label.AutoSize = true;
            label.Location = new Point(x, y);
            Controls.Add(label);
        }

        private void LoadSettings()
        {
            directory.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "VelaPoka");
            bind.Text = "10.0.0.1";
            peer.Text = "10.0.0.2";
            port.Text = "8899";
            try
            {
                if (File.Exists(config))
                {
                    string[] lines = File.ReadAllLines(config);
                    if (lines.Length == 4)
                    {
                        directory.Text = lines[0]; bind.Text = lines[1];
                        peer.Text = lines[2]; port.Text = lines[3];
                    }
                }
            }
            catch (IOException) { }
        }

        private void SetRunning(bool running)
        {
            start.Text = running ? "Stop receiving" : "Start receiving";
            directory.Enabled = bind.Enabled = peer.Enabled = port.Enabled = browse.Enabled = !running;
        }

        private void StartReceiver()
        {
            try
            {
                IPAddress local = IPAddress.Parse(bind.Text);
                IPAddress device = IPAddress.Parse(peer.Text);
                int number = Int32.Parse(port.Text);
                if (local.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork ||
                    device.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork ||
                    number < 1 || number > 65535) throw new ArgumentException("Use an IPv4 address and port 1-65535.");
                server = new ReceiverServer(directory.Text, local, device, number,
                    delegate(string text) { messages.Enqueue(text); });
                server.Start();
                SetRunning(true);
                Directory.CreateDirectory(Path.GetDirectoryName(config));
                File.WriteAllLines(config, new string[] { directory.Text, bind.Text, peer.Text, port.Text });
                status.Text = "READY on " + bind.Text + ":" + port.Text +
                              ". Tap Upload on device.\nKeep this receiver running; allow its firewall rule on the wired network.";
            }
            catch (Exception ex)
            {
                StopReceiver();
                messages.Enqueue("Cannot start: " + ex.Message);
                status.Text = "Not listening. Check PC wired IP / port / save folder, then Start receiving.";
            }
        }

        private void StopReceiver()
        {
            if (server != null) { server.Dispose(); server = null; }
            SetRunning(false);
        }

        [STAThread]
        public static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new ReceiverWindow());
        }
    }
}
