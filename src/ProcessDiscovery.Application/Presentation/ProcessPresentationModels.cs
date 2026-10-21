using ProcessDiscovery.Application.Backend;

namespace ProcessDiscovery.Application.Presentation;

public sealed record ProcessTableRow(
    uint Pid,
    string Name,
    string ParentPidText,
    string ParentName,
    string ImagePath,
    ResultState SnapshotState,
    string StateText,
    IReadOnlyList<DiagnosticRecord> Diagnostics,
    ProcessRecord Source);

public sealed class ProcessTreePresentationNode
{
    public ProcessTreePresentationNode(ProcessTreeNode source, string stateText)
    {
        Source = source;
        StateText = stateText;
        Children = source.Children
            .OrderBy(child => child.Process.Pid)
            .Select(child => new ProcessTreePresentationNode(child, stateText))
            .ToList();
    }

    public ProcessTreeNode Source { get; }
    public uint Pid => Source.Process.Pid;
    public string Name => Source.Process.Name.Value ?? "(nome indisponível)";
    public string StateText { get; }
    public IReadOnlyList<string> Diagnostics => Source.Diagnostics;
    public IReadOnlyList<ProcessTreePresentationNode> Children { get; }
}

public enum ProcessSelectionKind
{
    None,
    TableRow,
    TreeNode
}

public sealed record ProcessSelection(ProcessSelectionKind Kind, uint? ProcessId)
{
    public static ProcessSelection Empty { get; } = new(ProcessSelectionKind.None, null);
}
