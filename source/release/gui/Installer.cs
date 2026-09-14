using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace FgoPatch
{
    public sealed class ActionResult
    {
        public bool ok { get; set; }
        public string message { get; set; }
        public string backup { get; set; }
        public string gameApp { get; set; }
        public string sha256 { get; set; }
    }

    public static class InstallerPaths
    {
        public static string GameDirectory(string input)
        {
            string path = Path.GetFullPath(input.Trim().Trim('"'));
            if (File.Exists(path) && String.Equals(Path.GetFileName(path), "ago.exe", StringComparison.OrdinalIgnoreCase))
                path = Path.GetDirectoryName(path);
            if (!File.Exists(Path.Combine(path, "ago.exe")) && File.Exists(Path.Combine(path, "App", "ago.exe")))
                path = Path.Combine(path, "App");
            if (!File.Exists(Path.Combine(path, "ago.exe")))
                throw new InvalidOperationException("未找到 ago.exe。请选择游戏目录，或其包含 App 的上一级目录。");
            return path;
        }

        // Windows argv quoting, including trailing backslashes. These arguments
        // are passed to powershell.exe -File, never evaluated as a command string.
        public static string Quote(string value)
        {
            StringBuilder result = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in value)
            {
                if (c == '\\') { ++slashes; continue; }
                result.Append('\\', c == '"' ? slashes * 2 + 1 : slashes);
                result.Append(c);
                slashes = 0;
            }
            result.Append('\\', slashes * 2);
            return result.Append('"').ToString();
        }

        public static ActionResult Run(string package, string action, string app)
        {
            string script = Path.Combine(package, "game-patch", "gui-action.ps1");
            if (!File.Exists(script))
                throw new FileNotFoundException("补丁文件不完整。请完整解压压缩包，再运行安装器。");
            string powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                @"WindowsPowerShell\v1.0\powershell.exe");
            string[] args = { "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                "-File", script, "-Action", action, "-GameApp", app };
            ProcessStartInfo info = new ProcessStartInfo(powershell, String.Join(" ", args.Select(Quote).ToArray()));
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            info.WindowStyle = ProcessWindowStyle.Hidden;
            info.WorkingDirectory = package;
            info.RedirectStandardOutput = true;
            info.RedirectStandardError = true;
            info.StandardOutputEncoding = Encoding.UTF8;
            info.StandardErrorEncoding = Encoding.UTF8;
            StringBuilder errors = new StringBuilder();
            using (Process process = new Process())
            {
                process.StartInfo = info;
                process.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs e) {
                    if (e.Data != null) errors.AppendLine(e.Data);
                };
                process.Start();
                process.BeginErrorReadLine();
                string output = process.StandardOutput.ReadToEnd();
                process.WaitForExit();
                ActionResult result;
                try { result = new JavaScriptSerializer().Deserialize<ActionResult>(output.Trim().Trim('\uFEFF')); }
                catch (Exception) {
                    throw new InvalidOperationException("安装程序未正常完成。\r\n" + errors.ToString() + output);
                }
                if (result == null || (process.ExitCode != 0 && result.ok))
                    throw new InvalidOperationException("安装程序返回了无效结果。\r\n" + errors.ToString());
                return result;
            }
        }
    }

    public sealed class InstallerForm : Form
    {
        readonly TextBox directory = new TextBox();
        readonly TextBox details = new TextBox();
        readonly Label status = new Label();
        readonly Button browse = new Button();
        readonly Button install = new Button();
        readonly Button restore = new Button();
        readonly ProgressBar progress = new ProgressBar();
        readonly string package = AppDomain.CurrentDomain.BaseDirectory;
        bool busy;

        public InstallerForm()
        {
            Text = "FGO Arcade A卡补丁";
            Font = new Font("Microsoft YaHei UI", 10F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(690, 450);
            MinimumSize = new Size(650, 460);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.White;
            MaximizeBox = false;

            TableLayoutPanel layout = new TableLayoutPanel();
            layout.Dock = DockStyle.Fill;
            layout.Padding = new Padding(24, 20, 24, 18);
            layout.ColumnCount = 1;
            layout.RowCount = 9;
            foreach (int height in new int[] { 40, 38, 25, 42, 36, 12, 48 })
                layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
            Controls.Add(layout);

            Label title = new Label();
            title.Text = "FGO Arcade  ·  A卡补丁";
            title.Font = new Font(Font.FontFamily, 18F, FontStyle.Bold);
            title.AutoSize = true;
            layout.Controls.Add(title, 0, 0);
            Label intro = new Label();
            intro.Text = "选择游戏目录，然后安装补丁。安装前请先退出游戏。";
            intro.AutoSize = true;
            intro.ForeColor = Color.FromArgb(85, 90, 98);
            layout.Controls.Add(intro, 0, 1);
            Label pathLabel = new Label();
            pathLabel.Text = "游戏目录";
            pathLabel.AutoSize = true;
            layout.Controls.Add(pathLabel, 0, 2);

            TableLayoutPanel pathRow = new TableLayoutPanel();
            pathRow.Dock = DockStyle.Fill;
            pathRow.Margin = Padding.Empty;
            pathRow.ColumnCount = 2;
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 112));
            directory.Dock = DockStyle.Fill;
            directory.AccessibleName = "游戏目录";
            directory.TextChanged += delegate { UpdateSelection(); };
            browse.Text = "浏览…";
            browse.Dock = DockStyle.Fill;
            browse.Margin = new Padding(10, 2, 0, 7);
            browse.Click += SelectDirectory;
            pathRow.Controls.Add(directory, 0, 0);
            pathRow.Controls.Add(browse, 1, 0);
            layout.Controls.Add(pathRow, 0, 3);

            status.Dock = DockStyle.Fill;
            status.AutoEllipsis = true;
            status.TextAlign = ContentAlignment.MiddleLeft;
            layout.Controls.Add(status, 0, 4);
            progress.Dock = DockStyle.Fill;
            progress.Visible = false;
            progress.Style = ProgressBarStyle.Marquee;
            layout.Controls.Add(progress, 0, 5);

            FlowLayoutPanel actions = new FlowLayoutPanel();
            actions.Dock = DockStyle.Fill;
            actions.Margin = new Padding(0, 6, 0, 0);
            install.Text = "安装 / 更新";
            install.Size = new Size(156, 34);
            install.BackColor = Color.FromArgb(30, 103, 197);
            install.ForeColor = Color.White;
            install.FlatStyle = FlatStyle.Flat;
            install.FlatAppearance.BorderSize = 0;
            install.Click += delegate { RunAction("Install"); };
            restore.Text = "恢复上次备份";
            restore.Size = new Size(156, 34);
            restore.Margin = new Padding(12, 3, 0, 3);
            restore.Click += delegate {
                string app;
                try { app = InstallerPaths.GameDirectory(directory.Text); }
                catch (Exception e) { ShowFailure(e.Message); return; }
                if (MessageBox.Show(this, "将恢复最近一次安装前的补丁文件。\r\n\r\n" + app,
                    "恢复备份", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) == DialogResult.OK)
                    RunAction("Restore");
            };
            actions.Controls.Add(install);
            actions.Controls.Add(restore);
            layout.Controls.Add(actions, 0, 6);

            details.Multiline = true;
            details.ReadOnly = true;
            details.ScrollBars = ScrollBars.Vertical;
            details.Dock = DockStyle.Fill;
            details.BackColor = Color.FromArgb(247, 248, 250);
            details.BorderStyle = BorderStyle.FixedSingle;
            details.Text = "安装时会自动备份原有文件。完成后，可从原来的启动器进入游戏。";
            details.AccessibleName = "安装结果";
            layout.Controls.Add(details, 0, 7);
            Label footer = new Label();
            footer.Text = "2026.09.14  ·  Windows 64 位  ·  内置 60 FPS 配置";
            footer.AutoSize = true;
            footer.ForeColor = Color.FromArgb(100, 105, 114);
            footer.Margin = new Padding(0, 8, 0, 0);
            layout.Controls.Add(footer, 0, 8);
            AcceptButton = install;
            FormClosing += delegate(object sender, FormClosingEventArgs e) {
                if (busy) {
                    e.Cancel = true;
                    status.Text = "正在处理文件，请等待完成后关闭。";
                }
            };
            UpdateSelection();
        }

        void SelectDirectory(object sender, EventArgs e)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "请选择包含 ago.exe 的游戏目录，也可以选择其包含 App 的上一级目录。";
                dialog.ShowNewFolderButton = false;
                if (Directory.Exists(directory.Text)) dialog.SelectedPath = directory.Text;
                if (dialog.ShowDialog(this) == DialogResult.OK) directory.Text = dialog.SelectedPath;
            }
        }

        void UpdateSelection()
        {
            if (busy) return;
            install.Enabled = restore.Enabled = false;
            status.ForeColor = Color.FromArgb(90, 95, 105);
            if (String.IsNullOrWhiteSpace(directory.Text)) {
                status.Text = "请选择游戏目录。";
                return;
            }
            try {
                string app = InstallerPaths.GameDirectory(directory.Text);
                install.Enabled = true;
                restore.Enabled = Directory.Exists(Path.Combine(app, "shim-backups"));
                status.Text = "已找到游戏：" + app;
                status.ForeColor = Color.FromArgb(36, 116, 70);
            } catch (Exception e) {
                status.Text = e.Message;
            }
        }

        void ShowFailure(string message)
        {
            status.Text = "未完成，请查看下方说明。";
            status.ForeColor = Color.FromArgb(170, 45, 40);
            if (message.Contains("Close the game"))
                message = "游戏仍在运行。请退出游戏后重试。";
            else if (message.Contains("Package hash mismatch"))
                message = "补丁文件校验失败。请重新完整解压补丁包，再重试。\r\n\r\n" + message;
            else if (message.Contains("No portable-release backup"))
                message = "所选游戏目录中没有可恢复的备份。";
            else if (message.Contains("Backup hash mismatch"))
                message = "备份文件校验失败，尚未执行恢复。\r\n\r\n" + message;
            details.Text = message;
        }

        void RunAction(string action)
        {
            string app;
            try { app = InstallerPaths.GameDirectory(directory.Text); }
            catch (Exception e) { ShowFailure(e.Message); return; }
            directory.Text = app;
            busy = true;
            directory.Enabled = browse.Enabled = install.Enabled = restore.Enabled = false;
            progress.Visible = true;
            status.ForeColor = Color.FromArgb(30, 103, 197);
            status.Text = action == "Install" ? "正在备份并安装…" : "正在恢复备份…";
            details.Text = "请稍候，正在处理所选游戏目录中的补丁文件。";
            BackgroundWorker worker = new BackgroundWorker();
            worker.DoWork += delegate(object sender, DoWorkEventArgs e) {
                e.Result = InstallerPaths.Run(package, action, app);
            };
            worker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs e) {
                busy = false;
                directory.Enabled = browse.Enabled = true;
                progress.Visible = false;
                UpdateSelection();
                if (e.Error != null) ShowFailure(e.Error.Message);
                else {
                    ActionResult result = (ActionResult)e.Result;
                    if (!result.ok) ShowFailure(result.message);
                    else {
                        status.Text = action == "Install" ? "安装完成，可以从原来的启动器进入游戏。" : "已恢复安装前的补丁文件。";
                        status.ForeColor = Color.FromArgb(36, 116, 70);
                        details.Text = (action == "Install" ? "补丁安装成功，原有文件已备份。" : "已恢复上次安装前的文件。") +
                            "\r\n\r\n游戏目录：" + result.gameApp +
                            "\r\n备份目录：" + result.backup;
                    }
                }
                worker.Dispose();
            };
            worker.RunWorkerAsync();
        }
    }

    static class Program
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new InstallerForm());
        }
    }
}
