namespace ProcessDiscovery.Application.Backend;

public enum QueryMode
{
    Enumerate,
    ByPid
}

public enum ResultState { Complete, Partial, Failed }
public enum FieldState { Available, Unavailable, NotRequested, AccessDenied, ProcessExited, UnsupportedArchitecture, AvailableUnverified, Error }
public enum ObservationConsistency { SnapshotOnly, HandleOnly, BestEffortUnverified, MismatchDetected }
public enum ModuleCollectionState { Complete, Partial }
public enum SignaturePresence { Present, NoEmbeddedSignature, Unknown }
public enum SignatureTrust { Valid, Invalid, Indeterminate, NotEvaluated }

public sealed record QueryRequest(
    QueryMode Mode,
    uint? ProcessId = null,
    bool IncludeImagePath = true,
    bool IncludeInternalName = false,
    bool IncludeSignature = false,
    bool IncludeModules = false,
    bool AllowRemoteFileInspection = false);

public sealed record BackendResponse
{
    public BackendResponse(ProcessSnapshot snapshot, bool isFatal, string? fatalMessage = null)
    {
        Snapshot = snapshot ?? throw new ArgumentNullException(nameof(snapshot));
        IsFatal = isFatal;
        FatalMessage = fatalMessage;
        if (isFatal != (snapshot.State == ResultState.Failed))
            throw new ArgumentException("Fatal status must match failed state.", nameof(snapshot));
    }

    public ProcessSnapshot Snapshot { get; }
    public bool IsFatal { get; }
    public string? FatalMessage { get; }
}

public sealed record ProcessSnapshot(
    IReadOnlyList<ProcessRecord> Processes,
    ResultState State,
    IReadOnlyList<DiagnosticRecord> Diagnostics,
    int UnevaluableProcesses);

public sealed record ProcessRecord(
    uint Pid,
    FieldValue<uint?> ParentPid,
    FieldValue<string> Name,
    FieldValue<string> ImagePath,
    FieldValue<string> ParentName,
    FieldValue<ulong?> CreationTime100Ns,
    ObservationConsistency ObservationConsistency,
    IReadOnlyList<DiagnosticRecord> Diagnostics,
    FieldValue<string>? InternalName = null,
    FieldValue<ModuleCollection>? Modules = null,
    FieldValue<SignatureRecord>? Signature = null);

public sealed record ModuleRecord(
    FieldValue<string> Name,
    FieldValue<string> Path,
    ulong BaseAddress);

public sealed record ModuleCollection(
    IReadOnlyList<ModuleRecord> Items,
    ModuleCollectionState State,
    IReadOnlyList<DiagnosticRecord> Diagnostics);

public sealed record SignatureRecord(
    SignaturePresence Presence,
    SignatureTrust Trust,
    int WinTrustStatus,
    uint? LastError,
    string Policy);

public sealed record FieldValue<T>
{
    public FieldValue(FieldState state, T? value, DiagnosticRecord? diagnostic)
    {
        if (state == FieldState.NotRequested && value is not null)
            throw new ArgumentException("Not requested fields cannot contain a value.", nameof(value));
        if (state == FieldState.Error && diagnostic is null)
            throw new ArgumentException("Error fields require a diagnostic.", nameof(diagnostic));
        if (state is (FieldState.Unavailable or FieldState.AccessDenied or FieldState.ProcessExited or FieldState.UnsupportedArchitecture or FieldState.Error) && value is not null)
            throw new ArgumentException("Unavailable fields cannot contain a value.", nameof(value));

        State = state;
        Value = value;
        Diagnostic = diagnostic;
    }

    public FieldState State { get; }
    public T? Value { get; }
    public DiagnosticRecord? Diagnostic { get; }
}

public sealed record DiagnosticRecord(
    string DiagnosticId,
    string DiagnosticKind,
    string Origin,
    string Operation,
    uint NativeCode,
    string Message,
    string Domain,
    string Scope,
    uint? ProcessPid = null,
    string? Field = null,
    string? Collection = null);
