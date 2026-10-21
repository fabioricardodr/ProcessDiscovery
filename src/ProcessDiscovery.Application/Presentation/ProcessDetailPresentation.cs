using ProcessDiscovery.Application.Backend;

namespace ProcessDiscovery.Application.Presentation;

public sealed record ProcessDetailPresentation(
    uint Pid,
    string Name,
    string ParentPidText,
    string ParentName,
    string ImagePath,
    string CreationTimeText,
    string ConsistencyText,
    IdentityAssessment Identity,
    string IdentityText,
    string InternalName,
    IReadOnlyList<ModulePresentation> Modules,
    string ModulesStateText,
    IReadOnlyList<DiagnosticRecord> ModuleDiagnostics,
    SignaturePresentation? Signature,
    string SignatureStateText,
    DiagnosticRecord? SignatureDiagnostic,
    IReadOnlyList<DiagnosticRecord> Diagnostics,
    bool ModulesAvailable,
    bool SignatureAvailable)
{
    public static ProcessDetailPresentation From(ProcessRecord selected, ProcessRecord detail)
    {
        ArgumentNullException.ThrowIfNull(selected);
        ArgumentNullException.ThrowIfNull(detail);

        var identity = ProcessQueryDomain.AssessIdentity(selected, detail);
        var modules = detail.Modules;
        var signature = detail.Signature;

        return new ProcessDetailPresentation(
            detail.Pid,
            Text(detail.Name, "(nome indisponível)"),
            Text(detail.ParentPid, "(PPID indisponível)"),
            Text(detail.ParentName, "(pai indisponível)"),
            Text(detail.ImagePath, "(caminho indisponível)"),
            Text(detail.CreationTime100Ns, "(identidade indisponível)"),
            FormatConsistency(detail.ObservationConsistency),
            identity,
            FormatIdentity(identity),
            detail.InternalName is null
                ? "(nome interno indisponível)"
                : Text(detail.InternalName, "(nome interno indisponível)"),
            modules?.Value?.Items.Select(ModulePresentation.From).ToList() ?? [],
            modules is null ? "Módulos não solicitados" : ModuleStateText(modules),
            modules?.Value?.Diagnostics ?? [],
            signature?.Value is SignatureRecord signatureValue
                ? SignaturePresentation.From(signatureValue)
                : null,
            signature is null ? "Assinatura não solicitada" : FieldStateText(signature),
            signature?.Diagnostic,
            detail.Diagnostics,
            modules?.State is FieldState.Available or FieldState.AvailableUnverified,
            signature?.State is FieldState.Available or FieldState.AvailableUnverified);
    }

    private static string Text<T>(FieldValue<T> field, string unavailable) =>
        field.State is FieldState.Available or FieldState.AvailableUnverified
            ? field.Value?.ToString() ?? unavailable
            : unavailable;

    private static string FormatConsistency(ObservationConsistency consistency) => consistency switch
    {
        ObservationConsistency.SnapshotOnly => "Somente snapshot",
        ObservationConsistency.HandleOnly => "Somente handle",
        ObservationConsistency.BestEffortUnverified => "Melhor esforço, não verificado",
        ObservationConsistency.MismatchDetected => "Divergência detectada",
        _ => "Consistência desconhecida"
    };

    private static string FormatIdentity(IdentityAssessment identity) => identity switch
    {
        IdentityAssessment.Confirmed => "Identidade confirmada",
        IdentityAssessment.PidReused => "PID reutilizado ou divergente",
        IdentityAssessment.PossibleReuse => "Possível reutilização de PID",
        _ => "Identidade não confirmada"
    };

    private static string FieldStateText<T>(FieldValue<T> field) => field.State switch
    {
        FieldState.Available => "Disponível",
        FieldState.AvailableUnverified => "Disponível, não verificado",
        FieldState.NotRequested => "Não solicitado",
        FieldState.AccessDenied => "Acesso negado",
        FieldState.ProcessExited => "Processo encerrado",
        FieldState.UnsupportedArchitecture => "Arquitetura não suportada",
        FieldState.Error => "Erro",
        _ => "Indisponível"
    };

    private static string ModuleStateText(FieldValue<ModuleCollection> field) => field.State switch
    {
        FieldState.Available when field.Value?.State == ModuleCollectionState.Partial => "Parcial",
        FieldState.AvailableUnverified when field.Value?.State == ModuleCollectionState.Partial => "Parcial, não verificado",
        _ => FieldStateText(field)
    };
}

public sealed record ModulePresentation(string Name, string Path, string BaseAddress)
{
    public static ModulePresentation From(ModuleRecord module) => new(
        Text(module.Name, "(nome indisponível)"),
        Text(module.Path, "(caminho indisponível)"),
        $"0x{module.BaseAddress:X}");

    private static string Text<T>(FieldValue<T> field, string unavailable) =>
        field.State is FieldState.Available or FieldState.AvailableUnverified
            ? field.Value?.ToString() ?? unavailable
            : unavailable;
}

public sealed record SignaturePresentation(
    SignaturePresence Presence,
    SignatureTrust Trust,
    int WinTrustStatus,
    uint? LastError,
    string Policy)
{
    public static SignaturePresentation From(SignatureRecord signature) => new(
        signature.Presence,
        signature.Trust,
        signature.WinTrustStatus,
        signature.LastError,
        signature.Policy);
}
