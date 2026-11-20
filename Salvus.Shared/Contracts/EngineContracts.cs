namespace Salvus.Shared.Contracts;

public sealed record PingResult(
    string Status,
    uint ProcessId,
    string UtcTimestamp);

public sealed record EngineInfo(
    string EngineName,
    string EngineVersion,
    string DevelopmentPhase,
    uint ProtocolVersion,
    string BuildConfiguration,
    string Platform,
    uint ProcessId,
    bool IsElevated = false);

public sealed record CapabilityStatus(
    bool Implemented,
    string Phase,
    string Notes);

public sealed record EngineCapabilities(
    CapabilityStatus IpcNamedPipes,
    CapabilityStatus DiskOffsetValidation,
    CapabilityStatus DeviceEnumeration,
    CapabilityStatus RawDiskReadOnly,
    CapabilityStatus NtfsRecovery,
    CapabilityStatus DeepSignatureCarving,
    CapabilityStatus Fat32ExfatGptMbr,
    CapabilityStatus DiskImaging);

public sealed record StorageDeviceDto(
    uint PhysicalDiskIndex,
    string DevicePath,
    string DeviceName,
    string Model,
    string Manufacturer,
    string ConnectionType,
    string PartitionLayout,
    string HealthStatus,
    ulong CapacityBytes,
    uint SectorSizeBytes,
    string DeviceType,
    bool IsPhysicalDisk,
    bool IsRemovable,
    bool IsSsd,
    bool RawAccessAvailable)
{
    public string CapacityFormatted => CapacityBytes > 0
        ? ByteSizeFormatter.Format(CapacityBytes)
        : "Elevation Required";
}

public sealed record VolumeDto(
    string VolumePath,
    string DriveLetter,
    string Label,
    string FileSystem,
    ulong TotalBytes,
    ulong AvailableBytes,
    int PhysicalDiskIndex,
    ulong PartitionOffsetBytes,
    ulong PartitionLengthBytes,
    bool IsRemovable)
{
    public string DisplayName => string.IsNullOrWhiteSpace(DriveLetter)
        ? (string.IsNullOrWhiteSpace(Label) ? "Unmounted Partition" : Label)
        : (string.IsNullOrWhiteSpace(Label) ? DriveLetter : $"{DriveLetter} ({Label})");

    public string TotalFormatted => ByteSizeFormatter.Format(TotalBytes);
    public string AvailableFormatted => ByteSizeFormatter.Format(AvailableBytes);
    public string ParentDiskDisplay => PhysicalDiskIndex >= 0 ? $"PhysicalDrive{PhysicalDiskIndex}" : "Unmapped";
}

public sealed record DeviceListResult(
    bool IsElevated,
    IReadOnlyList<StorageDeviceDto> Devices);

public sealed record VolumeListResult(
    IReadOnlyList<VolumeDto> Volumes);

public static class ByteSizeFormatter
{
    private static readonly string[] Units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"];

    public static string Format(ulong bytes)
    {
        if (bytes == 0)
        {
            return "0 B";
        }

        double value = bytes;
        int unitIndex = 0;
        while (value >= 1024.0 && unitIndex < Units.Length - 1)
        {
            value /= 1024.0;
            unitIndex++;
        }

        return unitIndex == 0
            ? $"{bytes} B"
            : $"{value:0.##} {Units[unitIndex]}";
    }
}

public enum RecoveryConfidence
{
    High = 0,
    Medium = 1,
    Low = 2,
    Damaged = 3,
    Unrecoverable = 4
}

public enum ScanMode
{
    Quick = 0,
    Deep = 1,
    Partition = 2
}

public sealed record ScanSessionRecord(
    string SessionId,
    string TargetDevicePath,
    string TargetDisplayName,
    ScanMode Mode,
    string Status,
    DateTimeOffset StartedUtc,
    DateTimeOffset? CompletedUtc,
    long DiscoveredFileCount);

public sealed record RecoveryHistoryRecord(
    string RecoveryId,
    string SessionId,
    string DestinationPath,
    int RecoveredFileCount,
    int FailedFileCount,
    DateTimeOffset TimestampUtc,
    string? ReportSha256);
