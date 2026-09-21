#if GAME_STUB
class GameStub { static void Main() { System.Threading.Thread.Sleep(30000); } }
#else
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using FgoPatch;

class GuiReleaseTest
{
    static readonly List<string> passed = new List<string>();
    static string package;
    static string run;
    static string rendererHash;
    static string systemHash;

    static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    static string Hash(string path)
    {
        using (SHA256 algorithm = SHA256.Create())
        using (FileStream stream = File.OpenRead(path))
            return BitConverter.ToString(algorithm.ComputeHash(stream)).Replace("-", "");
    }

    static string MakeApp(string name, bool existing)
    {
        string app = Path.Combine(run, name, "App");
        Directory.CreateDirectory(app);
        File.WriteAllText(Path.Combine(app, "ago.exe"), "installer test fixture, not a real game");
        File.WriteAllText(Path.Combine(app, "segatools.ini"), "original user settings");
        if (existing) {
            File.WriteAllText(Path.Combine(app, "opengl32.dll"), "previous renderer");
            File.WriteAllText(Path.Combine(app, "opengl32real.dll"), "previous forwarder");
            Directory.CreateDirectory(Path.Combine(app, "amdcfg"));
            File.WriteAllText(Path.Combine(app, "amdcfg", "amdOglpSettings.cfg"), "previous AMD settings");
        }
        return app;
    }

    static ActionResult Apply(string action, string app, bool expectedSuccess)
    {
        ActionResult result = InstallerPaths.Run(package, action, app);
        Check(result.ok == expectedSuccess, action + " unexpected result: " + result.message);
        return result;
    }

    static void CheckInstalled(string app)
    {
        Check(Hash(Path.Combine(app, "opengl32.dll")) == rendererHash, "Installed renderer differs");
        Check(Hash(Path.Combine(app, "opengl32real.dll")) == systemHash, "Not the user's system OpenGL DLL");
        Check(Hash(Path.Combine(app, "amdcfg", "amdOglpSettings.cfg")) ==
            Hash(Path.Combine(package, "game-patch", "amdcfg", "amdOglpSettings.cfg")), "AMD settings differ");
        Check(File.ReadAllText(Path.Combine(app, "segatools.ini")) == "original user settings", "Game settings changed");
        Check(Directory.GetFiles(app, "*.on", SearchOption.AllDirectories).Length == 0, "Marker files were installed");
    }


    static Button FindButton(Control root, string text)
    {
        Button button = root as Button;
        if (button != null && button.Text == text) return button;
        foreach (Control child in root.Controls) {
            Button found = FindButton(child, text);
            if (found != null) return found;
        }
        return null;
    }

    static IntPtr RealizePreviewHandles(Control control)
    {
        IntPtr handle = control.Handle;
        foreach (Control child in control.Controls) RealizePreviewHandles(child);
        control.PerformLayout();
        return handle;
    }

