namespace LittleBearPublisher;

internal static class Program
{
    private const string DefaultServer = "https://devices.littlebear.dev";

    /// <summary>
    /// Optional arguments: --repo &lt;path&gt;, --server &lt;url&gt;, --token &lt;token&gt;.
    /// The repo root defaults to the nearest parent folder containing libraries\Woyak\LibraryVersion.h.
    /// </summary>
    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();

        string? repo = GetArg(args, "--repo") ?? FindRepoRoot();
        string server = GetArg(args, "--server") ?? DefaultServer;
        string? token = GetArg(args, "--token");

        if (repo == null || !File.Exists(Path.Combine(repo, "libraries", "Woyak", "LibraryVersion.h")))
        {
            MessageBox.Show(
                "Could not locate the Arduino repo. Run with --repo <path to repo root>.",
                "Little Bear Publisher",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return;
        }

        Application.Run(new MainForm(repo, new HubClient(server, token)));
    }

    private static string? GetArg(string[] args, string name)
    {
        int index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }

    private static string? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "libraries", "Woyak", "LibraryVersion.h")))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }
        return null;
    }
}
