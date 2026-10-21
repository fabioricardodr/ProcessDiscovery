using ProcessDiscovery.Application.Backend;

namespace ProcessDiscovery.Application.Presentation;

public sealed class ProcessPresentationState
{
    public IReadOnlyList<ProcessTableRow> TableRows { get; private set; } = [];
    public IReadOnlyList<ProcessTreePresentationNode> TreeRoots { get; private set; } = [];
    public ProcessSelection Selection { get; private set; } = ProcessSelection.Empty;
    public ProcessSnapshot? Snapshot { get; private set; }
    public bool IsBusy { get; private set; }
    public string AccessibleStatus { get; private set; } = "Nenhuma consulta executada.";

    public void ApplySnapshot(ProcessSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var roots = ProcessQueryDomain.BuildTree(snapshot);
        Snapshot = snapshot;
        TableRows = snapshot.Processes
            .OrderBy(process => process.Pid)
            .Select(process => ToTableRow(process, snapshot.State))
            .ToList();
        TreeRoots = roots
            .OrderBy(root => root.Process.Pid)
            .Select(root => new ProcessTreePresentationNode(root, StateText(snapshot.State)))
            .ToList();
        AccessibleStatus = $"{StateText(snapshot.State)}. {TableRows.Count} processos exibidos.";
        Selection = ProcessSelection.Empty;
    }

    public void SetFatal(string message)
    {
        IsBusy = false;
        AccessibleStatus = $"Falha: {message}";
    }

    public void SetCancelled()
    {
        IsBusy = false;
        AccessibleStatus = "Consulta cancelada; o snapshot anterior foi preservado.";
    }

    public void SetBusy(bool busy)
    {
        IsBusy = busy;
        if (busy) AccessibleStatus = "Consulta em andamento; a interface permanece disponível.";
    }

    public void SelectTableRow(uint processId) => Select(ProcessSelectionKind.TableRow, processId);
    public void SelectTreeNode(uint processId) => Select(ProcessSelectionKind.TreeNode, processId);

    private void Select(ProcessSelectionKind kind, uint processId)
    {
        if (Snapshot?.Processes.Any(process => process.Pid == processId) != true)
        {
            Selection = ProcessSelection.Empty;
            return;
        }
        Selection = new ProcessSelection(kind, processId);
    }

    private static ProcessTableRow ToTableRow(ProcessRecord process, ResultState state) =>
        new(
            process.Pid,
            process.Name.Value ?? "(nome indisponível)",
            process.ParentPid.Value?.ToString() ?? "(PPID indisponível)",
            process.ParentName.Value ?? "(pai indisponível)",
            process.ImagePath.Value ?? "(caminho indisponível)",
            state,
            StateText(state),
            process.Diagnostics,
            process);

    private static string StateText(ResultState state) => state switch
    {
        ResultState.Complete => "Completo",
        ResultState.Partial => "Parcial",
        ResultState.Failed => "Falha",
        _ => "Estado desconhecido"
    };
}
