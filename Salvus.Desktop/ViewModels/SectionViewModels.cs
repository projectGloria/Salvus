using System.Collections.ObjectModel;
using System.IO;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using Salvus.Data;
using Salvus.Desktop.Services;
using Salvus.Shared.Contracts;

namespace Salvus.Desktop.ViewModels;

public sealed record CapabilityRow(string Name, string Phase, bool Implemented, string StatusText, string Notes);

public sealed class DashboardViewModel : ViewModelBase
{
    private readonly IEngineIpcClient _ipcClient;
    private readonly ISalvusDatabase _database;

    private bool _isEngineConnected;
    private string _engineStatusText = "Not connected";
    private string _engineVersion = "—";
    private string _developmentPhase = "Phase 2 - Storage Access";
    private uint _engineProcessId;
    private string _lastPingUtc = "—";
    private int _physicalDiskCount;
    private int _volumeCount;
    private string _storageAccessSummary = "Waiting for engine...";
    private int _recentScanCount;
    private int _recentRecoveryCount;

    public bool IsEngineConnected
    {
        get => _isEngineConnected;
        private set => SetProperty(ref _isEngineConnected, value);
    }

    public string EngineStatusText
    {
        get => _engineStatusText;
        private set => SetProperty(ref _engineStatusText, value);
    }

    public string EngineVersion
    {
        get => _engineVersion;
        private set => SetProperty(ref _engineVersion, value);
    }

    public string DevelopmentPhase
    {
        get => _developmentPhase;
        private set => SetProperty(ref _developmentPhase, value);
    }

    public uint EngineProcessId
    {
        get => _engineProcessId;
        private set => SetProperty(ref _engineProcessId, value);
    }

    public string LastPingUtc
    {
        get => _lastPingUtc;
        private set => SetProperty(ref _lastPingUtc, value);
    }

    public int PhysicalDiskCount
    {
        get => _physicalDiskCount;
        private set => SetProperty(ref _physicalDiskCount, value);
    }

    public int VolumeCount
    {
        get => _volumeCount;
        private set => SetProperty(ref _volumeCount, value);
    }

    public string StorageAccessSummary
    {
        get => _storageAccessSummary;
        private set => SetProperty(ref _storageAccessSummary, value);
    }

    public int RecentScanCount
    {
        get => _recentScanCount;
        private set => SetProperty(ref _recentScanCount, value);
    }

    public int RecentRecoveryCount
    {
        get => _recentRecoveryCount;
        private set => SetProperty(ref _recentRecoveryCount, value);
    }

    public string PipeName => _ipcClient.PipeName;

    public ObservableCollection<CapabilityRow> Capabilities { get; } = new();

    public ICommand RefreshCommand { get; }

    public DashboardViewModel(IEngineIpcClient ipcClient, ISalvusDatabase database)
    {
        _ipcClient = ipcClient;
        _database = database;
        RefreshCommand = new AsyncRelayCommand(RefreshAsync);
    }

