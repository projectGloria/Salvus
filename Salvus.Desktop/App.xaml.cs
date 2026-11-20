using System.Windows;
using Microsoft.Extensions.DependencyInjection;
using Salvus.Data;
using Salvus.Desktop.Services;
using Salvus.Desktop.ViewModels;
using Salvus.Desktop.Views;

namespace Salvus.Desktop;

public partial class App : Application
{
    private ServiceProvider? _serviceProvider;

    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var services = new ServiceCollection();
        ConfigureServices(services);
        _serviceProvider = services.BuildServiceProvider();

        var database = _serviceProvider.GetRequiredService<ISalvusDatabase>();
        await database.InitializeAsync();

        var mainWindow = _serviceProvider.GetRequiredService<MainWindow>();
        var mainViewModel = _serviceProvider.GetRequiredService<MainWindowViewModel>();
        mainWindow.DataContext = mainViewModel;
        MainWindow = mainWindow;
        mainWindow.Show();

        await mainViewModel.InitializeAsync();
    }

    protected override async void OnExit(ExitEventArgs e)
    {
        if (_serviceProvider is not null)
        {
            var ipcClient = _serviceProvider.GetService<IEngineIpcClient>();
            if (ipcClient is not null)
            {
                await ipcClient.DisposeAsync();
            }
            await _serviceProvider.DisposeAsync();
        }
        base.OnExit(e);
    }

    private static void ConfigureServices(IServiceCollection services)
    {
        services.AddSingleton<ISalvusDatabase>(_ => new SalvusDatabase());
        services.AddSingleton<IEngineIpcClient>(_ => new EngineIpcClient());

        services.AddSingleton<DashboardViewModel>();
        services.AddSingleton<DrivesViewModel>();
        services.AddSingleton<RecoveryViewModel>();
        services.AddSingleton<ScanResultsViewModel>();
        services.AddSingleton<DiskImagingViewModel>();
        services.AddSingleton<RecoveryHistoryViewModel>();
        services.AddSingleton<SettingsViewModel>();
        services.AddSingleton<AboutViewModel>();
        services.AddSingleton<MainWindowViewModel>();

        services.AddSingleton<MainWindow>();
    }
}
