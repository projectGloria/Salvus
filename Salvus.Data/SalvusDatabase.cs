using Microsoft.Data.Sqlite;
using Salvus.Shared.Contracts;

namespace Salvus.Data;

public interface ISalvusDatabase
{
    string DatabasePath { get; }
    Task InitializeAsync(CancellationToken cancellationToken = default);
    Task<IReadOnlyList<ScanSessionRecord>> GetRecentScanSessionsAsync(int limit = 20, CancellationToken cancellationToken = default);
    Task<IReadOnlyList<RecoveryHistoryRecord>> GetRecoveryHistoryAsync(int limit = 50, CancellationToken cancellationToken = default);
    Task<string?> GetPreferenceAsync(string key, CancellationToken cancellationToken = default);
    Task SetPreferenceAsync(string key, string value, CancellationToken cancellationToken = default);
    Task UpsertScanSessionAsync(ScanSessionRecord session, CancellationToken cancellationToken = default);
    Task InsertRecoveryHistoryAsync(RecoveryHistoryRecord record, CancellationToken cancellationToken = default);
}

public sealed class SalvusDatabase : ISalvusDatabase
{
    private readonly string _connectionString;

    public string DatabasePath { get; }

    public SalvusDatabase(string? databasePath = null)
    {
        if (string.IsNullOrWhiteSpace(databasePath))
        {
            string appDataDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Salvus");
            Directory.CreateDirectory(appDataDir);
            DatabasePath = Path.Combine(appDataDir, "salvus.db");
        }
        else
        {
            DatabasePath = databasePath;
            string? dir = Path.GetDirectoryName(databasePath);
            if (!string.IsNullOrWhiteSpace(dir))
            {
                Directory.CreateDirectory(dir);
            }
        }

        var builder = new SqliteConnectionStringBuilder
        {
            DataSource = DatabasePath,
            Mode = SqliteOpenMode.ReadWriteCreate,
            Cache = SqliteCacheMode.Shared
        };
        _connectionString = builder.ToString();
    }

    public async Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        await using var conn = new SqliteConnection(_connectionString);
        await conn.OpenAsync(cancellationToken).ConfigureAwait(false);

        const string schemaSql = """
            PRAGMA foreign_keys = ON;
            PRAGMA journal_mode = WAL;

            CREATE TABLE IF NOT EXISTS scan_sessions (
                session_id TEXT PRIMARY KEY,
                target_device_path TEXT NOT NULL,
                target_display_name TEXT NOT NULL,
                scan_mode INTEGER NOT NULL,
                status TEXT NOT NULL,
                started_utc TEXT NOT NULL,
                completed_utc TEXT NULL,
                discovered_file_count INTEGER NOT NULL DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS scan_configurations (
                config_id TEXT PRIMARY KEY,
                session_id TEXT NOT NULL,
                read_buffer_bytes INTEGER NOT NULL,
                carving_enabled INTEGER NOT NULL,
                created_utc TEXT NOT NULL,
                FOREIGN KEY (session_id) REFERENCES scan_sessions(session_id) ON DELETE CASCADE
            );

            -- Stores discovered file metadata only. Recovered file contents are never stored in SQLite.
            CREATE TABLE IF NOT EXISTS discovered_files (
                file_id TEXT PRIMARY KEY,
                session_id TEXT NOT NULL,
                file_name TEXT NOT NULL,
                original_path TEXT NULL,
                size_bytes INTEGER NOT NULL,
                confidence INTEGER NOT NULL,
                confidence_reason TEXT NOT NULL,
                created_utc TEXT NULL,
                modified_utc TEXT NULL,
                source_offset_bytes INTEGER NULL,
                FOREIGN KEY (session_id) REFERENCES scan_sessions(session_id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS recovery_history (
                recovery_id TEXT PRIMARY KEY,
                session_id TEXT NOT NULL,
                destination_path TEXT NOT NULL,
                recovered_file_count INTEGER NOT NULL,
                failed_file_count INTEGER NOT NULL,
                timestamp_utc TEXT NOT NULL,
                report_sha256 TEXT NULL,
                FOREIGN KEY (session_id) REFERENCES scan_sessions(session_id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS recovery_reports (
                report_id TEXT PRIMARY KEY,
                recovery_id TEXT NOT NULL,
                report_json TEXT NOT NULL,
                sha256_hex TEXT NOT NULL,
                created_utc TEXT NOT NULL,
                FOREIGN KEY (recovery_id) REFERENCES recovery_history(recovery_id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS user_preferences (
                pref_key TEXT PRIMARY KEY,
                pref_value TEXT NOT NULL,
                updated_utc TEXT NOT NULL
            );
            """;

