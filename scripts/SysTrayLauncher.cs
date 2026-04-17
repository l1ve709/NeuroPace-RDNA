using System;
using System.Drawing;
using System.Windows.Forms;
using System.Diagnostics;
using System.IO;
using System.Security.Principal;

namespace NeuroPace {
    static class Program {
        [STAThread]
        static void Main() {
            // Auto-elevate to Administrator if not already
            if (!IsAdministrator()) {
                try {
                    ProcessStartInfo psi = new ProcessStartInfo();
                    psi.FileName = System.Reflection.Assembly.GetExecutingAssembly().Location;
                    psi.Verb = "runas";
                    psi.UseShellExecute = true;
                    Process.Start(psi);
                } catch (Exception) {
                    // User declined UAC — continue without elevation
                }
                return;
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new TrayContext());
        }

        static bool IsAdministrator() {
            WindowsIdentity identity = WindowsIdentity.GetCurrent();
            WindowsPrincipal principal = new WindowsPrincipal(identity);
            return principal.IsInRole(WindowsBuiltInRole.Administrator);
        }
    }

    public class TrayContext : ApplicationContext {
        private NotifyIcon trayIcon;
        private Process nodeProcess;

        public TrayContext() {
            try {
                trayIcon = new NotifyIcon() {
                    Icon = SystemIcons.Shield,
                    ContextMenu = new ContextMenu(new MenuItem[] {
                        new MenuItem("Open Dashboard...", OpenDashboard),
                        new MenuItem("-"),
                        new MenuItem("Exit NeuroPace", Exit)
                    }),
                    Visible = true,
                    Text = "NeuroPace RDNA (Admin)"
                };

                trayIcon.DoubleClick += OpenDashboard;

                if (StartNodeServer()) {
                    System.Threading.Thread.Sleep(2000);
                    OpenBrowser("http://localhost:3200");
                    trayIcon.ShowBalloonTip(3000, "NeuroPace RDNA",
                        "Control Center is running. Check your browser.",
                        ToolTipIcon.Info);
                }
            } catch (Exception ex) {
                MessageBox.Show("Fatal Error: " + ex.Message,
                    "NeuroPace Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                Application.Exit();
            }
        }

        private bool StartNodeServer() {
            string basePath = AppDomain.CurrentDomain.BaseDirectory;
            string dashboardPath = Path.Combine(basePath, "dashboard");

            if (!Directory.Exists(dashboardPath)) {
                MessageBox.Show(
                    "Dashboard not found at:\n" + dashboardPath +
                    "\n\nEnsure you extracted all files.",
                    "NeuroPace Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                Application.Exit();
                return false;
            }

            // Check if node_modules exists
            string nodeModulesPath = Path.Combine(dashboardPath, "node_modules");
            if (!Directory.Exists(nodeModulesPath)) {
                MessageBox.Show(
                    "node_modules not found. Running npm install...",
                    "NeuroPace Setup", MessageBoxButtons.OK, MessageBoxIcon.Information);
                try {
                    Process npmInstall = Process.Start(new ProcessStartInfo {
                        FileName = "cmd.exe",
                        Arguments = "/C npm install",
                        WorkingDirectory = dashboardPath,
                        UseShellExecute = false,
                        CreateNoWindow = true
                    });
                    npmInstall.WaitForExit(30000);
                } catch (Exception ex) {
                    MessageBox.Show("npm install failed: " + ex.Message,
                        "NeuroPace Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    Application.Exit();
                    return false;
                }
            }

            try {
                ProcessStartInfo psi = new ProcessStartInfo {
                    FileName = "cmd.exe",
                    Arguments = "/C node server/index.js",
                    WorkingDirectory = dashboardPath,
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                nodeProcess = Process.Start(psi);
                return true;
            } catch (Exception ex) {
                MessageBox.Show(
                    "Failed to start dashboard.\nEnsure Node.js is installed.\n\n" + ex.Message,
                    "NeuroPace Setup Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                Application.Exit();
                return false;
            }
        }

        private void OpenBrowser(string url) {
            try {
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            } catch (Exception) { }
        }

        private void OpenDashboard(object sender, EventArgs e) {
            OpenBrowser("http://localhost:3200");
        }

        private void Exit(object sender, EventArgs e) {
            trayIcon.Visible = false;
            if (nodeProcess != null && !nodeProcess.HasExited) {
                try {
                    Process.Start(new ProcessStartInfo {
                        FileName = "cmd.exe",
                        Arguments = "/C taskkill /PID " + nodeProcess.Id + " /T /F",
                        CreateNoWindow = true,
                        UseShellExecute = false
                    }).WaitForExit();
                } catch { }
            }
            Application.Exit();
        }
    }
}
