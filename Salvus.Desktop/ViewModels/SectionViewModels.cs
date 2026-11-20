using System.Collections.ObjectModel;
using System.Windows.Input;
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
    public string StatusBadge => "Scheduled for Phases 3–5";
    public string Description =>
        "Quick Scan (NTFS MFT metadata), Deep Scan (signature-based carving), and non-destructive Partition Recovery (MBR/GPT) require Phases 2–5 in Salvus.Engine. Scan controls remain disabled until real engine scanning is implemented.";
}

public sealed class ScanResultsViewModel : ViewModelBase
{
    public string StatusBadge => "Available in Phase 3";
    public string Description =>
        "Discovered file trees, confidence classifications (High, Medium, Low, Damaged, Unrecoverable), metadata inspection, and safe non-executing previews will populate here from real scan sessions.";
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
    public string Version => "0.2.0 — Phase 2 (Storage Access)";
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