        await using var cmd = conn.CreateCommand();
        cmd.CommandText = schemaSql;
        await cmd.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task UpsertScanSessionAsync(ScanSessionRecord session, CancellationToken cancellationToken = default)
    {
        await using var conn = new SqliteConnection(_connectionString);
        await conn.OpenAsync(cancellationToken).ConfigureAwait(false);

        await using var cmd = conn.CreateCommand();
        cmd.CommandText = """
            INSERT INTO scan_sessions (
                session_id, target_device_path, target_display_name, scan_mode,
                status, started_utc, completed_utc, discovered_file_count)
            VALUES ($id, $path, $name, $mode, $status, $started, $completed, $count)
            ON CONFLICT(session_id) DO UPDATE SET
                status = excluded.status,
                completed_utc = excluded.completed_utc,
                discovered_file_count = excluded.discovered_file_count;
            """;
        cmd.Parameters.AddWithValue("$id", session.SessionId);
        cmd.Parameters.AddWithValue("$path", session.TargetDevicePath);
        cmd.Parameters.AddWithValue("$name", session.TargetDisplayName);
        cmd.Parameters.AddWithValue("$mode", (int)session.Mode);
        cmd.Parameters.AddWithValue("$status", session.Status);
        cmd.Parameters.AddWithValue("$started", session.StartedUtc.ToString("O"));
        cmd.Parameters.AddWithValue("$completed", (object?)session.CompletedUtc?.ToString("O") ?? DBNull.Value);
        cmd.Parameters.AddWithValue("$count", session.DiscoveredFileCount);

        await cmd.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task InsertRecoveryHistoryAsync(RecoveryHistoryRecord record, CancellationToken cancellationToken = default)
    {
        await using var conn = new SqliteConnection(_connectionString);
        await conn.OpenAsync(cancellationToken).ConfigureAwait(false);

        await using var cmd = conn.CreateCommand();
        cmd.CommandText = """
            INSERT OR REPLACE INTO recovery_history (
                recovery_id, session_id, destination_path, recovered_file_count,
                failed_file_count, timestamp_utc, report_sha256)
            VALUES ($rid, $sid, $dest, $recCount, $failCount, $ts, $sha);
            """;
        cmd.Parameters.AddWithValue("$rid", record.RecoveryId);
        cmd.Parameters.AddWithValue("$sid", record.SessionId);
        cmd.Parameters.AddWithValue("$dest", record.DestinationPath);
        cmd.Parameters.AddWithValue("$recCount", record.RecoveredFileCount);
        cmd.Parameters.AddWithValue("$failCount", record.FailedFileCount);
        cmd.Parameters.AddWithValue("$ts", record.TimestampUtc.ToString("O"));
        cmd.Parameters.AddWithValue("$sha", (object?)record.ReportSha256 ?? DBNull.Value);

        await cmd.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task<IReadOnlyList<ScanSessionRecord>> GetRecentScanSessionsAsync(
        int limit = 20, CancellationToken cancellationToken = default)
    {
        var results = new List<ScanSessionRecord>();
        await using var conn = new SqliteConnection(_connectionString);
        await conn.OpenAsync(cancellationToken).ConfigureAwait(false);

        await using var cmd = conn.CreateCommand();
        cmd.CommandText = """
            SELECT session_id, target_device_path, target_display_name, scan_mode,
                   status, started_utc, completed_utc, discovered_file_count
            FROM scan_sessions
            ORDER BY started_utc DESC
            LIMIT $limit;
            """;
        cmd.Parameters.AddWithValue("$limit", Math.Clamp(limit, 1, 500));

        await using var reader = await cmd.ExecuteReaderAsync(cancellationToken).ConfigureAwait(false);
        while (await reader.ReadAsync(cancellationToken).ConfigureAwait(false))
        {
            results.Add(new ScanSessionRecord(
                SessionId: reader.GetString(0),
                TargetDevicePath: reader.GetString(1),
                TargetDisplayName: reader.GetString(2),
                Mode: (ScanMode)reader.GetInt32(3),
                Status: reader.GetString(4),
                StartedUtc: DateTimeOffset.Parse(reader.GetString(5)),
                CompletedUtc: reader.IsDBNull(6) ? null : DateTimeOffset.Parse(reader.GetString(6)),
                DiscoveredFileCount: reader.GetInt64(7)));
        }
        return results;
    }

    public async Task<IReadOnlyList<RecoveryHistoryRecord>> GetRecoveryHistoryAsync(
        int limit = 50, CancellationToken cancellationToken = default)
    {
        var results = new List<RecoveryHistoryRecord>();
        await using var conn = new SqliteConnection(_connectionString);
        await conn.OpenAsync(cancellationToken).ConfigureAwait(false);

        await using var cmd = conn.CreateCommand();
        cmd.CommandText = """
            SELECT recovery_id, session_id, destination_path, recovered_file_count,
                   failed_file_count, timestamp_utc, report_sha256
            FROM recovery_history
            ORDER BY timestamp_utc DESC
            LIMIT $limit;
            """;
        cmd.Parameters.AddWithValue("$limit", Math.Clamp(limit, 1, 500));

        await using var reader = await cmd.ExecuteReaderAsync(cancellationToken).ConfigureAwait(false);
        while (await reader.ReadAsync(cancellationToken).ConfigureAwait(false))
        {
            results.Add(new RecoveryHistoryRecord(
                RecoveryId: reader.GetString(0),
                SessionId: reader.GetString(1),
                DestinationPath: reader.GetString(2),
                RecoveredFileCount: reader.GetInt32(3),
                FailedFileCount: reader.GetInt32(4),
                TimestampUtc: DateTimeOffset.Parse(reader.GetString(5)),
                ReportSha256: reader.IsDBNull(6) ? null : reader.GetString(6)));
        }
        return results;
    }

    public async Task<string?> GetPreferenceAsync(string key, CancellationToken cancellationToken = default)
    {
        await using var conn = new SqliteConnection(_connectionString);
        await conn.OpenAsync(cancellationToken).ConfigureAwait(false);

        await using var cmd = conn.CreateCommand();
        cmd.CommandText = "SELECT pref_value FROM user_preferences WHERE pref_key = $key;";
        cmd.Parameters.AddWithValue("$key", key);
        object? result = await cmd.ExecuteScalarAsync(cancellationToken).ConfigureAwait(false);
        return result as string;
    }

    public async Task SetPreferenceAsync(string key, string value, CancellationToken cancellationToken = default)
    {
        await using var conn = new SqliteConnection(_connectionString);
        await conn.OpenAsync(cancellationToken).ConfigureAwait(false);

        await using var cmd = conn.CreateCommand();
        cmd.CommandText = """
            INSERT INTO user_preferences (pref_key, pref_value, updated_utc)
            VALUES ($key, $value, $updated)
            ON CONFLICT(pref_key) DO UPDATE SET
                pref_value = excluded.pref_value,
                updated_utc = excluded.updated_utc;
            """;
        cmd.Parameters.AddWithValue("$key", key);
        cmd.Parameters.AddWithValue("$value", value);
        cmd.Parameters.AddWithValue("$updated", DateTimeOffset.UtcNow.ToString("O"));
        await cmd.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
    }
}
