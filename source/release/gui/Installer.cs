using System;

using System.Collections.Generic;

using System.ComponentModel;

using System.Diagnostics;

using System.Drawing;

using System.Globalization;

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



    public static class UiText

    {

        static readonly Dictionary<string, Dictionary<string, string>> catalogs = CreateCatalogs();



        public static string Language { get; private set; }



        static UiText()

        {

            Select(null);

        }



        public static string Normalize(string name)

        {

            if (String.IsNullOrWhiteSpace(name)) return "en";

            name = name.Trim().ToLowerInvariant().Replace('_', '-');

            if (name == "zh" || name.StartsWith("zh-")) return "zh";

            if (name == "ja" || name.StartsWith("ja-")) return "ja";

            return "en";

        }



        public static void Select(string requested)

        {

            if (!String.IsNullOrWhiteSpace(requested)) {

                Language = Normalize(requested);

                return;

            }

            string configured = Environment.GetEnvironmentVariable("FGO_PATCH_UI_LANG");

            string culture = CultureInfo.CurrentUICulture == null ? null : CultureInfo.CurrentUICulture.Name;

            Language = Normalize(String.IsNullOrWhiteSpace(configured) ? culture : configured);

        }



        public static string Get(string key)

        {

            Dictionary<string, string> catalog;

            string value;

            if (catalogs.TryGetValue(Language, out catalog) && catalog.TryGetValue(key, out value))

                return value;

            if (catalogs["en"].TryGetValue(key, out value))

                return value;

            return key;

        }



        public static string Format(string key, params object[] args)

        {

            return String.Format(Get(key), args);

        }



        public static Font CreateFont(float size, FontStyle style)

        {

            string[] names;

            if (Language == "zh")

                names = new string[] { "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI" };

            else if (Language == "ja")

                names = new string[] { "Yu Gothic UI", "Meiryo UI", "Yu Gothic", "Meiryo", "Segoe UI" };

            else

                names = new string[] { "Segoe UI", "Microsoft YaHei UI" };

            foreach (string name in names)

            {

                try

                {

                    FontFamily family = new FontFamily(name);

                    if (family.IsStyleAvailable(style))

                        return new Font(family, size, style, GraphicsUnit.Point);

                    family.Dispose();

                }

                catch (ArgumentException)

                {

                }

            }

            return new Font(SystemFonts.MessageBoxFont.FontFamily, size, style, GraphicsUnit.Point);

        }



        static Dictionary<string, Dictionary<string, string>> CreateCatalogs()

        {

            Dictionary<string, Dictionary<string, string>> result = new Dictionary<string, Dictionary<string, string>>(StringComparer.Ordinal);

            result["en"] = English();

            result["zh"] = Chinese();

            result["ja"] = Japanese();

            return result;

        }



        static Dictionary<string, string> English()

        {

            Dictionary<string, string> t = NewCatalog();

            t["WindowTitle"] = "FGO Arcade AMD GPU Patch";

            t["Heading"] = "FGO Arcade  ·  AMD GPU Patch";

            t["Intro"] = "Choose the game folder, then install the patch. Close the game first.";

            t["PathLabel"] = "Game folder";

            t["Browse"] = "Browse…";

            t["Install"] = "Install / Update";

            t["Restore"] = "Restore Last Backup";

            t["RestoreConfirm"] = "This restores the patch files from before the latest installation.\r\n\r\n{0}";

            t["RestoreCaption"] = "Restore Backup";

            t["DetailsHint"] = "Existing files are backed up automatically. After it finishes, start the game from your usual launcher.";

            t["Footer"] = "2026.09.22  ·  64-bit Windows  ·  Built-in 60 FPS cap";

            t["CloseWhileBusy"] = "Files are still being processed. Close the window after it finishes.";

            t["BrowseDescription"] = "Select the folder that contains ago.exe, or its parent folder that contains App.";

            t["ChooseDirectory"] = "Choose a game folder.";

            t["GameFound"] = "Found game: {0}";

            t["MissingAgo"] = "ago.exe was not found. Choose the game folder, or its parent folder that contains App.";

            t["IncompletePackage"] = "The patch files are incomplete. Extract the full archive, then run the installer again.";

            t["InstallerDidNotFinish"] = "The installer did not finish normally.";

            t["InvalidInstallerResult"] = "The installer returned an invalid result.";

            t["Incomplete"] = "Not completed. See the details below.";

            t["DetailsName"] = "Installation result";

            t["CloseGame"] = "The game is still running. Close it, then try again.";

            t["PackageHashMismatch"] = "The patch files failed verification. Extract the full archive again, then retry.";

            t["NoBackup"] = "No restore backup was found in the selected game folder.";

            t["BackupHashMismatch"] = "The backup failed verification, so nothing was restored.";

            t["Installing"] = "Backing up and installing…";

            t["Restoring"] = "Restoring the backup…";

            t["Working"] = "Please wait. The patch files in the selected game folder are being processed.";

            t["InstallDoneStatus"] = "Installation finished. You can start the game from your usual launcher.";

            t["RestoreDoneStatus"] = "The previous patch files have been restored.";

            t["InstallDoneDetails"] = "The patch was installed and the original files were backed up.";

            t["RestoreDoneDetails"] = "The files from before the last installation have been restored.";

            t["GameDirectoryLabel"] = "Game folder:";

            t["BackupDirectoryLabel"] = "Backup folder:";

            return t;

        }



        static Dictionary<string, string> Chinese()

        {

            Dictionary<string, string> t = NewCatalog();

            t["WindowTitle"] = "FGO Arcade A卡补丁";

            t["Heading"] = "FGO Arcade  ·  A卡补丁";

            t["Intro"] = "选择游戏目录，然后安装补丁。安装前请先退出游戏。";

            t["PathLabel"] = "游戏目录";

            t["Browse"] = "浏览…";

            t["Install"] = "安装 / 更新";

            t["Restore"] = "恢复上次备份";

            t["RestoreConfirm"] = "将恢复最近一次安装前的补丁文件。\r\n\r\n{0}";

            t["RestoreCaption"] = "恢复备份";

            t["DetailsHint"] = "安装时会自动备份原有文件。完成后，可以从原来的启动器进入游戏。";

            t["Footer"] = "2026.09.22  ·  Windows 64 位  ·  内置 60 FPS 配置";

            t["CloseWhileBusy"] = "正在处理文件，请等待完成后关闭。";

            t["BrowseDescription"] = "请选择包含 ago.exe 的游戏目录，也可以选择其包含 App 的上一级目录。";

            t["ChooseDirectory"] = "请选择游戏目录。";

            t["GameFound"] = "已找到游戏：{0}";

            t["MissingAgo"] = "未找到 ago.exe。请选择游戏目录，或其包含 App 的上一级目录。";

            t["IncompletePackage"] = "补丁文件不完整。请完整解压压缩包，再运行安装器。";

            t["InstallerDidNotFinish"] = "安装程序未正常完成。";

            t["InvalidInstallerResult"] = "安装程序返回了无效结果。";

            t["Incomplete"] = "未完成，请查看下方说明。";

            t["DetailsName"] = "安装结果";

            t["CloseGame"] = "游戏仍在运行。请退出游戏后重试。";

            t["PackageHashMismatch"] = "补丁文件校验失败。请重新完整解压补丁包，再重试。";

            t["NoBackup"] = "所选游戏目录中没有可恢复的备份。";

            t["BackupHashMismatch"] = "备份文件校验失败，尚未执行恢复。";

            t["Installing"] = "正在备份并安装…";

            t["Restoring"] = "正在恢复备份…";

            t["Working"] = "请稍候，正在处理所选游戏目录中的补丁文件。";

            t["InstallDoneStatus"] = "安装完成，可以从原来的启动器进入游戏。";

            t["RestoreDoneStatus"] = "已恢复安装前的补丁文件。";

            t["InstallDoneDetails"] = "补丁安装成功，原有文件已备份。";

            t["RestoreDoneDetails"] = "已恢复上次安装前的文件。";

            t["GameDirectoryLabel"] = "游戏目录：";

            t["BackupDirectoryLabel"] = "备份目录：";

            return t;

        }



        static Dictionary<string, string> Japanese()

        {

            Dictionary<string, string> t = NewCatalog();

            t["WindowTitle"] = "FGO Arcade AMD GPUパッチ";

            t["Heading"] = "FGO Arcade  ·  AMD GPUパッチ";

            t["Intro"] = "ゲームフォルダーを選んでからパッチをインストールしてください。先にゲームを終了してください。";

            t["PathLabel"] = "ゲームフォルダー";

            t["Browse"] = "参照…";

            t["Install"] = "インストール / 更新";

            t["Restore"] = "前回のバックアップを復元";

            t["RestoreConfirm"] = "直前のインストール前のパッチファイルを復元します。\r\n\r\n{0}";

            t["RestoreCaption"] = "バックアップの復元";

            t["DetailsHint"] = "インストール時に既存ファイルは自動でバックアップされます。完了後は、いつものランチャーからゲームを起動できます。";

            t["Footer"] = "2026.09.22  ·  64ビット Windows  ·  内蔵 60 FPS 制限";

            t["CloseWhileBusy"] = "ファイルを処理中です。完了してから閉じてください。";

            t["BrowseDescription"] = "ago.exe があるゲームフォルダー、または App を含む親フォルダーを選んでください。";

            t["ChooseDirectory"] = "ゲームフォルダーを選んでください。";

            t["GameFound"] = "ゲームを検出しました: {0}";

            t["MissingAgo"] = "ago.exe が見つかりません。ゲームフォルダー、または App を含む親フォルダーを選んでください。";

            t["IncompletePackage"] = "パッチファイルが不完全です。アーカイブをすべて展開してから、インストーラーを実行してください。";

            t["InstallerDidNotFinish"] = "インストーラーが正常に完了しませんでした。";

            t["InvalidInstallerResult"] = "インストーラーが無効な結果を返しました。";

            t["Incomplete"] = "完了しませんでした。下の説明を確認してください。";

            t["DetailsName"] = "インストール結果";

            t["CloseGame"] = "ゲームが実行中です。終了してからやり直してください。";

            t["PackageHashMismatch"] = "パッチファイルの検証に失敗しました。アーカイブをすべて展開し直してからやり直してください。";

            t["NoBackup"] = "選択したゲームフォルダーに復元できるバックアップがありません。";

            t["BackupHashMismatch"] = "バックアップの検証に失敗したため、復元は実行していません。";

            t["Installing"] = "バックアップしてインストールしています…";

            t["Restoring"] = "バックアップを復元しています…";

            t["Working"] = "しばらくお待ちください。選択したゲームフォルダーのパッチファイルを処理しています。";

            t["InstallDoneStatus"] = "インストールが完了しました。いつものランチャーからゲームを起動できます。";

            t["RestoreDoneStatus"] = "インストール前のパッチファイルを復元しました。";

            t["InstallDoneDetails"] = "パッチのインストールに成功し、元のファイルをバックアップしました。";

            t["RestoreDoneDetails"] = "前回のインストール前のファイルを復元しました。";

            t["GameDirectoryLabel"] = "ゲームフォルダー:";

            t["BackupDirectoryLabel"] = "バックアップフォルダー:";

            return t;

        }



        static Dictionary<string, string> NewCatalog()

        {

            return new Dictionary<string, string>(StringComparer.Ordinal);

        }

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

                throw new InvalidOperationException(UiText.Get("MissingAgo"));

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

                throw new FileNotFoundException(UiText.Get("IncompletePackage"));

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

                    throw new InvalidOperationException(UiText.Get("InstallerDidNotFinish") + "\r\n" + errors.ToString() + output);

                }

                if (result == null || (process.ExitCode != 0 && result.ok))

                    throw new InvalidOperationException(UiText.Get("InvalidInstallerResult") + "\r\n" + errors.ToString());

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

            Text = UiText.Get("WindowTitle");

            Font = UiText.CreateFont(10F, FontStyle.Regular);

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

            title.Text = UiText.Get("Heading");

            title.Font = UiText.CreateFont(18F, FontStyle.Bold);

            title.AutoSize = true;

            layout.Controls.Add(title, 0, 0);

            Label intro = new Label();

            intro.Text = UiText.Get("Intro");

            intro.AutoSize = true;

            intro.ForeColor = Color.FromArgb(85, 90, 98);

            layout.Controls.Add(intro, 0, 1);

            Label pathLabel = new Label();

            pathLabel.Text = UiText.Get("PathLabel");

            pathLabel.AutoSize = true;

            layout.Controls.Add(pathLabel, 0, 2);



            TableLayoutPanel pathRow = new TableLayoutPanel();

            pathRow.Dock = DockStyle.Fill;

            pathRow.Margin = Padding.Empty;

            pathRow.ColumnCount = 2;

            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));

            directory.Dock = DockStyle.Fill;

            directory.AccessibleName = UiText.Get("PathLabel");

            directory.TextChanged += delegate { UpdateSelection(); };

            browse.Text = UiText.Get("Browse");

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

            install.Text = UiText.Get("Install");

            install.AutoSize = true;

            install.MinimumSize = new Size(156, 34);

            install.Padding = new Padding(12, 4, 12, 4);

            install.BackColor = Color.FromArgb(30, 103, 197);

            install.ForeColor = Color.White;

            install.FlatStyle = FlatStyle.Flat;

            install.FlatAppearance.BorderSize = 0;

            install.Click += delegate { RunAction("Install"); };

            restore.Text = UiText.Get("Restore");

            restore.AutoSize = true;

            restore.MinimumSize = new Size(156, 34);

            restore.Padding = new Padding(12, 4, 12, 4);

            restore.Margin = new Padding(12, 3, 0, 3);

            restore.Click += delegate {

                string app;

                try { app = InstallerPaths.GameDirectory(directory.Text); }

                catch (Exception e) { ShowFailure(e.Message); return; }

                if (MessageBox.Show(this, UiText.Format("RestoreConfirm", app),

                    UiText.Get("RestoreCaption"), MessageBoxButtons.OKCancel, MessageBoxIcon.Question) == DialogResult.OK)

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

            details.Text = UiText.Get("DetailsHint");

            details.AccessibleName = UiText.Get("DetailsName");

            layout.Controls.Add(details, 0, 7);

            Label footer = new Label();

            footer.Text = UiText.Get("Footer");

            footer.AutoSize = true;

            footer.ForeColor = Color.FromArgb(100, 105, 114);

            footer.Margin = new Padding(0, 8, 0, 0);

            layout.Controls.Add(footer, 0, 8);

            AcceptButton = install;

            FormClosing += delegate(object sender, FormClosingEventArgs e) {

                if (busy) {

                    e.Cancel = true;

                    status.Text = UiText.Get("CloseWhileBusy");

                }

            };

            UpdateSelection();

        }



        void SelectDirectory(object sender, EventArgs e)

        {

            using (FolderBrowserDialog dialog = new FolderBrowserDialog())

            {

                dialog.Description = UiText.Get("BrowseDescription");

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

                status.Text = UiText.Get("ChooseDirectory");

                return;

            }

            try {

                string app = InstallerPaths.GameDirectory(directory.Text);

                install.Enabled = true;

                restore.Enabled = Directory.Exists(Path.Combine(app, "shim-backups"));

                status.Text = UiText.Format("GameFound", app);

                status.ForeColor = Color.FromArgb(36, 116, 70);

            } catch (Exception e) {

                status.Text = e.Message;

            }

        }



        void ShowFailure(string message)

        {

            status.Text = UiText.Get("Incomplete");

            status.ForeColor = Color.FromArgb(170, 45, 40);

            if (message.Contains("Close the game"))

                message = UiText.Get("CloseGame");

            else if (message.Contains("Package hash mismatch"))

                message = UiText.Get("PackageHashMismatch") + "\r\n\r\n" + message;

            else if (message.Contains("No portable-release backup"))

                message = UiText.Get("NoBackup");

            else if (message.Contains("Backup hash mismatch"))

                message = UiText.Get("BackupHashMismatch") + "\r\n\r\n" + message;

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

            status.Text = action == "Install" ? UiText.Get("Installing") : UiText.Get("Restoring");

            details.Text = UiText.Get("Working");

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

                        status.Text = action == "Install" ? UiText.Get("InstallDoneStatus") : UiText.Get("RestoreDoneStatus");

                        status.ForeColor = Color.FromArgb(36, 116, 70);

                        details.Text = (action == "Install" ? UiText.Get("InstallDoneDetails") : UiText.Get("RestoreDoneDetails")) +

                            "\r\n\r\n" + UiText.Get("GameDirectoryLabel") + " " + result.gameApp +

                            "\r\n" + UiText.Get("BackupDirectoryLabel") + " " + result.backup;

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

            UiText.Select(null);

            Application.EnableVisualStyles();

            Application.SetCompatibleTextRenderingDefault(false);

            Application.Run(new InstallerForm());

        }

    }

}
