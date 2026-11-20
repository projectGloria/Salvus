using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text.Json;
using Salvus.Shared.Contracts;
using Salvus.Shared.Ipc;

namespace Salvus.Desktop.Services;

public interface IEngineIpcClient : IAsyncDisposable
{
    bool IsConnected { get; }
    string PipeName { get; }
    string? EngineExecutablePath { get; }
    Task<bool> EnsureEngineStartedAndConnectedAsync(CancellationToken cancellationToken = default);
    Task<PingResult> PingAsync(CancellationToken cancellationToken = default);
    Task<EngineInfo> GetEngineInfoAsync(CancellationToken cancellationToken = default);
    Task<EngineCapabilities> GetCapabilitiesAsync(CancellationToken cancellationToken = default);
    Task ShutdownEngineAsync(CancellationToken cancellationToken = default);
}

public sealed class EngineIpcClient : IEngineIpcClient
{
    private readonly SemaphoreSlim _ioLock = new(1, 1);
    private NamedPipeClientStream? _pipe;
    private Process? _engineProcess;

    public string PipeName { get; }
    public string? EngineExecutablePath { get; }
    public bool IsConnected => _pipe is { IsConnected: true };

    public EngineIpcClient(string? pipeShortName = null, string? engineExecutablePath = null)
    {
        PipeName = string.IsNullOrWhiteSpace(pipeShortName)
            ? $"Salvus.Engine.{Environment.ProcessId}.{Guid.NewGuid():N}"
            : pipeShortName;
        EngineExecutablePath = engineExecutablePath ?? ResolveDefaultEnginePath();
    }

    public static string? ResolveDefaultEnginePath()
    {
        string baseDir = AppContext.BaseDirectory;
        string[] candidates =
        [
            Path.Combine(baseDir, "Salvus.Engine.exe"),
            Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", "..", "Salvus.Engine", "build", "Release", "Salvus.Engine.exe")),
            Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", "..", "Salvus.Engine", "build", "Debug", "Salvus.Engine.exe"))
        ];

        return candidates.FirstOrDefault(File.Exists);
    }

    public async Task<bool> EnsureEngineStartedAndConnectedAsync(CancellationToken cancellationToken = default)
    {
        await _ioLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_pipe is { IsConnected: true })
            {
                return true;
            }

            if (_engineProcess is null || _engineProcess.HasExited)
            {
                if (string.IsNullOrWhiteSpace(EngineExecutablePath) || !File.Exists(EngineExecutablePath))
                {
                    return false;
                }

                var psi = new ProcessStartInfo
                {
                    FileName = EngineExecutablePath,
                    Arguments = $"--pipe \"{PipeName}\"",
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardError = true,
                    RedirectStandardOutput = true
                };

                _engineProcess = Process.Start(psi);
                if (_engineProcess is null)
                {
                    return false;
                }
            }

            _pipe?.Dispose();
            _pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);

            // Retry briefly while engine initializes its Named Pipe server
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (true)
            {
                try
                {
                    await _pipe.ConnectAsync(500, cancellationToken).ConfigureAwait(false);
                    return _pipe.IsConnected;
                }
                catch (TimeoutException) when (DateTime.UtcNow < deadline && !cancellationToken.IsCancellationRequested)
                {
                    await Task.Delay(100, cancellationToken).ConfigureAwait(false);
                }
                catch (IOException) when (DateTime.UtcNow < deadline && !cancellationToken.IsCancellationRequested)
                {
                    await Task.Delay(100, cancellationToken).ConfigureAwait(false);
                }
            }
        }
        finally
        {
            _ioLock.Release();
        }
    }

    public Task<PingResult> PingAsync(CancellationToken cancellationToken = default) =>
        SendCommandAsync<PingResult>("ping", cancellationToken);

    public Task<EngineInfo> GetEngineInfoAsync(CancellationToken cancellationToken = default) =>
        SendCommandAsync<EngineInfo>("get_engine_info", cancellationToken);

    public Task<EngineCapabilities> GetCapabilitiesAsync(CancellationToken cancellationToken = default) =>
        SendCommandAsync<EngineCapabilities>("get_capabilities", cancellationToken);

    public async Task ShutdownEngineAsync(CancellationToken cancellationToken = default)
    {
        if (!IsConnected)
        {
            return;
        }

        try
        {
            await SendCommandAsync<JsonElement>("shutdown", cancellationToken).ConfigureAwait(false);
        }
        catch
        {
            // Engine may close pipe immediately upon shutdown
        }
    }

    private async Task<TPayload> SendCommandAsync<TPayload>(string command, CancellationToken cancellationToken)
    {
        await _ioLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_pipe is not { IsConnected: true })
            {
                throw new InvalidOperationException("Not connected to Salvus.Engine Named Pipe.");
            }

            string reqId = Guid.NewGuid().ToString("N");
            var request = new IpcRequest(reqId, IpcConstants.ProtocolVersion, command);
            await IpcFraming.WriteMessageAsync(_pipe, request, cancellationToken).ConfigureAwait(false);

            IpcResponse response = await IpcFraming.ReadMessageAsync<IpcResponse>(_pipe, cancellationToken)
                .ConfigureAwait(false);

            if (!string.Equals(response.Id, reqId, StringComparison.Ordinal))
            {
                throw new InvalidDataException($"IPC response ID mismatch: expected {reqId}, received {response.Id}.");
            }

            if (!response.Ok)
            {
                throw new InvalidOperationException(
                    $"Engine command '{command}' failed ({response.ErrorCode}): {response.ErrorMessage}");
            }

            TPayload? payload = response.Payload.Deserialize<TPayload>(IpcConstants.JsonOptions);
            return payload ?? throw new InvalidDataException($"Empty payload returned for command '{command}'.");
        }
        finally
        {
            _ioLock.Release();
        }
    }

    public async ValueTask DisposeAsync()
    {
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            await ShutdownEngineAsync(cts.Token).ConfigureAwait(false);
        }
        catch
        {
            // Ignore shutdown errors on dispose
        }

        _pipe?.Dispose();
        _pipe = null;

        if (_engineProcess is not null)
        {
            try
            {
                if (!_engineProcess.HasExited && !_engineProcess.WaitForExit(1500))
                {
                    _engineProcess.Kill(entireProcessTree: true);
                }
            }
            catch
            {
                // Ignore process cleanup races
            }
            finally
            {
                _engineProcess.Dispose();
                _engineProcess = null;
            }
        }

        _ioLock.Dispose();
    }
}
