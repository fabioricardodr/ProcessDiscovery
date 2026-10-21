namespace ProcessDiscovery.Application.Backend;

public sealed class ProcessQueryService(IProcessQueryBackend backend)
{
    private readonly IProcessQueryBackend _backend = backend ?? throw new ArgumentNullException(nameof(backend));
    private ProcessSnapshot? _currentSnapshot;

    public ProcessSnapshot? CurrentSnapshot => _currentSnapshot;

    public async Task<BackendResponse> QueryAsync(QueryRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        ValidateRequest(request);

        var response = await _backend.QueryAsync(request, cancellationToken).ConfigureAwait(false);
        if (!response.IsFatal)
            _currentSnapshot = response.Snapshot;
        return response;
    }

    private static void ValidateRequest(QueryRequest request)
    {
        if (request.Mode == QueryMode.ByPid && request.ProcessId is null)
            throw new ArgumentException("ByPid requires a process id.", nameof(request));
        if (request.Mode == QueryMode.Enumerate && request.ProcessId is not null)
            throw new ArgumentException("Enumerate cannot include a process id.", nameof(request));
    }
}
