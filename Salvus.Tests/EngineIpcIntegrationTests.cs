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
        Assert.True(caps.DeviceEnumeration.Implemented);
        Assert.True(caps.RawDiskReadOnly.Implemented);
        Assert.True(caps.NtfsRecovery.Implemented);
        Assert.True(caps.DeepSignatureCarving.Implemented);
        Assert.True(caps.Fat32ExfatGptMbr.Implemented);
        Assert.True(caps.DiskImaging.Implemented);

        // Live Deep Scan + Safe Preview + Recover (with SHA-256) + Disk Imaging round-trip
        string tempRaw = Path.Combine(Path.GetTempPath(), $"salvus_ipc_deep_{Guid.NewGuid():N}.raw");
        string tempImgCopy = Path.Combine(Path.GetTempPath(), $"salvus_ipc_img_{Guid.NewGuid():N}.raw");
        string tempRecovered = Path.Combine(Path.GetTempPath(), $"salvus_ipc_rec_{Guid.NewGuid():N}.pdf");
        try
        {
            byte[] raw = new byte[16384];
            byte[] pdfBytes = System.Text.Encoding.ASCII.GetBytes(
                "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n012345678901234567890123456789\n%%EOF");
            Buffer.BlockCopy(pdfBytes, 0, raw, 2048, pdfBytes.Length);
            await File.WriteAllBytesAsync(tempRaw, raw);

            var deepRes = await client.DeepScanAsync(tempRaw);
            Assert.Single(deepRes.Files);
            Assert.EndsWith(".pdf", deepRes.Files[0].FileName);
            Assert.Equal("Low", deepRes.Files[0].Confidence);

            var preview = await client.GetFilePreviewAsync(deepRes.SessionId, deepRes.Files[0].RecordNumber);
            Assert.Equal("text", preview.PreviewKind);
            Assert.Contains("%PDF-1.7", preview.TextContent);

            var rec = await client.RecoverFileAsync(deepRes.SessionId, deepRes.Files[0].RecordNumber, tempRecovered);
            Assert.Equal((ulong)pdfBytes.Length, rec.BytesWritten);
            Assert.Equal(64, rec.Sha256Hex.Length);
            Assert.Equal(pdfBytes, await File.ReadAllBytesAsync(tempRecovered));

            var img = await client.CreateDiskImageAsync(tempRaw, tempImgCopy, 2);
            Assert.Equal((ulong)raw.Length, img.TotalBytesCopied);
            Assert.Equal(0UL, img.BadSectorCount);
            Assert.Equal(64, img.Sha256Hex.Length);

            var verify = await client.VerifySha256Async(tempRaw);
            Assert.Equal(img.Sha256Hex, verify.Sha256Hex);
        }
        finally
        {
            if (File.Exists(tempRaw)) File.Delete(tempRaw);
            if (File.Exists(tempImgCopy)) File.Delete(tempImgCopy);
            if (File.Exists(tempRecovered)) File.Delete(tempRecovered);
        }

        var devices = await client.ListDevicesAsync();
        Assert.NotEmpty(devices.Devices);
        Assert.All(devices.Devices, d =>
        {
            Assert.True(d.IsPhysicalDisk);
            Assert.False(string.IsNullOrWhiteSpace(d.DevicePath));
        });

        var volumes = await client.ListVolumesAsync();
        Assert.NotEmpty(volumes.Volumes);
        Assert.Contains(volumes.Volumes, v => !string.IsNullOrWhiteSpace(v.DriveLetter));

        await client.ShutdownEngineAsync();
    }
}
