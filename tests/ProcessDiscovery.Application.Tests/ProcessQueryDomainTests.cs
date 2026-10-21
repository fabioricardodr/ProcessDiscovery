using ProcessDiscovery.Application.Backend;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class ProcessQueryDomainTests
{
    [Theory]
    [InlineData("0", true, 0u)]
    [InlineData("4294967295", true, uint.MaxValue)]
    [InlineData("", false, 0u)]
    [InlineData("-1", false, 0u)]
    [InlineData("4294967296", false, 0u)]
    [InlineData("1.0", false, 0u)]
    public void ProcessIdParser_EnforcesDecimalUInt32(string text, bool expected, uint value)
    {
        var actual = ProcessQueryDomain.TryParseProcessId(text, out var parsed);
        Assert.Equal(expected, actual);
        if (expected) Assert.Equal(value, parsed);
    }

    [Fact]
    public void BuildTree_UsesPidAndPpidAndRootsMissingParent()
    {
        var child = Process(20, 10, "child");
        var missingParentChild = Process(30, 99, "orphan");
        var parent = Process(10, 0, "parent");

        var roots = ProcessQueryDomain.BuildTree(Snapshot(parent, child, missingParentChild));

        Assert.Equal(2, roots.Count);
        Assert.Single(roots.Single(x => x.Process.Pid == 10).Children);
        Assert.Contains(roots.Single(x => x.Process.Pid == 30).Diagnostics,
            value => value.Contains("parent", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void BuildTree_DoesNotLinkSelfParentOrMismatch()
    {
        var self = Process(10, 10, "self");
        var mismatch = Process(20, 10, "mismatch", ObservationConsistency.MismatchDetected);

        var roots = ProcessQueryDomain.BuildTree(Snapshot(self, mismatch));

        Assert.Equal(2, roots.Count);
        Assert.All(roots, node => Assert.Empty(node.Children));
    }

    [Fact]
    public void BuildTree_DetectsCycleWithoutBlocking()
    {
        var first = Process(10, 20, "first");
        var second = Process(20, 10, "second");

        var roots = ProcessQueryDomain.BuildTree(Snapshot(first, second));

        Assert.NotEmpty(roots);
        Assert.Contains(roots.SelectMany(x => x.Diagnostics),
            value => value.Contains("cycle", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void BuildTree_DoesNotReuseNodesFromPreviousSnapshot()
    {
        var oldRoots = ProcessQueryDomain.BuildTree(Snapshot(Process(10, 0, "old")));
        var newRoots = ProcessQueryDomain.BuildTree(Snapshot(Process(20, 0, "new")));

        Assert.Single(oldRoots);
        Assert.Single(newRoots);
        Assert.Equal(20u, newRoots[0].Process.Pid);
        Assert.DoesNotContain(newRoots, node => node.Process.Pid == 10);
    }

    [Fact]
    public void AssessIdentity_ConfirmsEqualCreationTime()
    {
        var selected = Process(10, 0, "same", creation: 100);
        var detail = Process(10, 0, "same", creation: 100);

        Assert.Equal(IdentityAssessment.Confirmed,
            ProcessQueryDomain.AssessIdentity(selected, detail));
    }

    [Fact]
    public void BuildTree_DuplicatePidDoesNotThrowAndRemainsVisible()
    {
        var roots = ProcessQueryDomain.BuildTree(Snapshot(Process(10, 0, "first"), Process(10, 0, "second")));

        Assert.Equal(2, roots.Count);
        Assert.Contains(roots.SelectMany(x => x.Diagnostics), value => value.Contains("duplicate"));
    }

    [Fact]
    public void FieldValue_RejectsValueForNotRequested()
    {
        Assert.Throws<ArgumentException>(() => new FieldValue<string>(FieldState.NotRequested, "value", null));
    }

    [Fact]
    public void FieldValue_RequiresDiagnosticForError()
    {
        Assert.Throws<ArgumentException>(() => new FieldValue<string>(FieldState.Error, null, null));
    }

    [Fact]
    public void AssessIdentity_DistinguishesPossibleReuseWithoutCreationTime()
    {
        var selected = Process(10, 0, "old", creation: null, path: "old.exe");
        var detail = Process(10, 0, "new", creation: null, path: "new.exe");

        Assert.Equal(IdentityAssessment.PossibleReuse,
            ProcessQueryDomain.AssessIdentity(selected, detail));
    }

    private static ProcessSnapshot Snapshot(params ProcessRecord[] processes) =>
        new(processes, ResultState.Complete, [], 0);

    private static ProcessRecord Process(uint pid, uint ppid, string name,
        ObservationConsistency consistency = ObservationConsistency.SnapshotOnly, ulong? creation = 1, string? path = null) =>
        new(pid,
            new(FieldState.Available, ppid, null),
            new(FieldState.Available, name, null),
            new(FieldState.Available, path ?? name + ".exe", null),
            new(FieldState.NotRequested, null, null),
            creation.HasValue ? new(FieldState.Available, creation, null) : new(FieldState.Unavailable, null, null),
            consistency, []);
}
