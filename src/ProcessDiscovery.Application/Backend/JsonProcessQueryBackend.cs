namespace ProcessDiscovery.Application.Backend;

public sealed class JsonProcessQueryBackend(
    Func<QueryRequest, CancellationToken, Task<string>> queryJson)
    : IProcessQueryBackend
{
    private readonly Func<QueryRequest, CancellationToken, Task<string>> _queryJson =
        queryJson ?? throw new ArgumentNullException(nameof(queryJson));

    public async Task<BackendResponse> QueryAsync(
        QueryRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();

        var json = await _queryJson(request, cancellationToken).ConfigureAwait(false);
        cancellationToken.ThrowIfCancellationRequested();
        var snapshot = ProcessResultV3Deserializer.Deserialize(json);
        return new BackendResponse(
            snapshot,
            snapshot.State == ResultState.Failed,
            snapshot.State == ResultState.Failed ? "A bridge retornou uma falha." : null);
    }
}
