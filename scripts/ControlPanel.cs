using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using System.Diagnostics;
using System.IO;
using System.Security.Principal;
using System.Threading.Tasks;

namespace NeuroPace {
    public class ControlPanel : Form {
        private Button btnStartAll;
        private Button btnStopAll;
        private Button btnStartGame;
        private TextBox txtPid;
        private Label lblStatusTelemetry;
        private Label lblStatusEngine;
        private Label lblStatusActuator;
        private RichTextBox txtLog;
        private Timer statusTimer;

        public ControlPanel() {
            InitializeComponent();
            CheckAdmin();
            statusTimer = new Timer { Interval = 1000 };
            statusTimer.Tick += StatusTimer_Tick;
            statusTimer.Start();
        }

        private void InitializeComponent() {
            this.Text = "NeuroPace RDNA Control Center v0.1.0";
            this.Size = new Size(650, 480);
            this.BackColor = Color.FromArgb(24, 24, 28);
            this.ForeColor = Color.White;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.StartPosition = FormStartPosition.CenterScreen;

            // Title
            Label lblTitle = new Label {
                Text = "NeuroPace RDNA",
                Font = new Font("Segoe UI", 18, FontStyle.Bold),
                Location = new Point(20, 15),
                AutoSize = true,
                ForeColor = Color.FromArgb(0, 150, 255)
            };
            this.Controls.Add(lblTitle);

            Label lblSubTitle = new Label {
                Text = "Predictive Latency Management",
                Font = new Font("Segoe UI", 10, FontStyle.Regular),
                Location = new Point(23, 45),
                AutoSize = true,
                ForeColor = Color.LightGray
            };
            this.Controls.Add(lblSubTitle);

            // Group: Status
            GroupBox grpStatus = new GroupBox {
                Text = "System Status",
                ForeColor = Color.White,
                Font = new Font("Segoe UI", 9, FontStyle.Bold),
                Location = new Point(20, 80),
                Size = new Size(300, 120)
            };

            lblStatusTelemetry = CreateStatusLabel("Telemetry Collector", 20);
            lblStatusEngine = CreateStatusLabel("AI Prediction Engine", 50);
            lblStatusActuator = CreateStatusLabel("Hardware Actuator", 80);

            grpStatus.Controls.Add(lblStatusTelemetry);
            grpStatus.Controls.Add(lblStatusEngine);
            grpStatus.Controls.Add(lblStatusActuator);
            this.Controls.Add(grpStatus);

            // Group: Controls
            GroupBox grpControls = new GroupBox {
                Text = "Action Panel",
                ForeColor = Color.White,
                Font = new Font("Segoe UI", 9, FontStyle.Bold),
                Location = new Point(330, 80),
                Size = new Size(280, 120)
            };

            btnStartAll = CreateButton("Start Services", new Point(15, 25), Color.FromArgb(0, 120, 215));
            btnStartAll.Click += BtnStartAll_Click;
            btnStopAll = CreateButton("Stop All", new Point(150, 25), Color.FromArgb(200, 50, 50));
            btnStopAll.Click += BtnStopAll_Click;

            Label lblPid = new Label {
                Text = "Target Game PID:",
                Location = new Point(15, 70),
                AutoSize = true,
                Font = new Font("Segoe UI", 9, FontStyle.Regular)
            };
            txtPid = new TextBox {
                Location = new Point(125, 68),
                Size = new Size(60, 25),
                BackColor = Color.FromArgb(40, 40, 45),
                ForeColor = Color.White,
                BorderStyle = BorderStyle.FixedSingle
            };
            
            btnStartGame = CreateButton("Attach", new Point(195, 65), Color.FromArgb(50, 150, 50));
            btnStartGame.Size = new Size(70, 25);
            btnStartGame.Click += BtnStartGame_Click;

            grpControls.Controls.Add(btnStartAll);
            grpControls.Controls.Add(btnStopAll);
            grpControls.Controls.Add(lblPid);
            grpControls.Controls.Add(txtPid);
            grpControls.Controls.Add(btnStartGame);
            this.Controls.Add(grpControls);

            // Log Console
            Label lblLog = new Label {
                Text = "Event Log :",
                Font = new Font("Segoe UI", 9, FontStyle.Bold),
                Location = new Point(20, 215),
                AutoSize = true
            };
            this.Controls.Add(lblLog);

            txtLog = new RichTextBox {
                Location = new Point(20, 235),
                Size = new Size(590, 180),
                BackColor = Color.FromArgb(15, 15, 18),
                ForeColor = Color.LightGreen,
                Font = new Font("Consolas", 9),
                ReadOnly = true,
                BorderStyle = BorderStyle.None
            };
            this.Controls.Add(txtLog);

            Log("Control Center Initialized.");
        }

