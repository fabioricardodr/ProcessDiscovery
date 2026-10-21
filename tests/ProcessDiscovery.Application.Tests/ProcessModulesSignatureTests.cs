using ProcessDiscovery.Application.Backend;
using ProcessDiscovery.Application.Presentation;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class ProcessModulesSignatureTests
{
    [Fact]
    public void Detail_ExposesModulesAndSignatureFields()
    {
        var diagnostic = Diagnostic("signature-error");
        var modules = new ModuleCollection(
            [new ModuleRecord(
                new(FieldState.Available, "kernel.dll", null),
                new(FieldState.Available, "C:\\Windows\\kernel.dll", null),
                0x180000000)],
            ModuleCollectionState.Complete,
            []);
        var signature = new SignatureRecord(
            SignaturePresence.Present,
            SignatureTrust.Valid,
            0,
            null,
            "Default");
        var selected = Process(42, 1234, new(FieldState.Available, modules, null), new(FieldState.Available, signature, null));

        var detail = ProcessDetailPresentation.From(selected, selected);

        Assert.True(detail.ModulesAvailable);
        Assert.Single(detail.Modules);
        Assert.Equal("kernel.dll", detail.Modules[0].Name);
        Assert.Equal("0x180000000", detail.Modules[0].BaseAddress);
        Assert.True(detail.SignatureAvailable);
        Assert.NotNull(detail.Signature);
        Assert.Equal(SignatureTrust.Valid, detail.Signature.Trust);
        Assert.Equal("Disponível", detail.ModulesStateText);
    }

    [Fact]
    public void Detail_PreservesNotRequestedAndSignatureDiagnosticStates()
    {
        var diagnostic = Diagnostic("signature-error");
        var selected = Process(
            42,
            1234,
            new(FieldState.NotRequested, null, null),
            new(FieldState.Error, null, diagnostic));

        var detail = ProcessDetailPresentation.From(selected, selected);

        Assert.False(detail.ModulesAvailable);
        Assert.Equal("Não solicitado", detail.ModulesStateText);
        Assert.False(detail.SignatureAvailable);
        Assert.Equal("Erro", detail.SignatureStateText);
        Assert.Same(diagnostic, detail.SignatureDiagnostic);
    }

    [Fact]
    public void Detail_PreservesPartialModuleCollectionDiagnostics()
    {
        var diagnostic = Diagnostic("modules-partial");
        var selected = Process(
            42,
            1234,
            new(FieldState.Available, new ModuleCollection([], ModuleCollectionState.Partial, [diagnostic]), null),
            new(FieldState.NotRequested, null, null));

        var detail = ProcessDetailPresentation.From(selected, selected);

        Assert.Equal("Parcial", detail.ModulesStateText);
        Assert.Empty(detail.Modules);
        Assert.Single(detail.ModuleDiagnostics);
        Assert.Same(diagnostic, detail.ModuleDiagnostics[0]);
    }

    private static ProcessRecord Process(
        uint pid,
        ulong creationTime,
        FieldValue<ModuleCollection>? modules,
        FieldValue<SignatureRecord>? signature) =>
        new(
            pid,
            new(FieldState.Available, 1u, null),
            new(FieldState.Available, "processo", null),
            new(FieldState.Available, "processo.exe", null),
            new(FieldState.Available, "pai", null),
            new(FieldState.Available, creationTime, null),
            ObservationConsistency.SnapshotOnly,
            [],
            new(FieldState.Available, "interno", null),
            modules,
            signature);

    private static DiagnosticRecord Diagnostic(string id) => new(
        id, "other", "field", "signature", 5, "falha", "wintrust", "field", 42, "signature");
}
