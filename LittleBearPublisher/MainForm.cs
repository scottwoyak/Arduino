namespace LittleBearPublisher;

/// <summary>
/// Lists firmware that is ready to publish and uploads the selected item or all of them.
/// Watches the repo's build output so the list updates as compiles finish.
/// </summary>
internal sealed class MainForm : Form
{
    private const int DebounceMs = 3000;

    private readonly string _repoRoot;
    private readonly HubClient _hub;
    private readonly FirmwareScanner _scanner;
    private readonly FileSystemWatcher _watcher;
    private readonly System.Windows.Forms.Timer _debounce = new() { Interval = DebounceMs };
    private readonly HashSet<string> _notified = [];

    private readonly ListView _list = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, MultiSelect = true, HideSelection = false };
    private readonly Button _refreshButton = new() { Text = "Refresh", AutoSize = true };
    private readonly Button _uploadSelectedButton = new() { Text = "Publish Selected", AutoSize = true, Enabled = false };
    private readonly Button _uploadAllButton = new() { Text = "Publish All", AutoSize = true, Enabled = false };
    private readonly TextBox _log = new() { Dock = DockStyle.Bottom, Height = 120, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly Icon _appIcon = LoadAppIcon();
    private readonly Font _readyFont;
    private readonly NotifyIcon _tray = new() { Visible = true };

    private List<FirmwareItem> _items = [];
    private bool _busy;
    private bool _rescanRequested;
    private bool _exiting;
    private int _sortColumn = -1;
    private bool _sortAscending = true;

    public MainForm(string repoRoot, HubClient hub)
    {
        _repoRoot = repoRoot;
        _hub = hub;
        _scanner = new FirmwareScanner(repoRoot);
        _readyFont = new Font(_list.Font, FontStyle.Bold);

        Text = "Little Bear Publisher";
        Icon = _appIcon;
        _tray.Icon = _appIcon;
        Width = 900;
        Height = 560;
        StartPosition = FormStartPosition.CenterScreen;

        _list.Columns.Add("Sketch", 190);
        _list.Columns.Add("Board", 230);
        _list.Columns.Add("Version", 90);
        _list.Columns.Add("Built", 140);
        _list.Columns.Add("Status", 90);
        _list.Columns.Add("Detail", 300);
        RestoreBounds();

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(4), WrapContents = false };
        buttons.Controls.AddRange([_refreshButton, _uploadSelectedButton, _uploadAllButton]);

        Controls.Add(_list);
        Controls.Add(_log);
        Controls.Add(buttons);

        var menu = new ContextMenuStrip();
        menu.Items.Add("Open", null, (_, _) => ShowWindow());
        menu.Items.Add("Exit", null, (_, _) => { _exiting = true; Close(); });
        _tray.ContextMenuStrip = menu;
        _tray.Text = "Little Bear Publisher";
        _tray.DoubleClick += (_, _) => ShowWindow();
        _tray.BalloonTipClicked += (_, _) => ShowWindow();

        _refreshButton.Click += async (_, _) => await RefreshAsync();
        _uploadSelectedButton.Click += async (_, _) => await UploadAsync(SelectedReadyItems());
        _uploadAllButton.Click += async (_, _) => await UploadAsync(_items.Where(i => i.Status == FirmwareStatus.Ready).ToList());
        _list.SelectedIndexChanged += (_, _) => UpdateButtons();
        _list.ColumnClick += (_, e) => SortBy(e.Column);

        ContextMenuStrip listMenu = new();
        ToolStripMenuItem uploadAnyway = new("Publish");
        uploadAnyway.Click += async (_, _) => await UploadAsync(SelectedUploadableItems());
        listMenu.Items.Add(uploadAnyway);
        listMenu.Opening += (_, e) =>
        {
            uploadAnyway.Enabled = !_busy;
            e.Cancel = SelectedUploadableItems().Count == 0;
        };
        _list.ContextMenuStrip = listMenu;

        _debounce.Tick += async (_, _) =>
        {
            _debounce.Stop();
            await RefreshAsync();
        };

        _watcher = new FileSystemWatcher(repoRoot)
        {
            IncludeSubdirectories = true,
            InternalBufferSize = 64 * 1024,
            NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.CreationTime,
        };
        _watcher.Changed += (_, e) => OnPathChanged(e.FullPath);
        _watcher.Created += (_, e) => OnPathChanged(e.FullPath);
        _watcher.Renamed += (_, e) => OnPathChanged(e.FullPath);
        _watcher.EnableRaisingEvents = true;

        Shown += async (_, _) => await RefreshAsync();
        Log($"Watching {repoRoot}; server {hub.Server}");
    }

    private static string SettingsPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "LittleBearPublisher",
        "window.json");

    private sealed record WindowSettings(int X, int Y, int Width, int Height, bool Maximized, int[]? ColumnWidths = null, int SortColumn = -1, bool SortAscending = true);

    /// <summary>
    /// Restores the last saved window position and size, if it is still on a connected screen.
    /// </summary>
    private void RestoreBounds()
    {
        try
        {
            if (!File.Exists(SettingsPath))
            {
                return;
            }

            WindowSettings? saved = System.Text.Json.JsonSerializer.Deserialize<WindowSettings>(File.ReadAllText(SettingsPath));
            if (saved is null)
            {
                return;
            }

            if (saved.ColumnWidths is not null && saved.ColumnWidths.Length == _list.Columns.Count)
            {
                for (int i = 0; i < saved.ColumnWidths.Length; i++)
                {
                    _list.Columns[i].Width = saved.ColumnWidths[i];
                }
            }

            if (saved.SortColumn >= 0 && saved.SortColumn < _list.Columns.Count)
            {
                _sortColumn = saved.SortColumn;
                _sortAscending = saved.SortAscending;
                UpdateSortHeaders();
            }

            if (saved.Width < 300 || saved.Height < 200)
            {
                return;
            }

            Rectangle bounds = new(saved.X, saved.Y, saved.Width, saved.Height);
            if (!Screen.AllScreens.Any(s => s.WorkingArea.IntersectsWith(bounds)))
            {
                return;
            }

            StartPosition = FormStartPosition.Manual;
            Bounds = bounds;
            if (saved.Maximized)
            {
                WindowState = FormWindowState.Maximized;
            }
        }
        catch (Exception ex) when (ex is IOException or System.Text.Json.JsonException or UnauthorizedAccessException)
        {
            Log($"Could not restore window size: {ex.Message}");
        }
    }

    /// <summary>
    /// Saves the current window position and size (the normal bounds when maximized or minimized).
    /// </summary>
    private void SaveBounds()
    {
        try
        {
            Rectangle bounds = WindowState == FormWindowState.Normal ? Bounds : base.RestoreBounds;
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);
            File.WriteAllText(
                SettingsPath,
                System.Text.Json.JsonSerializer.Serialize(
                    new WindowSettings(
                        bounds.X,
                        bounds.Y,
                        bounds.Width,
                        bounds.Height,
                        WindowState == FormWindowState.Maximized,
                        _list.Columns.Cast<ColumnHeader>().Select(c => c.Width).ToArray(),
                        _sortColumn,
                        _sortAscending)));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            Log($"Could not save window size: {ex.Message}");
        }
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        SaveBounds();

        if (!_exiting && e.CloseReason == CloseReason.UserClosing)
        {
            e.Cancel = true;
            Hide();
            return;
        }

        _watcher.Dispose();
        _tray.Visible = false;
        _tray.Dispose();
        base.OnFormClosing(e);
    }

    private static Icon LoadAppIcon()
    {
        using Stream stream = typeof(MainForm).Assembly.GetManifestResourceStream("littlebear.ico")!;

        return new Icon(stream);
    }

    private void ShowWindow()
    {
        Show();
        WindowState = FormWindowState.Normal;
        Activate();
    }

    private void OnPathChanged(string path)
    {
        bool relevant =
            (path.EndsWith(".ino.bin", StringComparison.OrdinalIgnoreCase) ||
             path.EndsWith(".ino", StringComparison.OrdinalIgnoreCase) ||
             path.EndsWith("LibraryVersion.h", StringComparison.OrdinalIgnoreCase)) &&
            !path.Contains($"{Path.DirectorySeparatorChar}_publish{Path.DirectorySeparatorChar}") &&
            !path.StartsWith(Path.Combine(_repoRoot, "LittleBearPublisher") + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

        if (relevant && IsHandleCreated)
        {
            BeginInvoke(() =>
            {
                _debounce.Stop();
                _debounce.Start();
            });
        }
    }

    private async Task RefreshAsync()
    {
        if (_busy)
        {
            _rescanRequested = true;
            return;
        }

        _busy = true;
        UpdateButtons();
        try
        {
            do
            {
                _rescanRequested = false;
                _items = await _scanner.ScanAsync(_hub, false);
                PopulateList();
                NotifyNewReady();
            }
            while (_rescanRequested);
        }
        catch (Exception ex)
        {
            Log($"Scan failed: {ex.Message}");
        }
        finally
        {
            _busy = false;
            UpdateButtons();
        }
    }

    /// <summary>
    /// Sorts by the clicked column; clicking the same column again reverses the order.
    /// </summary>
    /// <param name="column">Index of the clicked column.</param>
    private void SortBy(int column)
    {
        _sortAscending = column != _sortColumn || !_sortAscending;
        _sortColumn = column;
        UpdateSortHeaders();
        PopulateList();
    }

    private void UpdateSortHeaders()
    {
        string[] titles = ["Sketch", "Board", "Version", "Built", "Status", "Detail"];
        for (int i = 0; i < _list.Columns.Count; i++)
        {
            _list.Columns[i].Text = i == _sortColumn ? $"{titles[i]} {(_sortAscending ? "\u25B2" : "\u25BC")}" : titles[i];
        }
    }

    private IEnumerable<FirmwareItem> SortedItems()
    {
        if (_sortColumn < 0)
        {
            return _items;
        }

        Comparison<FirmwareItem> compare = _sortColumn switch
        {
            1 => (a, b) => string.Compare(a.Board, b.Board, StringComparison.OrdinalIgnoreCase),
            2 => (a, b) => CompareVersions(a.Version, b.Version),
            3 => (a, b) => a.Built.CompareTo(b.Built),
            4 => (a, b) => a.Status.CompareTo(b.Status),
            5 => (a, b) => string.Compare(a.Detail, b.Detail, StringComparison.OrdinalIgnoreCase),
            _ => (a, b) => string.Compare(a.Sketch, b.Sketch, StringComparison.OrdinalIgnoreCase),
        };

        int direction = _sortAscending ? 1 : -1;

        // List.Sort is unstable, so use OrderBy to keep equal rows in their scan order.
        return _items.OrderBy(i => i, Comparer<FirmwareItem>.Create((a, b) => direction * compare(a, b)));
    }

    private static int CompareVersions(string a, string b)
    {
        return Version.TryParse(a, out Version? va) && Version.TryParse(b, out Version? vb)
            ? va.CompareTo(vb)
            : string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
    }

    private void PopulateList()
    {
        _list.BeginUpdate();
        _list.Items.Clear();
        foreach (var item in SortedItems())
        {
            var row = new ListViewItem(item.Sketch) { Tag = item };
            row.SubItems.Add(item.Board);
            row.SubItems.Add(item.Version);
            row.SubItems.Add(item.Status == FirmwareStatus.Skipped ? "" : DateText.Format(item.Built));
            row.SubItems.Add(item.Status.ToString());
            row.SubItems.Add(item.Detail);
            if (item.Status == FirmwareStatus.Ready)
            {
                row.ForeColor = Color.DarkGreen;
                row.Font = _readyFont;
            }
            else
            {
                row.ForeColor = Color.Gray;
            }
            _list.Items.Add(row);
        }
        _list.EndUpdate();

        int ready = _items.Count(i => i.Status == FirmwareStatus.Ready);
        _tray.Text = ready == 0 ? "Little Bear Publisher: nothing to upload" : $"Little Bear Publisher: {ready} ready";
        UpdateButtons();
    }

    private void NotifyNewReady()
    {
        var fresh = _items.Where(i => i.Status == FirmwareStatus.Ready && _notified.Add(i.Key)).ToList();
        if (fresh.Count > 0 && !Visible)
        {
            _tray.ShowBalloonTip(
                5000,
                "Firmware ready to upload",
                string.Join(", ", fresh.Select(i => $"{i.Firmware} {i.Version}")),
                ToolTipIcon.Info);
        }
    }

    private List<FirmwareItem> SelectedUploadableItems()
    {
        return _list.SelectedItems
            .Cast<ListViewItem>()
            .Select(r => (FirmwareItem)r.Tag!)
            .Where(i => i.BinPath.Length > 0)
            .ToList();
    }

    private List<FirmwareItem> SelectedReadyItems()
    {
        return _list.SelectedItems
            .Cast<ListViewItem>()
            .Select(r => (FirmwareItem)r.Tag!)
            .Where(i => i.Status == FirmwareStatus.Ready)
            .ToList();
    }

    private void UpdateButtons()
    {
        _refreshButton.Enabled = !_busy;
        _uploadSelectedButton.Enabled = !_busy && SelectedReadyItems().Count > 0;
        _uploadAllButton.Enabled = !_busy && _items.Any(i => i.Status == FirmwareStatus.Ready);
    }

    private async Task UploadAsync(List<FirmwareItem> items)
    {
        if (_busy || items.Count == 0)
        {
            return;
        }

        _busy = true;
        UpdateButtons();
        try
        {
            foreach (var item in items)
            {
                Log($"Uploading {item.Firmware} {item.Version}...");
                var (ok, detail) = await _hub.UploadAsync(item, null);
                Log(ok ? $"OK     {item.Firmware} {item.Version}" : $"FAILED {item.Firmware}: {detail}");
            }
        }
        finally
        {
            _busy = false;
        }

        await RefreshAsync();
    }

    private void Log(string message)
    {
        _log.AppendText($"{DateTime.Now:T}  {message}{Environment.NewLine}");
    }
}
