using System.Buffers.Binary;
using Salvus.Shared.Ipc;
using Xunit;

namespace Salvus.Tests;

public sealed class IpcFramingTests
{
    [Fact]
    public async Task WriteAndReadMessageAsync_RoundTripsIpcRequest()
    {
        using var stream = new MemoryStream();
        var original = new IpcRequest("req-42", IpcConstants.ProtocolVersion, "ping");

        await IpcFraming.WriteMessageAsync(stream, original, CancellationToken.None);
        stream.Position = 0;

        var roundTripped = await IpcFraming.ReadMessageAsync<IpcRequest>(stream, CancellationToken.None);
        Assert.Equal("req-42", roundTripped.Id);
        Assert.Equal(IpcConstants.ProtocolVersion, roundTripped.ProtocolVersion);
        Assert.Equal("ping", roundTripped.Command);
    }

    [Fact]
    public void DecodeFrameLength_RejectsZeroAndOversizedFrames()
    {
        byte[] zero = [0, 0, 0, 0];
        Assert.Throws<InvalidDataException>(() => IpcFraming.DecodeFrameLength(zero));

        byte[] oversized = new byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(oversized, (uint)IpcConstants.MaxFrameBytes + 1);
        Assert.Throws<InvalidDataException>(() => IpcFraming.DecodeFrameLength(oversized));
    }
}
