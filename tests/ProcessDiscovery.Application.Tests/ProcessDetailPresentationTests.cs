using ProcessDiscovery.Application.Backend;
using ProcessDiscovery.Application.Presentation;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class ProcessDetailPresentationTests
{
    [Fact]
    public void From_ConfirmsIdentityWhenPidAndCreationTimeMatch()
    {
        var selected = Process(42, 100, 1234);
        var detail = Process(42, 100, 1234);

        var presentation = ProcessDetailPresentation.From(selected, detail);

        Assert.Equal(42u, presentation.Pid);
        Assert.Equal(IdentityAssessment.Confirmed, presentation.Identity);
        Assert.Equal("Identidade confirmada", presentation.IdentityText);
        Assert.False(presentation.ModulesAvailable);
        Assert.False(presentation.SignatureAvailable);
    }

    [Fact]
    public void From_ReportsPidReuseWhenCreationTimeDiffers()
    {
        var selected = Process(42, 100, 1234);
        var detail = Process(42, 100, 5678);

        var presentation = ProcessDetailPresentation.From(selected, detail);

        Assert.Equal(IdentityAssessment.PidReused, presentation.Identity);
        Assert.Equal("PID reutilizado ou divergente", presentation.IdentityText);
    }

    private static ProcessRecord Process(uint pid, uint ppid, ulong creationTime) =>
        new(
            pid,
            new(FieldState.Available, ppid, null),
            new(FieldState.Available, "processo", null),
            new(FieldState.Available, "processo.exe", null),
            new(FieldState.Available, "pai", null),
            new(FieldState.Available, creationTime, null),
            ObservationConsistency.SnapshotOnly,
            []);
}
