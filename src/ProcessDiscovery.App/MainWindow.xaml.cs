using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using ProcessDiscovery.Application.Backend;
using ProcessDiscovery.Application.Presentation;

namespace ProcessDiscovery.App;

public partial class MainWindow : Window
{
    private bool _applyingState;
    private readonly ProcessPresentationController? _controller;
    private CancellationTokenSource? _refreshCancellation;
    private CancellationTokenSource? _detailCancellation;
    private readonly DispatcherTimer _refreshTimer;
    private bool _isRefreshing;
    private bool _isClosed;
    private ProcessSelection _selection = ProcessSelection.Empty;

    public MainWindow(ProcessPresentationState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        State = state;
        _selection = state.Selection;
        _refreshTimer = new DispatcherTimer(DispatcherPriority.Background, Dispatcher)
        {
            Interval = TimeSpan.FromSeconds(10)
        };
        _refreshTimer.Tick += RefreshTimer_OnTick;
        Closed += MainWindow_OnClosed;
        InitializeComponent();
        ApplyState();
    }

    public MainWindow()
        : this(CreateController())
    {
    }

    public MainWindow(ProcessPresentationController controller)
        : this(controller?.State ?? throw new ArgumentNullException(nameof(controller)))
    {
        _controller = controller;
        RefreshButton.IsEnabled = true;
        Loaded += MainWindow_OnLoaded;
    }

    private static ProcessPresentationController CreateController()
    {
        var backend = new NativeBridgeProcessQueryBackend(NativeBridgePathResolver.Resolve());
        var service = new ProcessQueryService(backend);
        return new ProcessPresentationController(service, new ProcessPresentationState());
    }

    public ProcessPresentationState State { get; }

    public void ApplyState()
    {
        var selectedTablePid = ProcessTable.SelectedItem is ProcessTableRow selectedRow
            ? selectedRow.Pid
            : (uint?)null;
        var selectedTreePid = (ProcessTree.SelectedItem as ProcessTreePresentationNode)?.Pid;
        var treeChanged = !ReferenceEquals(ProcessTree.ItemsSource, State.TreeRoots);

        _applyingState = true;
        try
        {
            ProcessTable.ItemsSource = State.TableRows;
            ProcessTree.ItemsSource = State.TreeRoots;
            StatusText.Text = State.AccessibleStatus;
            AutomationProperties.SetName(StatusText, $"Status da consulta: {State.AccessibleStatus}");
            RefreshButton.IsEnabled = _controller is not null && !_isRefreshing && !_isClosed;

            if (selectedTablePid is uint pid)
                ProcessTable.SelectedItem = State.TableRows.FirstOrDefault(row => row.Pid == pid);
            if (treeChanged && selectedTreePid is uint treePid)
                RestoreTreeSelection(treePid);

            if (_selection.ProcessId is uint selectedPid)
            {
                if (_selection.Kind == ProcessSelectionKind.TreeNode)
                    State.SelectTreeNode(selectedPid);
                else
                    State.SelectTableRow(selectedPid);

                _selection = State.Selection;
                if (_selection == ProcessSelection.Empty)
                {
                    _detailCancellation?.Cancel();
                    ClearDetail();
                }
            }
        }
        finally
        {
            _applyingState = false;
        }
    }

    private void RestoreTreeSelection(uint processId)
    {
        var path = new List<ProcessTreePresentationNode>();
        if (!FindTreePath(State.TreeRoots, processId, path))
            return;

        ItemsControl parent = ProcessTree;
        foreach (var node in path)
        {
            parent.ApplyTemplate();
            parent.UpdateLayout();
            if (parent.ItemContainerGenerator.ContainerFromItem(node) is not TreeViewItem)
            {
                // Realize only the selected branch, including virtualized ancestors.
                FindVisualChild<VirtualizingStackPanel>(parent)?
                    .BringIndexIntoViewPublic(parent.Items.IndexOf(node));
                parent.UpdateLayout();
            }
            if (parent.ItemContainerGenerator.ContainerFromItem(node) is not TreeViewItem item)
                return;

            if (node.Pid == processId)
                item.IsSelected = true;
            else
                item.IsExpanded = true;
            parent = item;
        }
    }

    private static bool FindTreePath(
        IReadOnlyList<ProcessTreePresentationNode> nodes, uint processId, List<ProcessTreePresentationNode> path)
    {
        foreach (var node in nodes)
        {
            path.Add(node);
            if (node.Pid == processId || FindTreePath(node.Children, processId, path))
                return true;
            path.RemoveAt(path.Count - 1);
        }
        return false;
    }

    private static T? FindVisualChild<T>(DependencyObject parent) where T : DependencyObject
    {
        for (var index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
        {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T match)
                return match;
            if (FindVisualChild<T>(child) is T descendant)
                return descendant;
        }
        return null;
    }

