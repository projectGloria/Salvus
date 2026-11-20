using Salvus.Desktop.Services;
using Salvus.Shared.Ipc;
using Xunit;

namespace Salvus.Tests;

public sealed class EngineIpcIntegrationTests
{
    [Fact]
    public async Task LiveNamedPipeRoundTrip_WithNativeCppEngine_Succeeds()
    {
        string? enginePath = EngineIpcClient.ResolveDefaultEnginePath();
        Assert.False(string.IsNullOrWhiteSpace(enginePath), "Salvus.Engine.exe must be built before running IPC integration tests.");
        Assert.True(File.Exists(enginePath), $"Expected native engine binary at: {enginePath}");

        string testPipe = $"Salvus.Engine.Test.{Guid.NewGuid():N}";
        await using var client = new EngineIpcClient(testPipe, enginePath);

        bool connected = await client.EnsureEngineStartedAndConnectedAsync();
        Assert.True(connected);

        var ping = await client.PingAsync();
        Assert.Equal("ready", ping.Status);
        Assert.True(ping.ProcessId > 0);

        var info = await client.GetEngineInfoAsync();
        Assert.Equal("Salvus.Engine", info.EngineName);
        Assert.Equal(IpcConstants.ProtocolVersion, info.ProtocolVersion);

        var caps = await client.GetCapabilitiesAsync();
        Assert.True(caps.IpcNamedPipes.Implemented);
        Assert.True(caps.DiskOffsetValidation.Implemented);
        Assert.False(caps.NtfsRecovery.Implemented);

        await client.ShutdownEngineAsync();
    }
}