        private Label CreateStatusLabel(string text, int y) {
            return new Label {
                Text = text + " : [ STOPPED ]",
                Location = new Point(15, y),
                AutoSize = true,
                Font = new Font("Segoe UI", 9, FontStyle.Regular),
                ForeColor = Color.Gray
            };
        }

        private Button CreateButton(string text, Point loc, Color bg) {
            Button btn = new Button {
                Text = text,
                Location = loc,
                Size = new Size(125, 30),
                FlatStyle = FlatStyle.Flat,
                BackColor = bg,
                ForeColor = Color.White,
                Font = new Font("Segoe UI", 9, FontStyle.Bold),
                Cursor = Cursors.Hand
            };
            btn.FlatAppearance.BorderSize = 0;
            return btn;
        }

        private void CheckAdmin() {
            using (WindowsIdentity identity = WindowsIdentity.GetCurrent()) {
                WindowsPrincipal principal = new WindowsPrincipal(identity);
                if (!principal.IsInRole(WindowsBuiltInRole.Administrator)) {
                    MessageBox.Show("Please run NeuroPace Control Center as Administrator for full ETW kernel features.", "Administrator Rights Required", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
            }
        }

        private void StatusTimer_Tick(object sender, EventArgs e) {
            UpdateStatus(lblStatusTelemetry, "neuropace-telemetry");
            UpdateStatus(lblStatusActuator, "neuropace-actuator");
            CheckPythonStatus();
        }

        private void UpdateStatus(Label lbl, string processName) {
            Process[] pname = Process.GetProcessesByName(processName);
            if (pname.Length > 0) {
                lbl.Text = lbl.Text.Split(':')[0] + ": [ RUNNING ]";
                lbl.ForeColor = Color.LightGreen;
            } else {
                lbl.Text = lbl.Text.Split(':')[0] + ": [ STOPPED ]";
                lbl.ForeColor = Color.Gray;
            }
        }

        private void CheckPythonStatus() {
            // Find python processes running main.py using WMI or just check if any python is running.
            // Simplified check based on python process
            Process[] pname = Process.GetProcessesByName("python");
            if (pname.Length > 0) {
                lblStatusEngine.Text = "AI Prediction Engine : [ RUNNING ]";
                lblStatusEngine.ForeColor = Color.LightGreen;
            } else {
                lblStatusEngine.Text = "AI Prediction Engine : [ STOPPED ]";
                lblStatusEngine.ForeColor = Color.Gray;
            }
        }

        private void Log(string msg) {
            txtLog.AppendText(string.Format("[{0:HH:mm:ss}] {1}\n", DateTime.Now, msg));
            txtLog.ScrollToCaret();
        }

        private void BtnStartAll_Click(object sender, EventArgs e) {
            string basePath = AppDomain.CurrentDomain.BaseDirectory;
            
            try {
                Log("Starting Telemetry Collector...");
                StartProcess(Path.Combine(basePath, "bin", "neuropace-telemetry.exe"), "");
                
                Log("Starting AI Prediction Engine...");
                StartProcess("python", string.Format("\"{0}\"", Path.Combine(basePath, "scripts", "main.py")));
                
                Log("Services started. Waiting for sensor ready...");
            } catch (Exception ex) {
                Log("Error: " + ex.Message);
            }
        }

        private void BtnStopAll_Click(object sender, EventArgs e) {
            Log("Stopping all services...");
            KillProcess("neuropace-telemetry");
            KillProcess("neuropace-actuator");
            KillProcess("python");
        }

        private void BtnStartGame_Click(object sender, EventArgs e) {
            if (string.IsNullOrWhiteSpace(txtPid.Text)) {
                Log("Please enter a valid PID.");
                return;
            }

            string basePath = AppDomain.CurrentDomain.BaseDirectory;
            Log(string.Format("Attaching Actuator to PID {0}...", txtPid.Text));
            StartProcess(Path.Combine(basePath, @"bin\neuropace-actuator.exe"), string.Format("--pid {0}", txtPid.Text));
        }

        private void StartProcess(string file, string args) {
            ProcessStartInfo psi = new ProcessStartInfo {
                FileName = file,
                Arguments = args,
                WorkingDirectory = AppDomain.CurrentDomain.BaseDirectory,
                UseShellExecute = true,
                WindowStyle = ProcessWindowStyle.Minimized
            };
            try {
                Process.Start(psi);
            } catch (Exception ex) {
                Log(string.Format("Error launching '{0}': {1}", Path.GetFileName(file), ex.Message));
            }
        }

        private void KillProcess(string name) {
            foreach (Process p in Process.GetProcessesByName(name)) {
                try { p.Kill(); } catch { }
            }
        }

        [STAThread]
        static void Main() {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new ControlPanel());
        }
    }
}
