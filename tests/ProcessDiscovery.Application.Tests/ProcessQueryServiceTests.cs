using ProcessDiscovery.Application.Backend;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class ProcessQueryServiceTests
{
    [Fact]
    public async Task QueryAsync_RejectsByPidWithoutPidBeforeBackend()
    {
        var backend = new FakeBackend();
        var service = new ProcessQueryService(backend);

        await Assert.ThrowsAsync<ArgumentException>(() =>
            service.QueryAsync(new(QueryMode.ByPid), CancellationToken.None));
        Assert.Equal(0, backend.CallCount);
    }

    [Fact]
    public async Task QueryAsync_PreservesPreviousSnapshotOnFatalResponse()
    {
        var snapshot = new ProcessSnapshot([], ResultState.Complete, [], 0);
        var backend = new FakeBackend(
            new BackendResponse(snapshot, false),
            new BackendResponse(new ProcessSnapshot([], ResultState.Failed, [], 0), true, "fatal"));
        var service = new ProcessQueryService(backend);

        await service.QueryAsync(new(QueryMode.Enumerate), CancellationToken.None);
        var response = await service.QueryAsync(new(QueryMode.Enumerate), CancellationToken.None);

        Assert.True(response.IsFatal);
        Assert.Same(snapshot, service.CurrentSnapshot);
    }

    [Fact]
    public async Task QueryAsync_ForwardsCancellationTokenAndRequest()
    {
        using var cancellation = new CancellationTokenSource();
        var backend = new FakeBackend(new BackendResponse(new ProcessSnapshot([], ResultState.Partial, [], 0), false));
        var service = new ProcessQueryService(backend);
        var request = new QueryRequest(QueryMode.ByPid, 42, IncludeModules: true);

        await service.QueryAsync(request, cancellation.Token);

        Assert.Same(request, backend.LastRequest);
        Assert.Equal(cancellation.Token, backend.LastToken);
    }

    [Fact]
    public void BackendResponse_RejectsFatalStatusMismatch()
    {
        var complete = new ProcessSnapshot([], ResultState.Complete, [], 0);
        var failed = new ProcessSnapshot([], ResultState.Failed, [], 0);

        Assert.Throws<ArgumentException>(() => new BackendResponse(complete, true));
        Assert.Throws<ArgumentException>(() => new BackendResponse(failed, false));
    }

    private sealed class FakeBackend(params BackendResponse[] responses) : IProcessQueryBackend
    {
        private readonly Queue<BackendResponse> _responses = new(responses);
        public int CallCount { get; private set; }
        public QueryRequest? LastRequest { get; private set; }
        public CancellationToken LastToken { get; private set; }

        public Task<BackendResponse> QueryAsync(QueryRequest request, CancellationToken cancellationToken)
        {
            CallCount++;
            LastRequest = request;
            LastToken = cancellationToken;
            return Task.FromResult(_responses.Count == 0
                ? new BackendResponse(new ProcessSnapshot([], ResultState.Complete, [], 0), false)
                : _responses.Dequeue());
        }
    }
}
