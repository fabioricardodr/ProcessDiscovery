using ProcessDiscovery.Application.Backend;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class JsonProcessQueryBackendTests
{
    [Fact]
    public async Task QueryAsync_ForwardsRequestAndCancellationToJsonProvider()
    {
        QueryRequest? receivedRequest = null;
        CancellationToken receivedToken = default;
        var backend = new JsonProcessQueryBackend((request, token) =>
        {
            receivedRequest = request;
            receivedToken = token;
            return Task.FromResult("""
            {"schema_version":3,"processes":[],"state":"complete","fatal_error":null,"diagnostics":[],"partial_reasons":[],"unevaluable_processes":0}
            """);
        });
        using var cancellation = new CancellationTokenSource();
        var request = new QueryRequest(QueryMode.ByPid, 42, IncludeModules: true, IncludeSignature: true);

        var response = await backend.QueryAsync(request, cancellation.Token);

        Assert.False(response.IsFatal);
        Assert.Same(request, receivedRequest);
        Assert.Equal(cancellation.Token, receivedToken);
    }

    [Fact]
    public async Task QueryAsync_RejectsCancellationBeforeCallingProvider()
    {
        var called = false;
        var backend = new JsonProcessQueryBackend((_, _) =>
        {
            called = true;
            return Task.FromResult("{}");
        });
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            backend.QueryAsync(new(QueryMode.Enumerate), cancellation.Token));
        Assert.False(called);
    }
}
