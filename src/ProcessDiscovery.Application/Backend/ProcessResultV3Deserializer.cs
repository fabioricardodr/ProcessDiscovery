using System.Text.Json;

namespace ProcessDiscovery.Application.Backend;

public static class ProcessResultV3Deserializer
{
    public static ProcessSnapshot Deserialize(string json)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(json);
        using var document = JsonDocument.Parse(json);
        return Deserialize(document.RootElement);
    }

    public static ProcessSnapshot Deserialize(JsonElement root)
    {
        if (root.ValueKind != JsonValueKind.Object)
            throw new JsonException("O resultado v3 deve ser um objeto JSON.");

        var schemaVersion = Required(root, "schema_version").GetInt32();
        if (schemaVersion != 3)
            throw new JsonException($"schema_version não suportado: {schemaVersion}.");

        var state = ParseResultState(RequiredString(root, "state"));
        var processes = Required(root, "processes")
            .EnumerateArray()
            .Select(ParseProcess)
            .ToList();
        var diagnostics = Required(root, "diagnostics")
            .EnumerateArray()
            .Select(ParseDiagnostic)
            .ToList();

        return new ProcessSnapshot(
            processes,
            state,
            diagnostics,
            Required(root, "unevaluable_processes").GetInt32());
    }

    private static ProcessRecord ParseProcess(JsonElement element)
    {
        return new ProcessRecord(
            Required(element, "pid").GetUInt32(),
            ParseField(element, "ppid", ParseNullableUInt32),
            ParseField(element, "name", ParseNullableString),
            ParseField(element, "image_path", ParseNullableString),
            ParseField(element, "parent_name", ParseNullableString),
            ParseField(element, "creation_time_100ns", ParseNullableUInt64),
            ParseConsistency(RequiredString(element, "observation_consistency")),
            ParseDiagnostics(element, "diagnostics"),
            ParseField(element, "internal_name", ParseNullableString),
            ParseField(element, "modules", ParseModuleCollection),
            ParseField(element, "signature", ParseSignature));
    }

    private static ModuleCollection ParseModuleCollection(JsonElement element)
    {
        var items = Required(element, "items")
            .EnumerateArray()
            .Select(ParseModule)
            .ToList();
        var state = RequiredString(element, "state") switch
        {
            "complete" => ModuleCollectionState.Complete,
            "partial" => ModuleCollectionState.Partial,
            var value => throw new JsonException($"Estado de coleção de módulos inválido: {value}.")
        };

        return new ModuleCollection(items, state, ParseDiagnostics(element, "diagnostics"));
    }

    private static ModuleRecord ParseModule(JsonElement element) => new(
        ParseField(element, "name", ParseNullableString),
        ParseField(element, "path", ParseNullableString),
        Required(element, "base_address").GetUInt64());

    private static SignatureRecord ParseSignature(JsonElement element) => new(
        RequiredString(element, "presence") switch
        {
            "present" => SignaturePresence.Present,
            "no_embedded_signature" => SignaturePresence.NoEmbeddedSignature,
            "unknown" => SignaturePresence.Unknown,
            var value => throw new JsonException($"Presença de assinatura inválida: {value}.")
        },
        RequiredString(element, "trust") switch
        {
            "valid" => SignatureTrust.Valid,
            "invalid" => SignatureTrust.Invalid,
            "indeterminate" => SignatureTrust.Indeterminate,
            "not_evaluated" => SignatureTrust.NotEvaluated,
            var value => throw new JsonException($"Confiança de assinatura inválida: {value}.")
        },
        Required(element, "wintrust_status").GetInt32(),
        NullableUInt32(Required(element, "last_error")),
        RequiredString(element, "policy"));

    private static DiagnosticRecord ParseDiagnostic(JsonElement element) => new(
        RequiredString(element, "diagnostic_id"),
        RequiredString(element, "diagnostic_kind"),
        RequiredString(element, "origin"),
        RequiredString(element, "operation"),
        Required(element, "native_code").GetUInt32(),
        RequiredString(element, "message"),
        RequiredString(element, "domain"),
        RequiredString(element, "scope"),
        NullableUInt32(Optional(element, "process_pid")),
        OptionalString(element, "field"),
        OptionalString(element, "collection"));

    private static IReadOnlyList<DiagnosticRecord> ParseDiagnostics(JsonElement element, string name) =>
        Required(element, name).EnumerateArray().Select(ParseDiagnostic).ToList();

    private static FieldValue<T> ParseField<T>(JsonElement parent, string name, Func<JsonElement, T?> parser)
    {
        var field = Required(parent, name);
        var state = ParseFieldState(RequiredString(field, "state"));
        var valueElement = Required(field, "value");
        var value = valueElement.ValueKind == JsonValueKind.Null ? default : parser(valueElement);
        var diagnostic = Optional(field, "diagnostic") is { ValueKind: not JsonValueKind.Null } diagnosticElement
            ? ParseDiagnostic(diagnosticElement)
            : null;
        return new FieldValue<T>(state, value, diagnostic);
    }

    private static FieldState ParseFieldState(string value) => value switch
    {
        "available" => FieldState.Available,
        "unavailable" => FieldState.Unavailable,
        "not_requested" => FieldState.NotRequested,
        "access_denied" => FieldState.AccessDenied,
        "process_exited" => FieldState.ProcessExited,
        "unsupported_architecture" => FieldState.UnsupportedArchitecture,
        "available_unverified" => FieldState.AvailableUnverified,
        "error" => FieldState.Error,
        _ => throw new JsonException($"Estado de campo inválido: {value}.")
    };

    private static ResultState ParseResultState(string value) => value switch
    {
        "complete" => ResultState.Complete,
        "partial" => ResultState.Partial,
        "failed" => ResultState.Failed,
        _ => throw new JsonException($"Estado de resultado inválido: {value}.")
    };

    private static ObservationConsistency ParseConsistency(string value) => value switch
    {
        "snapshot_only" => ObservationConsistency.SnapshotOnly,
        "handle_only" => ObservationConsistency.HandleOnly,
        "best_effort_unverified" => ObservationConsistency.BestEffortUnverified,
        "mismatch_detected" => ObservationConsistency.MismatchDetected,
        _ => throw new JsonException($"Consistência inválida: {value}.")
    };

    private static uint? ParseNullableUInt32(JsonElement value) => value.ValueKind == JsonValueKind.Null ? null : value.GetUInt32();
    private static ulong? ParseNullableUInt64(JsonElement value) => value.ValueKind == JsonValueKind.Null ? null : value.GetUInt64();
    private static string? ParseNullableString(JsonElement value) => value.ValueKind == JsonValueKind.Null ? null : value.GetString();
    private static uint? NullableUInt32(JsonElement value) =>
        value.ValueKind is JsonValueKind.Null or JsonValueKind.Undefined ? null : value.GetUInt32();

    private static JsonElement Required(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value)
            ? value
            : throw new JsonException($"Propriedade obrigatória ausente: {name}.");

    private static JsonElement Optional(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value) ? value : default;

    private static string RequiredString(JsonElement element, string name) =>
        Required(element, name).GetString()
            ?? throw new JsonException($"Propriedade string nula: {name}.");

    private static string? OptionalString(JsonElement element, string name)
    {
        var value = Optional(element, name);
        return value.ValueKind is JsonValueKind.Undefined or JsonValueKind.Null ? null : value.GetString();
    }
}
