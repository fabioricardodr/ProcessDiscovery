using ProcessDiscovery.Application.Backend;

namespace ProcessDiscovery.Application.Presentation;

public sealed class ProcessPresentationController
{
    private readonly ProcessQueryService _service;

    public ProcessPresentationController(ProcessQueryService service, ProcessPresentationState state)
    {
        _service = service ?? throw new ArgumentNullException(nameof(service));
        State = state ?? throw new ArgumentNullException(nameof(state));
    }

    public ProcessPresentationState State { get; }

    public async Task<BackendResponse> RefreshAsync(QueryRequest request, CancellationToken cancellationToken)
    {
        State.SetBusy(true);
        try
        {
            var response = await _service.QueryAsync(request, cancellationToken).ConfigureAwait(false);
            if (!response.IsFatal)
                State.ApplySnapshot(response.Snapshot);
            else
                State.SetFatal(response.FatalMessage ?? "A consulta falhou.");
            return response;
        }
        catch (OperationCanceledException)
        {
            State.SetCancelled();
            throw;
        }
        finally
        {
            State.SetBusy(false);
        }
    }

    public void SelectTableRow(uint processId) => State.SelectTableRow(processId);
    public void SelectTreeNode(uint processId) => State.SelectTreeNode(processId);

    public Task<BackendResponse> QueryDetailAsync(QueryRequest request, CancellationToken cancellationToken) =>
        _service.QueryAsync(request, cancellationToken);
}