    public async Task RefreshAsync()
    {
        try
        {
            var sessions = await _database.GetRecentScanSessionsAsync(10);
            var recoveries = await _database.GetRecoveryHistoryAsync(10);
            RecentScanCount = sessions.Count;
            RecentRecoveryCount = recoveries.Count;
        }
        catch
        {
            RecentScanCount = 0;
            RecentRecoveryCount = 0;
        }

        try
        {
            bool connected = await _ipcClient.EnsureEngineStartedAndConnectedAsync();
            if (!connected)
            {
                IsEngineConnected = false;
                EngineStatusText = "Engine executable not found or failed to start. Build Salvus.Engine first.";
                return;
            }

            PingResult ping = await _ipcClient.PingAsync();
            EngineInfo info = await _ipcClient.GetEngineInfoAsync();
            EngineCapabilities caps = await _ipcClient.GetCapabilitiesAsync();
            DeviceListResult devices = await _ipcClient.ListDevicesAsync();
            VolumeListResult volumes = await _ipcClient.ListVolumesAsync();

            IsEngineConnected = true;
            EngineStatusText = $"Connected ({info.EngineName} — {info.BuildConfiguration})";
            EngineVersion = $"{info.EngineVersion} (IPC v{info.ProtocolVersion})";
            DevelopmentPhase = info.DevelopmentPhase;
            EngineProcessId = info.ProcessId;
            LastPingUtc = ping.UtcTimestamp;
            PhysicalDiskCount = devices.Devices.Count;
            VolumeCount = volumes.Volumes.Count;
            StorageAccessSummary = devices.IsElevated
                ? "Elevated — Raw read-only physical disk access enabled"
                : "Standard user — Run as Administrator for raw physical disk sector reads";

            Capabilities.Clear();
            AddCapability("Named Pipe IPC Protocol", caps.IpcNamedPipes);
            AddCapability("Disk Offset & Sector Validation", caps.DiskOffsetValidation);
            AddCapability("Physical Disk & Volume Enumeration", caps.DeviceEnumeration);
            AddCapability("Read-Only Raw Disk Access", caps.RawDiskReadOnly);
            AddCapability("NTFS MFT & Extent Recovery", caps.NtfsRecovery);
            AddCapability("Deep Signature Carving", caps.DeepSignatureCarving);
            AddCapability("FAT32 / exFAT / GPT / MBR", caps.Fat32ExfatGptMbr);
            AddCapability("Raw Disk Imaging & SHA-256", caps.DiskImaging);
        }
        catch (Exception ex)
        {
            IsEngineConnected = false;
            EngineStatusText = $"IPC Error: {ex.Message}";
        }
    }

    private void AddCapability(string name, CapabilityStatus status)
    {
        Capabilities.Add(new CapabilityRow(
            name,
            status.Phase,
            status.Implemented,
            status.Implemented ? "Active" : "Under Development",
            status.Notes));
    }
}

public sealed class DrivesViewModel : ViewModelBase
{
    private readonly IEngineIpcClient _ipcClient;
    private bool _isElevated;
    private string _statusMessage = "Loading storage devices...";

    public ObservableCollection<StorageDeviceDto> PhysicalDisks { get; } = new();
    public ObservableCollection<VolumeDto> Volumes { get; } = new();

    public bool IsElevated
    {
        get => _isElevated;
        private set => SetProperty(ref _isElevated, value);
    }

    public string StatusMessage
    {
        get => _statusMessage;
        private set => SetProperty(ref _statusMessage, value);
    }

    public ICommand RefreshCommand { get; }

    public DrivesViewModel(IEngineIpcClient ipcClient)
    {
        _ipcClient = ipcClient;
        RefreshCommand = new AsyncRelayCommand(RefreshAsync);
    }

    public async Task RefreshAsync()
    {
        try
        {
            if (!_ipcClient.IsConnected)
            {
                bool connected = await _ipcClient.EnsureEngineStartedAndConnectedAsync();
                if (!connected)
                {
                    StatusMessage = "Salvus.Engine is offline. Cannot enumerate storage devices.";
                    return;
                }
            }

            DeviceListResult devRes = await _ipcClient.ListDevicesAsync();
            VolumeListResult volRes = await _ipcClient.ListVolumesAsync();

            IsElevated = devRes.IsElevated;

            PhysicalDisks.Clear();
            foreach (var d in devRes.Devices)
            {
                PhysicalDisks.Add(d);
            }

            Volumes.Clear();
            foreach (var v in volRes.Volumes)
            {
                Volumes.Add(v);
            }

            StatusMessage = IsElevated
                ? $"Detected {PhysicalDisks.Count} physical disk(s) and {Volumes.Count} logical volume(s). Raw read-only access is available."
                : $"Detected {PhysicalDisks.Count} physical disk(s) and {Volumes.Count} logical volume(s). Launch Salvus as Administrator to enable raw physical disk sector reading.";
        }
        catch (Exception ex)
        {
            StatusMessage = $"Enumeration error: {ex.Message}";
        }
    }
}