    [STAThread]
    static int Main(string[] args)
    {
        try {
            package = Path.GetFullPath(args[0]);
            run = Path.GetFullPath(args[1]);
            Check(!File.Exists(Path.Combine(package, "release.json")), "Obsolete release manifest is present");
            Directory.CreateDirectory(run);
            rendererHash = Hash(Path.Combine(package, "game-patch", "opengl32.dll"));
            systemHash = Hash(Path.Combine(Environment.SystemDirectory, "opengl32.dll"));

            string fresh = MakeApp("fresh", false);
            Apply("Install", fresh, true);
            CheckInstalled(fresh);
            Apply("Restore", fresh, true);
            Check(!File.Exists(Path.Combine(fresh, "opengl32.dll")) && !File.Exists(Path.Combine(fresh, "opengl32real.dll")), "Fresh restore retained DLLs");
            Check(!Directory.Exists(Path.Combine(fresh, "amdcfg")), "Fresh restore retained AMD settings");
            passed.Add("fresh_install_and_restore");

            string unicode = MakeApp("中文路径 [方括号] 空格 & $() 单引号'", true);
            Check(InstallerPaths.GameDirectory(Path.GetDirectoryName(unicode)) == unicode, "Parent/App selection failed");
            Check(InstallerPaths.GameDirectory(Path.Combine(unicode, "ago.exe")) == unicode, "ago.exe selection failed");
            string beforeRenderer = Hash(Path.Combine(unicode, "opengl32.dll"));
            string beforeForwarder = Hash(Path.Combine(unicode, "opengl32real.dll"));
            Apply("Install", unicode + Path.DirectorySeparatorChar, true);
            CheckInstalled(unicode);
            Apply("Restore", unicode, true);
            Check(Hash(Path.Combine(unicode, "opengl32.dll")) == beforeRenderer && Hash(Path.Combine(unicode, "opengl32real.dll")) == beforeForwarder, "Old DLLs were not restored exactly");
            Check(File.ReadAllText(Path.Combine(unicode, "amdcfg", "amdOglpSettings.cfg")) == "previous AMD settings", "AMD settings were not restored");
            passed.Add("upgrade_restore_unicode_spaces_metacharacters_trailing_slash");

            string invalid = Path.Combine(run, "not a game");
            Directory.CreateDirectory(invalid);
            Apply("Install", invalid, false);
            Check(Directory.GetFileSystemEntries(invalid).Length == 0, "Invalid game directory was modified");
            Apply("Restore", MakeApp("no backup", false), false);
            passed.Add("invalid_directory_and_missing_backup_rejected");

            string corruptPackage = Path.Combine(run, "corrupt package");
            Directory.CreateDirectory(Path.Combine(corruptPackage, "game-patch"));
            File.Copy(Path.Combine(package, "SHA256SUMS.txt"), Path.Combine(corruptPackage, "SHA256SUMS.txt"));
            foreach (string file in Directory.GetFiles(Path.Combine(package, "game-patch"), "*", SearchOption.AllDirectories)) {
                string destination = Path.Combine(corruptPackage, file.Substring(package.TrimEnd('\\').Length + 1));
                Directory.CreateDirectory(Path.GetDirectoryName(destination));
                File.Copy(file, destination);
            }
            File.AppendAllText(Path.Combine(corruptPackage, "game-patch", "opengl32.dll"), "corrupt");
            string corruptTarget = MakeApp("corrupt input target", true);
            ActionResult rejected = InstallerPaths.Run(corruptPackage, "Install", corruptTarget);
            Check(!rejected.ok && rejected.message.Contains("Package hash mismatch"), "Corrupt renderer was accepted");
            Check(!Directory.Exists(Path.Combine(corruptTarget, "shim-backups")), "Corrupt package wrote a backup");
            Check(File.ReadAllText(Path.Combine(corruptTarget, "opengl32.dll")) == "previous renderer", "Corrupt package changed renderer");
            passed.Add("corrupt_payload_rejected_before_writes");

            string badBackup = MakeApp("corrupt backup", true);
            ActionResult installed = Apply("Install", badBackup, true);
            File.AppendAllText(Path.Combine(installed.backup, "dlls", "opengl32.dll"), "corrupt");
            rejected = Apply("Restore", badBackup, false);
            Check(rejected.message.Contains("Backup hash mismatch"), "Bad backup failure had the wrong cause");
            CheckInstalled(badBackup);
            passed.Add("corrupt_backup_rejected_before_restore_writes");

            string running = MakeApp("running process", true);
            File.Copy(args[2], Path.Combine(running, "ago.exe"), true);
            ProcessStartInfo start = new ProcessStartInfo(Path.Combine(running, "ago.exe"));
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            start.WindowStyle = ProcessWindowStyle.Hidden;
            using (Process stub = Process.Start(start)) {
                try {
                    rejected = Apply("Install", running, false);
                    Check(rejected.message.Contains("Close the game"), "Running game refusal had the wrong cause");
                    Check(!stub.HasExited, "Installer ended the process");
                    Check(!Directory.Exists(Path.Combine(running, "shim-backups")), "Running game directory was modified");
                    Check(File.ReadAllText(Path.Combine(running, "opengl32.dll")) == "previous renderer", "Running game's DLL changed");
                } finally {
                    // This is the fixture process created just above, never a user game.
                    if (!stub.HasExited) { stub.Kill(); stub.WaitForExit(); }
                }
            }
            passed.Add("running_game_rejected_without_stopping_process_or_writes");

            Check(UiText.Normalize(null) == "en", "null language defaults to English");
            Check(UiText.Normalize("") == "en", "empty language defaults to English");
            Check(UiText.Normalize("fr-FR") == "en", "unsupported language defaults to English");
            Check(UiText.Normalize("en-US") == "en", "en-US maps to English");
            Check(UiText.Normalize("zh") == "zh" && UiText.Normalize("zh-CN") == "zh" && UiText.Normalize("zh-TW") == "zh" && UiText.Normalize("zh-HK") == "zh", "Chinese cultures map to zh");
            Check(UiText.Normalize("ja") == "ja" && UiText.Normalize("ja-JP") == "ja", "Japanese cultures map to ja");
            passed.Add("ui_language_defaults_to_english_and_maps_zh_ja");

            var previousCulture = System.Threading.Thread.CurrentThread.CurrentUICulture;
            string previousLanguage = Environment.GetEnvironmentVariable("FGO_PATCH_UI_LANG");
            try {
                Environment.SetEnvironmentVariable("FGO_PATCH_UI_LANG", null);
                string[] cultures = { "en-US", "zh-CN", "zh-TW", "ja-JP", "fr-FR" };
                string[] expected = { "en", "zh", "zh", "ja", "en" };
                for (int i = 0; i < cultures.Length; ++i) {
                    System.Threading.Thread.CurrentThread.CurrentUICulture = new System.Globalization.CultureInfo(cultures[i]);
                    UiText.Select(null);
                    Check(UiText.Language == expected[i], cultures[i] + " automatic language selection");
                }
            } finally {
                System.Threading.Thread.CurrentThread.CurrentUICulture = previousCulture;
                Environment.SetEnvironmentVariable("FGO_PATCH_UI_LANG", previousLanguage);
                UiText.Select(null);
            }
            passed.Add("automatic_windows_ui_language_selection_and_english_fallback");

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            string[] languages = new string[] { "en", "zh", "ja" };
            foreach (string language in languages) {
                UiText.Select(language);
                using (InstallerForm form = new InstallerForm()) {
                    RealizePreviewHandles(form);
                    Check(form.Text == UiText.Get("WindowTitle"), language + " window title");
                    Check(FindButton(form, UiText.Get("Install")) != null, language + " install button");
                    Check(FindButton(form, UiText.Get("Restore")) != null, language + " restore button");
                    Check(FindButton(form, UiText.Get("Browse")) != null, language + " browse button");
                    using (Bitmap bitmap = new Bitmap(form.Width, form.Height)) {
                        form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, bitmap.Size));
                        bitmap.Save(Path.Combine(run, "gui-preview-" + language + ".png"));
                        if (language == "en") bitmap.Save(Path.Combine(run, "gui-preview.png"));
                    }
                }
            }
            UiText.Select(null);
            passed.Add("gui_constructed_and_rendered_without_showing_window");
            var report = new {
                passed = true, checks = passed, renderer_sha256 = rendererHash,
                user_system_opengl_sha256 = systemHash, test_directory = run,
                live_game_modified = false, marker_files_installed = false
            };
            File.WriteAllText(Path.Combine(run, "gui-validation.json"), new JavaScriptSerializer().Serialize(report), new UTF8Encoding(false));
            Console.WriteLine("GUI release integration passed: " + passed.Count + " groups");
            return 0;
        } catch (Exception e) {
            Console.Error.WriteLine(e.ToString());
            return 1;
        }
    }
}
#endif
