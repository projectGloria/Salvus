using System.Buffers.Binary;
using System.Text;
using System.Text.Json;

namespace Salvus.Shared.Ipc;

public static class IpcConstants
{
    public const uint ProtocolVersion = 1;
    public const int MaxFrameBytes = 16 * 1024 * 1024; // 16 MiB
    public const string DefaultPipeShortName = "Salvus.Engine";

    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        WriteIndented = false
    };
}

public sealed record IpcRequest(
    string Id,
    uint ProtocolVersion,
    string Command,
    JsonElement? Payload = null);

public sealed record IpcResponse(
    string Id,
    uint ProtocolVersion,
    bool Ok,
    string ErrorCode,
    string ErrorMessage,
    JsonElement Payload);

public static class IpcFraming
{
    public static byte[] EncodeFrame(string jsonPayload)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(jsonPayload);
        byte[] payloadBytes = Encoding.UTF8.GetBytes(jsonPayload);
        if (payloadBytes.Length == 0 || payloadBytes.Length > IpcConstants.MaxFrameBytes)
        {
            throw new InvalidOperationException(
                $"IPC payload length {payloadBytes.Length} is outside valid bounds (1..{IpcConstants.MaxFrameBytes}).");
        }

        byte[] frame = new byte[4 + payloadBytes.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(0, 4), (uint)payloadBytes.Length);
        Buffer.BlockCopy(payloadBytes, 0, frame, 4, payloadBytes.Length);
        return frame;
    }

    public static int DecodeFrameLength(ReadOnlySpan<byte> headerBytes)
    {
        if (headerBytes.Length != 4)
        {
            throw new InvalidDataException("IPC frame header must be exactly 4 bytes.");
        }

        uint length = BinaryPrimitives.ReadUInt32LittleEndian(headerBytes);
        if (length == 0)
        {
            throw new InvalidDataException("IPC frame length must be greater than zero.");
        }
        if (length > IpcConstants.MaxFrameBytes)
        {
            throw new InvalidDataException($"IPC frame length {length} exceeds maximum {IpcConstants.MaxFrameBytes}.");
        }

        return (int)length;
    }

    public static async Task WriteMessageAsync<T>(Stream stream, T message, CancellationToken cancellationToken)
    {
        string json = JsonSerializer.Serialize(message, IpcConstants.JsonOptions);
        byte[] frame = EncodeFrame(json);
        await stream.WriteAsync(frame, cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
    }

    public static async Task<T> ReadMessageAsync<T>(Stream stream, CancellationToken cancellationToken)
    {
        byte[] header = new byte[4];
        await ReadExactAsync(stream, header, cancellationToken).ConfigureAwait(false);
        int length = DecodeFrameLength(header);

        byte[] payload = new byte[length];
        await ReadExactAsync(stream, payload, cancellationToken).ConfigureAwait(false);

        string json = Encoding.UTF8.GetString(payload);
        T? deserialized = JsonSerializer.Deserialize<T>(json, IpcConstants.JsonOptions);
        return deserialized ?? throw new InvalidDataException($"Failed to deserialize IPC message as {typeof(T).Name}.");
    }

    private static async Task ReadExactAsync(Stream stream, byte[] buffer, CancellationToken cancellationToken)
    {
        int offset = 0;
        while (offset < buffer.Length)
        {
            int read = await stream.ReadAsync(buffer.AsMemory(offset, buffer.Length - offset), cancellationToken)
                .ConfigureAwait(false);
            if (read == 0)
            {
                throw new EndOfStreamException("IPC pipe closed unexpectedly while reading frame.");
            }
            offset += read;
        }
    }
}
