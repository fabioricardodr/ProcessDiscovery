namespace ProcessDiscovery.Application.Backend;

public static class ProcessQueryDomain
{
    public static bool TryParseProcessId(string? text, out uint processId)
    {
        return uint.TryParse(text, System.Globalization.NumberStyles.None,
            System.Globalization.CultureInfo.InvariantCulture, out processId);
    }

    public static IReadOnlyList<ProcessTreeNode> BuildTree(ProcessSnapshot snapshot)
    {
        var nodes = new Dictionary<uint, ProcessTreeNode>();
        var roots = new List<ProcessTreeNode>();
        foreach (var process in snapshot.Processes)
        {
            if (!nodes.TryAdd(process.Pid, new ProcessTreeNode(process)))
            {
                var duplicate = new ProcessTreeNode(process);
                duplicate.AddDiagnostic("duplicate pid in snapshot");
                roots.Add(duplicate);
            }
        }

        foreach (var node in nodes.Values)
        {
            if (!TryGetParentPid(node.Process, out var parentPid) ||
                parentPid == node.Process.Pid ||
                node.Process.ObservationConsistency == ObservationConsistency.MismatchDetected)
            {
                node.AddDiagnostic("parent relationship not established");
                roots.Add(node);
                continue;
            }

            if (!nodes.TryGetValue(parentPid, out var parent) || CreatesCycle(node, parent))
            {
                node.AddDiagnostic("parent not observed or cycle detected");
                roots.Add(node);
                continue;
            }

            node.Parent = parent;
            parent.Children.Add(node);
        }

        return roots;
    }

    public static IdentityAssessment AssessIdentity(ProcessRecord selected, ProcessRecord detail)
    {
        var selectedTime = selected.CreationTime100Ns;
        var detailTime = detail.CreationTime100Ns;
        if (selected.Pid != detail.Pid)
            return IdentityAssessment.PidReused;
        if (selectedTime.State is (FieldState.Available or FieldState.AvailableUnverified) &&
            detailTime.State is (FieldState.Available or FieldState.AvailableUnverified))
        {
            if (selectedTime.Value is ulong selectedCreationTime &&
                detailTime.Value is ulong detailCreationTime)
            {
                return selectedCreationTime == detailCreationTime &&
                    selected.ObservationConsistency != ObservationConsistency.MismatchDetected &&
                    detail.ObservationConsistency != ObservationConsistency.MismatchDetected
                    ? IdentityAssessment.Confirmed
                    : IdentityAssessment.PidReused;
            }

            return IdentityAssessment.Unconfirmed;
        }

        var nameDiffers = selected.Name.Value is not null && detail.Name.Value is not null &&
                          !String.Equals(selected.Name.Value, detail.Name.Value, StringComparison.Ordinal);
        var pathDiffers = selected.ImagePath.Value is not null && detail.ImagePath.Value is not null &&
                          !String.Equals(selected.ImagePath.Value, detail.ImagePath.Value, StringComparison.Ordinal);
        return nameDiffers || pathDiffers
            ? IdentityAssessment.PossibleReuse
            : IdentityAssessment.Unconfirmed;
    }

    private static bool TryGetParentPid(ProcessRecord process, out uint parentPid)
    {
        parentPid = 0;
        if (process.ParentPid.State is not (FieldState.Available or FieldState.AvailableUnverified) ||
            process.ParentPid.Value is not uint value)
            return false;

        parentPid = value;
        return true;
    }

    private static bool CreatesCycle(ProcessTreeNode child, ProcessTreeNode parent)
    {
        for (var current = parent; current is not null; current = current.Parent)
            if (current == child) return true;
        return false;
    }
}

public sealed class ProcessTreeNode(ProcessRecord process)
{
    public ProcessRecord Process { get; } = process;
    public ProcessTreeNode? Parent { get; internal set; }
    public List<ProcessTreeNode> Children { get; } = [];
    public List<string> Diagnostics { get; } = [];

    internal void AddDiagnostic(string message) => Diagnostics.Add(message);
}

public enum IdentityAssessment
{
    Confirmed,
    PidReused,
    PossibleReuse,
    Unconfirmed
}
