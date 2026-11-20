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
    uint ProcessId);

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