public sealed class RecoveryViewModel : ViewModelBase
{
    private readonly IEngineIpcClient _ipcClient;
    private readonly ISalvusDatabase _database;
    private readonly ScanResultsViewModel _scanResults;

    private string _targetPath = string.Empty;
    private bool _includeActiveFiles;
    private bool _isScanning;
    private string _scanStatusMessage = "Select an NTFS volume path (e.g. \\\\.\\E:) or browse for a raw NTFS disk image (.raw / .img).";

    public string TargetPath
    {
        get => _targetPath;
        set => SetProperty(ref _targetPath, value);
    }

    public bool IncludeActiveFiles
    {
        get => _includeActiveFiles;
        set => SetProperty(ref _includeActiveFiles, value);
    }

    public bool IsScanning
    {
        get => _isScanning;
        private set => SetProperty(ref _isScanning, value);
    }

    public string ScanStatusMessage
    {
        get => _scanStatusMessage;
        private set => SetProperty(ref _scanStatusMessage, value);
    }

    public ICommand BrowseImageCommand { get; }
    public ICommand StartNtfsQuickScanCommand { get; }
    public ICommand StartDeepScanCommand { get; }

    public RecoveryViewModel(
        IEngineIpcClient ipcClient,
        ISalvusDatabase database,
        ScanResultsViewModel scanResults)
    {
        _ipcClient = ipcClient;
        _database = database;
        _scanResults = scanResults;

        BrowseImageCommand = new RelayCommand(_ =>
        {
            var dlg = new OpenFileDialog
            {
                Title = "Select Raw Disk Image (Read-Only)",
                Filter = "Disk Images (*.raw;*.img;*.dd)|*.raw;*.img;*.dd|All Files (*.*)|*.*"
            };
            if (dlg.ShowDialog() == true)
            {
                TargetPath = dlg.FileName;
            }
        });

        StartNtfsQuickScanCommand = new AsyncRelayCommand(StartNtfsQuickScanAsync);
        StartDeepScanCommand = new AsyncRelayCommand(StartDeepScanAsync);
    }

    public async Task StartDeepScanAsync()
    {
        if (string.IsNullOrWhiteSpace(TargetPath))
        {
            ScanStatusMessage = "Specify a target device, volume, or raw disk image file first.";
            return;
        }

        IsScanning = true;
        ScanStatusMessage = "Opening target read-only and running signature-based Deep Scan...";
        DateTimeOffset startedUtc = DateTimeOffset.UtcNow;

        try
        {
            if (!_ipcClient.IsConnected)
            {
                bool connected = await _ipcClient.EnsureEngineStartedAndConnectedAsync();
                if (!connected)
                {
                    ScanStatusMessage = "Salvus.Engine is offline.";
                    return;
                }
            }

            NtfsQuickScanResult scanResult = await _ipcClient.DeepScanAsync(TargetPath.Trim());
            _scanResults.LoadScanResult(TargetPath.Trim(), scanResult);

            await _database.UpsertScanSessionAsync(new ScanSessionRecord(
                SessionId: scanResult.SessionId,
                TargetDevicePath: TargetPath.Trim(),
                TargetDisplayName: Path.GetFileName(TargetPath.Trim()) is { Length: > 0 } name ? name : TargetPath.Trim(),
                Mode: ScanMode.Deep,
                Status: "Completed",
                StartedUtc: startedUtc,
                CompletedUtc: DateTimeOffset.UtcNow,
                DiscoveredFileCount: scanResult.Files.Count));

            ScanStatusMessage =
                $"Deep Scan complete: scanned {ByteSizeFormatter.Format(scanResult.MftRecordsScanned)}, carved {scanResult.Files.Count} file candidate(s). Open Scan Results to preview and recover.";
        }
        catch (Exception ex)
        {
            ScanStatusMessage = $"Deep Scan failed: {ex.Message}";
        }
        finally
        {
            IsScanning = false;
        }
    }

