using Salvus.Data;
using Salvus.Shared.Contracts;
using Xunit;

namespace Salvus.Tests;

public sealed class SalvusDatabaseTests : IDisposable
{
    private readonly string _tempDir;
    private readonly string _dbPath;

    public SalvusDatabaseTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "salvus-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);
        _dbPath = Path.Combine(_tempDir, "salvus-test.db");
    }

    [Fact]
    public async Task InitializeAndPersistPreferencesAndSessions_WorksEndToEnd()
    {
        var db = new SalvusDatabase(_dbPath);
        await db.InitializeAsync();

        await db.SetPreferenceAsync("read_buffer_mb", "8");
        string? pref = await db.GetPreferenceAsync("read_buffer_mb");
        Assert.Equal("8", pref);

        var session = new ScanSessionRecord(
            SessionId: "sess-1",
            TargetDevicePath: @"\\.\PhysicalDrive99",
            TargetDisplayName: "Synthetic Test Image",
            Mode: ScanMode.Quick,
            Status: "Completed",
            StartedUtc: DateTimeOffset.UtcNow.AddMinutes(-1),
            CompletedUtc: DateTimeOffset.UtcNow,
            DiscoveredFileCount: 0);

        await db.UpsertScanSessionAsync(session);
        var sessions = await db.GetRecentScanSessionsAsync(10);

        Assert.Single(sessions);
        Assert.Equal("sess-1", sessions[0].SessionId);
        Assert.Equal("Synthetic Test Image", sessions[0].TargetDisplayName);
    }

    public void Dispose()
    {
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        try
        {
            if (Directory.Exists(_tempDir))
            {
                Directory.Delete(_tempDir, recursive: true);
            }
        }
        catch
        {
            // Best-effort temp cleanup
        }
    }
}
