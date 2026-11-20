using System.Collections.ObjectModel;
using System.Windows.Input;

namespace Salvus.Desktop.ViewModels;

public sealed record NavigationItem(string Key, string Title, string Subtitle, ViewModelBase ViewModel);

public sealed class MainWindowViewModel : ViewModelBase
{
    private NavigationItem _selectedNavigationItem;
    private string _engineStatusSummary = "Connecting to Salvus.Engine...";
    private bool _isEngineOnline;

    public ObservableCollection<NavigationItem> NavigationItems { get; }

    public NavigationItem SelectedNavigationItem
    {
        get => _selectedNavigationItem;
        set
        {
            if (SetProperty(ref _selectedNavigationItem, value))
            {
                OnPropertyChanged(nameof(CurrentViewModel));
                OnPropertyChanged(nameof(CurrentSectionTitle));
                OnPropertyChanged(nameof(CurrentSectionSubtitle));
            }
        }
    }

    public ViewModelBase CurrentViewModel => _selectedNavigationItem.ViewModel;
    public string CurrentSectionTitle => _selectedNavigationItem.Title;
    public string CurrentSectionSubtitle => _selectedNavigationItem.Subtitle;

    public string EngineStatusSummary
    {
        get => _engineStatusSummary;
        private set => SetProperty(ref _engineStatusSummary, value);
    }

    public bool IsEngineOnline
    {
        get => _isEngineOnline;
        private set => SetProperty(ref _isEngineOnline, value);
    }

    public ICommand SelectSectionCommand { get; }

    public DashboardViewModel Dashboard { get; }
    public RecoveryHistoryViewModel RecoveryHistory { get; }
    public SettingsViewModel Settings { get; }
    public AboutViewModel About { get; }

    public MainWindowViewModel(
        DashboardViewModel dashboard,
        DrivesViewModel drives,
        RecoveryViewModel recovery,
        ScanResultsViewModel scanResults,
        DiskImagingViewModel diskImaging,
        RecoveryHistoryViewModel recoveryHistory,
        SettingsViewModel settings,
        AboutViewModel about)
    {
        Dashboard = dashboard;
        RecoveryHistory = recoveryHistory;
        Settings = settings;
        About = about;

        NavigationItems = new ObservableCollection<NavigationItem>
        {
            new("dashboard", "Dashboard", "System status, engine diagnostics, and recent activity", dashboard),
            new("drives", "Drives", "Physical disks, partitions, removable media, and disk images", drives),
            new("recovery", "Recovery", "Configure Quick Scan, Deep Scan, or Partition Recovery", recovery),
            new("results", "Scan Results", "Browse discovered files, confidence ratings, and safe previews", scanResults),
            new("imaging", "Disk Imaging", "Sector-by-sector raw disk imaging and SHA-256 verification", diskImaging),
            new("history", "Recovery History", "Persisted scan sessions, destinations, and audit reports", recoveryHistory),
            new("settings", "Settings", "Read buffer sizing, IPC settings, and local storage paths", settings),
            new("about", "About Salvus", "Branding, license, architecture, and data-integrity commitments", about)
        };

        _selectedNavigationItem = NavigationItems[0];

        SelectSectionCommand = new RelayCommand(param =>
        {
            if (param is NavigationItem item)
            {
                SelectedNavigationItem = item;
            }
            else if (param is string key)
            {
                var found = NavigationItems.FirstOrDefault(n => n.Key == key);
                if (found is not null)
                {
                    SelectedNavigationItem = found;
                }
            }
        });
    }

    public async Task InitializeAsync()
    {
        await Dashboard.RefreshAsync();
        await RecoveryHistory.RefreshAsync();
        await Settings.LoadAsync();
        await About.RefreshEngineInfoAsync();

        IsEngineOnline = Dashboard.IsEngineConnected;
        EngineStatusSummary = Dashboard.IsEngineConnected
            ? $"Engine Online (PID {Dashboard.EngineProcessId})"
            : "Engine Offline";
    }
}