    public async Task StartNtfsQuickScanAsync()
    {
        if (string.IsNullOrWhiteSpace(TargetPath))
        {
            ScanStatusMessage = "Specify a target NTFS volume path or disk image file first.";
            return;
        }

        IsScanning = true;
        ScanStatusMessage = "Opening target read-only and scanning NTFS Master File Table ($MFT)...";
        DateTimeOffset startedUtc = DateTimeOffset.UtcNow;

        try
        {
            if (!_ipcClient.IsConnected)
            {
                bool connected = await _ipcClient.EnsureEngineStartedAndConnectedAsync();
                if (!connected)
                {
                    ScanStatusMessage = "Salvus.Engine is offline.";
                    return;
                }
            }

            NtfsQuickScanResult scanResult = await _ipcClient.QuickScanNtfsAsync(TargetPath.Trim(), IncludeActiveFiles);
            _scanResults.LoadScanResult(TargetPath.Trim(), scanResult);

            await _database.UpsertScanSessionAsync(new ScanSessionRecord(
                SessionId: scanResult.SessionId,
                TargetDevicePath: TargetPath.Trim(),
                TargetDisplayName: Path.GetFileName(TargetPath.Trim()) is { Length: > 0 } name ? name : TargetPath.Trim(),
                Mode: ScanMode.Quick,
                Status: "Completed",
                StartedUtc: startedUtc,
                CompletedUtc: DateTimeOffset.UtcNow,
                DiscoveredFileCount: scanResult.Files.Count));

            ScanStatusMessage =
                $"NTFS Quick Scan complete: scanned {scanResult.MftRecordsScanned} MFT record(s), discovered {scanResult.Files.Count} file(s). Open Scan Results to inspect and recover.";
        }
        catch (Exception ex)
        {
            ScanStatusMessage = $"Scan failed: {ex.Message}";
        }
        finally
        {
            IsScanning = false;
        }
    }
}

public sealed class ScanResultsViewModel : ViewModelBase
{
    private readonly IEngineIpcClient _ipcClient;
    private readonly List<DiscoveredFileDto> _allFiles = new();

    private string _sessionId = string.Empty;
    private string _sourceTargetPath = string.Empty;
    private string _searchQuery = string.Empty;
    private string _selectedConfidenceFilter = "All";
    private DiscoveredFileDto? _selectedFile;
    private string _previewText = "Select a file and click Load Safe Preview to inspect supported text, PDF metadata, or images.";
    private BitmapImage? _previewImage;
    private string _statusMessage = "Run a Quick Scan or Deep Scan from the Recovery section to inspect discovered files.";

    public ObservableCollection<DiscoveredFileDto> FilteredFiles { get; } = new();
    public IReadOnlyList<string> ConfidenceFilters { get; } = ["All", "High", "Medium", "Low", "Damaged", "Unrecoverable"];

    public string PreviewText
    {
        get => _previewText;
        private set => SetProperty(ref _previewText, value);
    }

    public BitmapImage? PreviewImage
    {
        get => _previewImage;
        private set => SetProperty(ref _previewImage, value);
    }

    public ICommand PreviewSelectedCommand { get; }

    public string SearchQuery
    {
        get => _searchQuery;
        set
        {
            if (SetProperty(ref _searchQuery, value))
            {
                ApplyFilters();
            }
        }
    }

    public string SelectedConfidenceFilter
    {
        get => _selectedConfidenceFilter;
        set
        {
            if (SetProperty(ref _selectedConfidenceFilter, value))
            {
                ApplyFilters();
            }
        }
    }

    public DiscoveredFileDto? SelectedFile
    {
        get => _selectedFile;
        set
        {
            if (SetProperty(ref _selectedFile, value))
            {
                PreviewImage = null;
                PreviewText = value is null
                    ? "Select a file to inspect metadata and preview."
                    : "Click 'Safe Preview' to inspect this file without executing it.";
                ((AsyncRelayCommand)RecoverSelectedCommand).RaiseCanExecuteChanged();
                ((AsyncRelayCommand)PreviewSelectedCommand).RaiseCanExecuteChanged();
            }
        }
    }

