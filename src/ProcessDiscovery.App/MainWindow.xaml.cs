using System.Windows;
using System.Windows.Automation;
using ProcessDiscovery.Application.Backend;
using ProcessDiscovery.Application.Presentation;

namespace ProcessDiscovery.App;

public partial class MainWindow : Window
{
    private bool _applyingState;
    private readonly ProcessPresentationController? _controller;
    private CancellationTokenSource? _refreshCancellation;
    private CancellationTokenSource? _detailCancellation;

    public MainWindow(ProcessPresentationState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        State = state;
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
        Loaded += MainWindow_OnLoaded;
        Closed += MainWindow_OnClosed;
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

        _applyingState = true;
        try
        {
            ProcessTable.ItemsSource = State.TableRows;
            ProcessTree.ItemsSource = State.TreeRoots;
            StatusText.Text = State.AccessibleStatus;
            AutomationProperties.SetName(StatusText, $"Status da consulta: {State.AccessibleStatus}");

            if (selectedTablePid is uint pid)
                ProcessTable.SelectedItem = State.TableRows.FirstOrDefault(row => row.Pid == pid);
        }
        finally
        {
            _applyingState = false;
        }
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
            _ = LoadDetailAsync(row.Pid);
        }
    }

    private void ProcessTree_OnSelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (!_applyingState && e.NewValue is ProcessTreePresentationNode node)
        {
            State.SelectTreeNode(node.Pid);
            _ = LoadDetailAsync(node.Pid);
        }
    }

    private async void MainWindow_OnLoaded(object sender, RoutedEventArgs e)
    {
        Loaded -= MainWindow_OnLoaded;
        await RefreshAsync();
    }

    private void MainWindow_OnClosed(object? sender, EventArgs e)
    {
        _refreshCancellation?.Cancel();
        _refreshCancellation?.Dispose();
        _detailCancellation?.Cancel();
        _detailCancellation?.Dispose();
        _controller?.State.SetBusy(false);
    }

    private async Task RefreshAsync()
    {
        if (_controller is null)
            return;

        _refreshCancellation?.Cancel();
        _refreshCancellation?.Dispose();
        _refreshCancellation = new CancellationTokenSource();
        ApplyStateOnUiThread();

        try
        {
            await _controller.RefreshAsync(
                new QueryRequest(
                    QueryMode.Enumerate,
                    IncludeImagePath: true,
                    IncludeInternalName: true,
                    IncludeSignature: false,
                    IncludeModules: false),
                _refreshCancellation.Token);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            State.SetFatal(exception.Message);
        }
        finally
        {
            ApplyStateOnUiThread();
        }
    }

    private async Task LoadDetailAsync(uint processId)
    {
        if (_controller is null || State.Snapshot is null)
            return;

        var selected = State.Snapshot.Processes.FirstOrDefault(process => process.Pid == processId);
        if (selected is null)
            return;

        _detailCancellation?.Cancel();
        _detailCancellation?.Dispose();
        _detailCancellation = new CancellationTokenSource();

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
                _detailCancellation.Token);

            if (!response.IsFatal)
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
            DetailPidText.Text = $"Falha ao consultar PID {processId}: {exception.Message}";
        }
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
