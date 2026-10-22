using System.Diagnostics;
using System.Reflection;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Threading;
using ProcessDiscovery.App;
using ProcessDiscovery.Application.Backend;
using ProcessDiscovery.Application.Presentation;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class MainWindowRefreshTests
{
    [Fact]
    public Task Loaded_StartsOneTenSecondTimer_AndCloseStopsIt() => OnStaAsync(async () =>
    {
        var backend = new ControlledBackend();
        var window = CreateWindow(backend);
        try
        {
            window.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent));
            window.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent));
            Assert.Single(backend.Requests);
            backend.Complete();
            await WaitForIdleAsync(window);

            var timer = Field<DispatcherTimer>(window, "_refreshTimer");
            Assert.Equal(TimeSpan.FromSeconds(10), timer.Interval);
            Assert.True(timer.IsEnabled);
            window.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent));
            Assert.Single(backend.Requests);

            Tick(window);
            Assert.Equal(2, backend.Requests.Count);
            backend.Complete();
            await WaitForIdleAsync(window);
            window.Close();
            Assert.False(timer.IsEnabled);
            Tick(window);
            Click(window);
            Assert.Equal(2, backend.Requests.Count);
        }
        finally { window.Close(); }
    });

    [Fact]
    public Task ManualAndTimerRefresh_ShareGuardAndBusyState() => OnStaAsync(async () =>
    {
        var backend = new ControlledBackend();
        var window = CreateWindow(backend);
        try
        {
            var button = Control<Button>(window, "RefreshButton");
            Assert.Equal("Atualizar", button.Content);
            Assert.Equal("Atualizar processos", AutomationProperties.GetName(button));
            Click(window);
            Assert.Single(backend.Requests);
            Assert.False(button.IsEnabled);
            Assert.True(window.State.IsBusy);
            Assert.Contains("em andamento", Control<TextBlock>(window, "StatusText").Text);

            Tick(window);
            Click(window); // Even a programmatically raised event must not bypass the guard.
            Assert.Single(backend.Requests);
            backend.Complete();
            await WaitForIdleAsync(window);
            Assert.True(button.IsEnabled);
            Assert.False(window.State.IsBusy);

            Tick(window);
            Click(window);
            Assert.Equal(2, backend.Requests.Count);
            backend.Complete();
            await WaitForIdleAsync(window);
            Assert.All(backend.Requests, request =>
            {
                Assert.Equal(QueryMode.Enumerate, request.Mode);
                Assert.False(request.IncludeSignature);
                Assert.False(request.IncludeModules);
            });
        }
        finally { window.Close(); }
    });

    [Theory]
    [InlineData("success")]
    [InlineData("fatal")]
    [InlineData("exception")]
    [InlineData("cancel")]
    public Task Refresh_AlwaysReenablesButton(string outcome) => OnStaAsync(async () =>
    {
        var backend = new ControlledBackend();
        var window = CreateWindow(backend);
        try
        {
            Click(window);
            Assert.False(Control<Button>(window, "RefreshButton").IsEnabled);
            switch (outcome)
            {
                case "fatal": backend.Complete(fatal: true); break;
                case "exception": backend.Fail(new InvalidOperationException("falha simulada")); break;
                case "cancel": Field<CancellationTokenSource>(window, "_refreshCancellation").Cancel(); break;
                default: backend.Complete(); break;
            }
            await WaitForIdleAsync(window);
            Assert.True(Control<Button>(window, "RefreshButton").IsEnabled);
            Assert.False(window.State.IsBusy);
            if (outcome is "fatal" or "exception")
                Assert.Contains("Falha", window.State.AccessibleStatus);
            if (outcome == "cancel")
                Assert.Contains("cancelada", window.State.AccessibleStatus);

            Click(window);
            Assert.Equal(2, backend.Requests.Count);
            backend.Complete();
            await WaitForIdleAsync(window);
        }
        finally { window.Close(); }
    });

    [Fact]
    public Task Closing_CancelsPendingRefresh_AndPreventsFurtherQueries() => OnStaAsync(async () =>
    {
        var backend = new ControlledBackend();
        var window = CreateWindow(backend);
        window.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent));
        var timer = Field<DispatcherTimer>(window, "_refreshTimer");
        var token = backend.LastToken;
        window.Close();
        Assert.True(token.IsCancellationRequested);
        Assert.False(timer.IsEnabled);
        await WaitForIdleAsync(window);
        Tick(window);
        Click(window);
        Assert.Single(backend.Requests);
        Assert.False(window.State.IsBusy);
    });

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public Task Refresh_PreservesSelectionWithoutQueryingDetails_AndClearsRemovedProcess(bool tree) => OnStaAsync(async () =>
    {
        var backend = new ControlledBackend();
        var window = CreateWindow(backend);
        try
        {
            window.State.ApplySnapshot(Snapshot(10, 20));
            window.ApplyState();
            if (tree)
            {
                window.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent));
                backend.Complete(Snapshot(10, 20));
                await WaitForIdleAsync(window);
                window.Show();
                window.UpdateLayout();
                var processTree = Control<TreeView>(window, "ProcessTree");
                var parent = Assert.IsType<TreeViewItem>(processTree.ItemContainerGenerator.ContainerFromIndex(0));
                parent.IsExpanded = true;
                window.UpdateLayout();
                var child = Assert.IsType<TreeViewItem>(parent.ItemContainerGenerator.ContainerFromIndex(0));
                child.IsSelected = true;
            }
            else
                Control<DataGrid>(window, "ProcessTable").SelectedItem = window.State.TableRows.Last();

            Assert.Equal(20u, window.State.Selection.ProcessId);
            var detailCount = backend.Requests.Count(request => request.Mode == QueryMode.ByPid);
            Assert.Equal(1, detailCount);
            Tick(window);
            backend.Complete(Snapshot(10, 20));
            await WaitForIdleAsync(window);
            Assert.Equal(20u, window.State.Selection.ProcessId);
            if (tree)
                Assert.Equal(20u, Assert.IsType<ProcessTreePresentationNode>(Control<TreeView>(window, "ProcessTree").SelectedItem).Pid);
            else
                Assert.Equal(20u, Assert.IsType<ProcessTableRow>(Control<DataGrid>(window, "ProcessTable").SelectedItem).Pid);
            Assert.Equal(detailCount, backend.Requests.Count(request => request.Mode == QueryMode.ByPid));

            Click(window);
            backend.Complete(Snapshot(10));
            await WaitForIdleAsync(window);
            Assert.Equal(ProcessSelection.Empty, window.State.Selection);
            Assert.Null(Control<DataGrid>(window, "ProcessTable").SelectedItem);
            Assert.Null(Control<TreeView>(window, "ProcessTree").SelectedItem);
            Assert.Equal("Nenhum processo selecionado.", Control<TextBlock>(window, "DetailPidText").Text);
            Assert.Equal(detailCount, backend.Requests.Count(request => request.Mode == QueryMode.ByPid));
        }
        finally { window.Close(); }
    });

    [Fact]
    public Task Refresh_PreservesSelectionChangedWhileEnumerationIsPending() => OnStaAsync(async () =>
    {
        var backend = new ControlledBackend();
        var window = CreateWindow(backend);
        try
        {
            window.State.ApplySnapshot(Snapshot(10, 20));
            window.ApplyState();
            var table = Control<DataGrid>(window, "ProcessTable");
            table.SelectedItem = window.State.TableRows.First();
            Tick(window);
            table.SelectedItem = window.State.TableRows.Last();
            backend.Complete();
            await WaitForIdleAsync(window);
            Assert.Equal(20u, window.State.Selection.ProcessId);
            Assert.Equal(20u, Assert.IsType<ProcessTableRow>(table.SelectedItem).Pid);
            Assert.Equal(2, backend.Requests.Count(request => request.Mode == QueryMode.ByPid));
        }
        finally { window.Close(); }
    });

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public Task PendingDetail_IsCancelledOnRemovalOrClose_AndLateResponseIsIgnored(bool close) => OnStaAsync(async () =>
    {
        var backend = new ControlledBackend { HoldDetails = true };
        var window = CreateWindow(backend);
        try
        {
            window.State.ApplySnapshot(Snapshot(10, 20));
            window.ApplyState();
            Control<DataGrid>(window, "ProcessTable").SelectedItem = window.State.TableRows.Last();
            var detailToken = backend.DetailToken;
            var detailCancellation = Field<CancellationTokenSource>(window, "_detailCancellation");
            if (close)
                window.Close();
            else
            {
                Tick(window);
                backend.Complete(Snapshot(10));
                await WaitForIdleAsync(window);
            }
            Assert.True(detailToken.IsCancellationRequested);
            backend.CompleteDetail(); // Simulate a backend returning despite cancellation.
            var watch = Stopwatch.StartNew();
            while (ReferenceEquals(detailCancellation,
                typeof(MainWindow).GetField("_detailCancellation", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)))
            {
                Assert.True(watch.Elapsed < TimeSpan.FromSeconds(10));
                await Dispatcher.Yield(DispatcherPriority.ApplicationIdle);
            }
            Assert.Equal("Nenhum processo selecionado.", Control<TextBlock>(window, "DetailPidText").Text);
        }
        finally { window.Close(); }
    });

    [Fact]
    public Task Refresh_RestoresTreeSelectionWhenNewRootsPushItOutsideViewport() => OnStaAsync(async () =>
    {
        var backend = new ControlledBackend();
        var window = CreateWindow(backend);
        try
        {
            window.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent));
            backend.Complete(Snapshot(5000));
            await WaitForIdleAsync(window);
            window.Show();
            window.UpdateLayout();
            var tree = Control<TreeView>(window, "ProcessTree");
            Assert.IsType<TreeViewItem>(tree.ItemContainerGenerator.ContainerFromIndex(0)).IsSelected = true;

            Tick(window);
            backend.Complete(Snapshot(Enumerable.Range(100, 1000).Select(pid => (uint)pid).Append(5000u).ToArray()));
            await WaitForIdleAsync(window);
            Assert.Equal(5000u, window.State.Selection.ProcessId);
            Assert.Equal(5000u, Assert.IsType<ProcessTreePresentationNode>(tree.SelectedItem).Pid);
            Assert.Single(backend.Requests, request => request.Mode == QueryMode.ByPid);
        }
        finally { window.Close(); }
    });

    private static MainWindow CreateWindow(ControlledBackend backend) =>
        new(new ProcessPresentationController(new ProcessQueryService(backend), new ProcessPresentationState()));

    private static T Control<T>(MainWindow window, string name) => Assert.IsType<T>(window.FindName(name));
    private static T Field<T>(MainWindow window, string name) =>
        Assert.IsType<T>(typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window));
    private static void Click(MainWindow window) =>
        Control<Button>(window, "RefreshButton").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
    private static void Tick(MainWindow window) =>
        typeof(MainWindow).GetMethod("RefreshTimer_OnTick", BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(window, [null, EventArgs.Empty]);

    private static async Task WaitForIdleAsync(MainWindow window)
    {
        var watch = Stopwatch.StartNew();
        while (Field<bool>(window, "_isRefreshing"))
        {
            Assert.True(watch.Elapsed < TimeSpan.FromSeconds(10), "A atualização não terminou.");
            await Dispatcher.Yield(DispatcherPriority.ApplicationIdle);
        }
    }

    private static Task OnStaAsync(Func<Task> test)
    {
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread = new Thread(() =>
        {
            var dispatcher = Dispatcher.CurrentDispatcher;
            SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(dispatcher));
            dispatcher.BeginInvoke(new Action(async () =>
            {
                try { await test(); completion.SetResult(); }
                catch (Exception exception) { completion.SetException(exception); }
                finally { dispatcher.BeginInvokeShutdown(DispatcherPriority.Send); }
            }));
            Dispatcher.Run();
        }) { IsBackground = true };
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        return completion.Task.WaitAsync(TimeSpan.FromSeconds(20));
    }

    private static ProcessSnapshot Snapshot(params uint[] pids) => new(
        pids.Select(pid => new ProcessRecord(pid,
            new(FieldState.Available, pid == 20 ? 10u : 0u, null),
            new(FieldState.Available, $"process-{pid}", null),
            new(FieldState.Available, "process.exe", null),
            new(FieldState.NotRequested, null, null),
            new(FieldState.Available, 1ul, null),
            ObservationConsistency.SnapshotOnly, [])).ToList(), ResultState.Complete, [], 0);

    private sealed class ControlledBackend : IProcessQueryBackend
    {
        private TaskCompletionSource<BackendResponse>? _pending;
        private readonly TaskCompletionSource<BackendResponse> _pendingDetail = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public List<QueryRequest> Requests { get; } = [];
        public CancellationToken LastToken { get; private set; }
        public CancellationToken DetailToken { get; private set; }
        public bool HoldDetails { get; init; }

        public Task<BackendResponse> QueryAsync(QueryRequest request, CancellationToken cancellationToken)
        {
            Requests.Add(request);
            if (request.Mode == QueryMode.ByPid)
            {
                DetailToken = cancellationToken;
                if (HoldDetails)
                    return _pendingDetail.Task;
                return Task.FromResult(new BackendResponse(Snapshot(request.ProcessId!.Value), false));
            }
            LastToken = cancellationToken;
            _pending = new(TaskCreationOptions.RunContinuationsAsynchronously);
            return _pending.Task.WaitAsync(cancellationToken);
        }

        public void Complete(ProcessSnapshot? snapshot = null, bool fatal = false) =>
            _pending!.SetResult(new BackendResponse(
                fatal ? new ProcessSnapshot([], ResultState.Failed, [], 0) : snapshot ?? Snapshot(10, 20),
                fatal, fatal ? "falha simulada" : null));
        public void Fail(Exception exception) => _pending!.SetException(exception);
        public void CompleteDetail() => _pendingDetail.SetResult(new BackendResponse(Snapshot(20), false));
    }
}