    public string StatusMessage
    {
        get => _statusMessage;
        private set => SetProperty(ref _statusMessage, value);
    }

    public ICommand RecoverSelectedCommand { get; }

    public ScanResultsViewModel(IEngineIpcClient ipcClient)
    {
        _ipcClient = ipcClient;
        RecoverSelectedCommand = new AsyncRelayCommand(RecoverSelectedAsync, () => SelectedFile is not null);
        PreviewSelectedCommand = new AsyncRelayCommand(LoadSafePreviewAsync, () => SelectedFile is not null);
    }

    private async Task LoadSafePreviewAsync()
    {
        if (SelectedFile is null || string.IsNullOrWhiteSpace(_sessionId))
        {
            return;
        }

        PreviewImage = null;
        PreviewText = "Loading safe non-executing preview...";

        try
        {
            FilePreviewResult preview = await _ipcClient.GetFilePreviewAsync(_sessionId, SelectedFile.RecordNumber);
            if (string.Equals(preview.PreviewKind, "image", StringComparison.OrdinalIgnoreCase) &&
                !string.IsNullOrWhiteSpace(preview.Base64Bytes))
            {
                byte[] imgBytes = Convert.FromBase64String(preview.Base64Bytes);
                using var ms = new MemoryStream(imgBytes);
                var bmp = new BitmapImage();
                bmp.BeginInit();
                bmp.CacheOption = BitmapCacheOption.OnLoad;
                bmp.StreamSource = ms;
                bmp.EndInit();
                bmp.Freeze();
                PreviewImage = bmp;
                PreviewText = $"Image preview loaded ({ByteSizeFormatter.Format(preview.BytesRead)}).";
            }
            else
            {
                PreviewText = preview.TextContent;
            }
        }
        catch (Exception ex)
        {
            PreviewImage = null;
            PreviewText = $"Preview unavailable (image/file may be damaged or incomplete): {ex.Message}";
        }
    }

    public void LoadScanResult(string sourceTargetPath, NtfsQuickScanResult result)
    {
        _sessionId = result.SessionId;
        _sourceTargetPath = sourceTargetPath;
        _allFiles.Clear();
        _allFiles.AddRange(result.Files);
        ApplyFilters();
        StatusMessage = $"Session {result.SessionId}: {_allFiles.Count} file(s) discovered from {sourceTargetPath}.";
    }