    public void ApplyStateOnUiThread()
    {
        if (Dispatcher.CheckAccess())
            ApplyState();
        else
            Dispatcher.Invoke(ApplyState);
    }

    private void ProcessTable_OnSelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (!_applyingState && ProcessTable.SelectedItem is ProcessTableRow row)
        {
            State.SelectTableRow(row.Pid);
            _selection = State.Selection;
            _ = LoadDetailAsync(row.Pid);
        }
    }

    private void ProcessTree_OnSelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (!_applyingState && e.NewValue is ProcessTreePresentationNode node)
        {
            State.SelectTreeNode(node.Pid);
            _selection = State.Selection;
            _ = LoadDetailAsync(node.Pid);
        }
    }

    private async void MainWindow_OnLoaded(object sender, RoutedEventArgs e)
    {
        Loaded -= MainWindow_OnLoaded;
        if (_isClosed)
            return;
        await RefreshAsync();
        if (!_isClosed)
            _refreshTimer.Start();
    }

    private async void RefreshTimer_OnTick(object? sender, EventArgs e) => await RefreshAsync();

    private async void RefreshButton_OnClick(object sender, RoutedEventArgs e) => await RefreshAsync();

    private void MainWindow_OnClosed(object? sender, EventArgs e)
    {
        _isClosed = true;
        Loaded -= MainWindow_OnLoaded;
        _refreshTimer.Stop();
        _refreshTimer.Tick -= RefreshTimer_OnTick;
        _refreshCancellation?.Cancel();
        _detailCancellation?.Cancel();
        _controller?.State.SetBusy(false);
    }

    private async Task RefreshAsync()
    {
        if (_controller is null || _isClosed || _isRefreshing)
            return;

        _isRefreshing = true;
        using var cancellation = new CancellationTokenSource();
        _refreshCancellation = cancellation;

        try
        {
            State.SetBusy(true);
            ApplyStateOnUiThread();
            await _controller.RefreshAsync(
                new QueryRequest(
                    QueryMode.Enumerate,
                    IncludeImagePath: true,
                    IncludeInternalName: true,
                    IncludeSignature: false,
                    IncludeModules: false),
                cancellation.Token);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            if (!_isClosed)
                State.SetFatal(exception.Message);
        }
        finally
        {
            _refreshCancellation = null;
            _isRefreshing = false;
            State.SetBusy(false);
            if (!_isClosed)
                ApplyStateOnUiThread();
        }
    }

    private async Task LoadDetailAsync(uint processId)
    {
        if (_controller is null || State.Snapshot is null || _isClosed)
            return;

        var selected = State.Snapshot.Processes.FirstOrDefault(process => process.Pid == processId);
        if (selected is null)
            return;

        _detailCancellation?.Cancel();
        using var cancellation = new CancellationTokenSource();
        _detailCancellation = cancellation;

        try
        {
            var response = await _controller.QueryDetailAsync(
                new QueryRequest(
                    QueryMode.ByPid,
                    processId,
                    IncludeImagePath: true,
                    IncludeInternalName: true,
                    IncludeSignature: true,
                    IncludeModules: true),
                cancellation.Token);

            if (!response.IsFatal && !cancellation.IsCancellationRequested && !_isClosed
                && _selection.ProcessId == processId)
            {
                var detail = response.Snapshot.Processes.FirstOrDefault(process => process.Pid == processId);
                if (detail is not null)
                    ApplyDetail(ProcessDetailPresentation.From(selected, detail));
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            if (!_isClosed && !cancellation.IsCancellationRequested && _selection.ProcessId == processId)
                DetailPidText.Text = $"Falha ao consultar PID {processId}: {exception.Message}";
        }
        finally
        {
            if (ReferenceEquals(_detailCancellation, cancellation))
                _detailCancellation = null;
        }
    }

    private void ClearDetail()
    {
        DetailPidText.Text = "Nenhum processo selecionado.";
        DetailInternalNameText.Text = "-";
        DetailIdentityText.Text = "-";
        DetailSignatureText.Text = "-";
        DetailModulesText.Text = "-";
        DetailConsistencyText.Text = "-";
    }

    private void ApplyDetail(ProcessDetailPresentation detail)
    {
        DetailPidText.Text = $"{detail.Pid} - {detail.Name}";
        DetailInternalNameText.Text = detail.InternalName;
        DetailIdentityText.Text = detail.IdentityText;
        DetailSignatureText.Text = detail.Signature is null
            ? detail.SignatureStateText
            : $"{detail.Signature.Trust} ({detail.Signature.Presence})";
        DetailModulesText.Text = $"{detail.Modules.Count} ({detail.ModulesStateText})";
        DetailConsistencyText.Text = detail.ConsistencyText;
    }
}
