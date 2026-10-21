namespace ProcessDiscovery.Application.Backend;

public interface IProcessQueryBackend
{
    Task<BackendResponse> QueryAsync(QueryRequest request, CancellationToken cancellationToken);
}