    private void ApplyFilters()
    {
        FilteredFiles.Clear();
        foreach (var file in _allFiles)
        {
            if (!string.Equals(SelectedConfidenceFilter, "All", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(file.Confidence, SelectedConfidenceFilter, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            if (!string.IsNullOrWhiteSpace(SearchQuery) &&
                !file.FileName.Contains(SearchQuery, StringComparison.OrdinalIgnoreCase) &&
                !file.FullPath.Contains(SearchQuery, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            FilteredFiles.Add(file);
        }
    }

    private async Task RecoverSelectedAsync()
    {
        if (SelectedFile is null || string.IsNullOrWhiteSpace(_sessionId))
        {
            return;
        }

        var dlg = new SaveFileDialog
        {
            Title = "Choose Separate Recovery Destination",
            FileName = SelectedFile.FileName
        };
        if (dlg.ShowDialog() != true)
        {
            return;
        }

        try
        {
            RecoverFileResult res = await _ipcClient.RecoverFileAsync(
                _sessionId, SelectedFile.RecordNumber, dlg.FileName);
            StatusMessage = $"Recovered '{res.FileName}' ({ByteSizeFormatter.Format(res.BytesWritten)}) to {res.DestinationPath}.";
        }
        catch (Exception ex)
        {
            StatusMessage = $"Recovery failed: {ex.Message}";
        }
    }
}

public sealed class DiskImagingViewModel : ViewModelBase
{
    public string StatusBadge => "Scheduled for Phase 6 — Professional Features";
    public string Description =>
        "Sector-by-sector raw disk imaging, bounded retry policies for unstable media, persistent bad-sector maps, and SHA-256 verification will be enabled in Phase 6.";
}

public sealed class RecoveryHistoryViewModel : ViewModelBase
{
    private readonly ISalvusDatabase _database;

    public ObservableCollection<ScanSessionRecord> ScanSessions { get; } = new();
    public ObservableCollection<RecoveryHistoryRecord> RecoveryRecords { get; } = new();

    public ICommand RefreshCommand { get; }

    public RecoveryHistoryViewModel(ISalvusDatabase database)
    {
        _database = database;
        RefreshCommand = new AsyncRelayCommand(RefreshAsync);
    }

    public async Task RefreshAsync()
    {
        var sessions = await _database.GetRecentScanSessionsAsync(50);
        var history = await _database.GetRecoveryHistoryAsync(50);

        ScanSessions.Clear();
        foreach (var s in sessions)
        {
            ScanSessions.Add(s);
        }

        RecoveryRecords.Clear();
        foreach (var h in history)
        {
            RecoveryRecords.Add(h);
        }
    }
}

public sealed class SettingsViewModel : ViewModelBase
{
    private readonly ISalvusDatabase _database;
    private string _readBufferSizeMb = "4";
    private string _saveStatusMessage = string.Empty;

    public string DatabasePath => _database.DatabasePath;

    public string ReadBufferSizeMb
    {
        get => _readBufferSizeMb;
        set => SetProperty(ref _readBufferSizeMb, value);
    }

    public string SaveStatusMessage
    {
        get => _saveStatusMessage;
        private set => SetProperty(ref _saveStatusMessage, value);
    }

    public ICommand SaveCommand { get; }

    public SettingsViewModel(ISalvusDatabase database)
    {
        _database = database;
        SaveCommand = new AsyncRelayCommand(SaveAsync);
    }

    public async Task LoadAsync()
    {
        string? saved = await _database.GetPreferenceAsync("read_buffer_mb");
        if (!string.IsNullOrWhiteSpace(saved))
        {
            ReadBufferSizeMb = saved;
        }
    }

    public async Task SaveAsync()
    {
        if (!int.TryParse(ReadBufferSizeMb, out int mb) || mb < 1 || mb > 64)
        {
            SaveStatusMessage = "Read buffer size must be an integer between 1 and 64 MiB.";
            return;
        }

        await _database.SetPreferenceAsync("read_buffer_mb", mb.ToString());
        SaveStatusMessage = $"Preferences saved to SQLite ({DateTime.Now:HH:mm:ss}).";
    }
}

public sealed class AboutViewModel : ViewModelBase
{
    private readonly IEngineIpcClient _ipcClient;
    private string _engineSummary = "Checking engine process...";

    public string ProductName => "Salvus";
    public string Tagline => "Recover what matters.";
    public string Version => "0.4.0 — Phase 4 (Deep Scanning)";
    public string License => "GNU General Public License v3.0 or later (GPL-3.0-or-later)";
    public string Platform => "Windows 10 / Windows 11 (x64)";

    public string EngineSummary
    {
        get => _engineSummary;
        private set => SetProperty(ref _engineSummary, value);
    }

    public AboutViewModel(IEngineIpcClient ipcClient)
    {
        _ipcClient = ipcClient;
    }

    public async Task RefreshEngineInfoAsync()
    {
        try
        {
            if (_ipcClient.IsConnected)
            {
                EngineInfo info = await _ipcClient.GetEngineInfoAsync();
                EngineSummary = $"{info.EngineName} v{info.EngineVersion} ({info.BuildConfiguration}, IPC v{info.ProtocolVersion}, PID {info.ProcessId})";
            }
            else
            {
                EngineSummary = "Salvus.Engine is not currently connected.";
            }
        }
        catch (Exception ex)
        {
            EngineSummary = $"Unavailable ({ex.Message})";
        }
    }
}
