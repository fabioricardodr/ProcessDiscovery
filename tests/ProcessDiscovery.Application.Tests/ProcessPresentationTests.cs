using ProcessDiscovery.Application.Backend;
using ProcessDiscovery.Application.Presentation;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class ProcessPresentationTests
{
    [Fact]
    public void ApplySnapshot_OrdersTableByPidAndBuildsTreeByPidPpid()
    {
        var state = new ProcessPresentationState();
        var parent = Process(10, 0, "parent");
        var child = Process(20, 10, "child");

        state.ApplySnapshot(new ProcessSnapshot([child, parent], ResultState.Complete, [], 0));

        Assert.Equal([10u, 20u], state.TableRows.Select(row => row.Pid));
        Assert.Single(state.TreeRoots);
        Assert.Equal(10u, state.TreeRoots[0].Pid);
        Assert.Single(state.TreeRoots[0].Children);
        Assert.Equal(20u, state.TreeRoots[0].Children[0].Pid);
    }

    [Fact]
    public void ApplySnapshot_ExposesMissingParentAndAccessibleState()
    {
        var state = new ProcessPresentationState();
        state.ApplySnapshot(new ProcessSnapshot(
            [Process(20, 99, "orphan")], ResultState.Partial, [], 1));

        Assert.Single(state.TreeRoots);
        Assert.Contains(state.TreeRoots[0].Diagnostics, value => value.Contains("parent"));
        Assert.Contains("Parcial", state.AccessibleStatus);
        Assert.Contains("1 processos", state.AccessibleStatus);
    }

    [Fact]
    public void Selection_OnlySelectsProcessesInCurrentSnapshot()
    {
        var state = new ProcessPresentationState();
        state.ApplySnapshot(new ProcessSnapshot([Process(10, 0, "p")], ResultState.Complete, [], 0));

        state.SelectTableRow(10);
        Assert.Equal(ProcessSelectionKind.TableRow, state.Selection.Kind);
        Assert.Equal(10u, state.Selection.ProcessId);

        state.SelectTreeNode(99);
        Assert.Equal(ProcessSelection.Empty, state.Selection);
    }

    [Fact]
    public async Task Controller_PreservesPresentationSnapshotOnFatalResponse()
    {
        var oldSnapshot = new ProcessSnapshot([Process(10, 0, "old")], ResultState.Complete, [], 0);
        var backend = new FakeBackend(
            new BackendResponse(oldSnapshot, false),
            new BackendResponse(new ProcessSnapshot([], ResultState.Failed, [], 0), true, "bridge"));
        var service = new ProcessQueryService(backend);
        var state = new ProcessPresentationState();
        var controller = new ProcessPresentationController(service, state);

        await controller.RefreshAsync(new(QueryMode.Enumerate), CancellationToken.None);
        var response = await controller.RefreshAsync(new(QueryMode.Enumerate), CancellationToken.None);

        Assert.True(response.IsFatal);
        Assert.Equal(10u, state.TableRows.Single().Pid);
        Assert.Contains("Falha", state.AccessibleStatus);
    }

    [Fact]
    public async Task Controller_PublishesCancellationStateAndClearsBusy()
    {
        var backend = new CancellingBackend();
        var controller = new ProcessPresentationController(
            new ProcessQueryService(backend), new ProcessPresentationState());
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            controller.RefreshAsync(new(QueryMode.Enumerate), cancellation.Token));

        Assert.False(controller.State.IsBusy);
        Assert.Contains("cancelada", controller.State.AccessibleStatus, StringComparison.OrdinalIgnoreCase);
    }

    private sealed class FakeBackend(params BackendResponse[] responses) : IProcessQueryBackend
    {
        private readonly Queue<BackendResponse> _responses = new(responses);
        public Task<BackendResponse> QueryAsync(QueryRequest request, CancellationToken cancellationToken) =>
            Task.FromResult(_responses.Dequeue());
    }

    private sealed class CancellingBackend : IProcessQueryBackend
    {
        public Task<BackendResponse> QueryAsync(QueryRequest request, CancellationToken cancellationToken) =>
            Task.FromCanceled<BackendResponse>(cancellationToken);
    }

    private static ProcessRecord Process(uint pid, uint ppid, string name) =>
        new(pid,
            new(FieldState.Available, ppid, null),
            new(FieldState.Available, name, null),
            new(FieldState.Available, name + ".exe", null),
            new(FieldState.NotRequested, null, null),
            new(FieldState.Available, 1ul, null),
            ObservationConsistency.SnapshotOnly, []);
}
