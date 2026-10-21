using System.Text.Json;
using Json.Schema;

if (args.Length < 3)
{
    Console.Error.WriteLine("usage: ContractValidator <schema.json> <document.json> <document-kind>");
    return 2;
}

var schemaText = await File.ReadAllTextAsync(args[0]);
var documentText = await File.ReadAllTextAsync(args[1]);
var schema = JsonSchema.FromText(schemaText);
using var document = JsonDocument.Parse(documentText);
var result = schema.Evaluate(document.RootElement, new EvaluationOptions { OutputFormat = OutputFormat.List });
if (!result.IsValid)
{
    foreach (var detail in result.Details ?? [])
        Console.Error.WriteLine($"schema: {detail.InstanceLocation} {JsonSerializer.Serialize(detail.Errors)}");
    return 1;
}

if (args[2].Equals("result", StringComparison.OrdinalIgnoreCase))
{
    var root = document.RootElement;
    var state = root.GetProperty("state").GetString();
    var reasons = root.GetProperty("partial_reasons");
    var fatal = root.GetProperty("fatal_error");
    if (state == "complete" && reasons.GetArrayLength() != 0) return SemanticFailure("complete result has partial reasons");
    if (state == "failed" && fatal.ValueKind != JsonValueKind.Object) return SemanticFailure("failed result has no fatal_error");
    if (state == "partial" && (fatal.ValueKind != JsonValueKind.Null || reasons.GetArrayLength() == 0)) return SemanticFailure("partial result invariants failed");

    var ids = new HashSet<string>(StringComparer.Ordinal);
    foreach (var diagnostic in root.GetProperty("diagnostics").EnumerateArray())
    {
        if (!ids.Add(diagnostic.GetProperty("diagnostic_id").GetString()!))
            return SemanticFailure("diagnostic_id must be unique within a response");
        var scope = diagnostic.GetProperty("scope").GetString();
        var hasPid = diagnostic.TryGetProperty("process_pid", out var pid);
        if (scope == "process" && (!hasPid || pid.ValueKind != JsonValueKind.Number))
            return SemanticFailure("process diagnostics require process_pid");
        if (scope != "process" && hasPid)
            return SemanticFailure("only process diagnostics may contain process_pid");
    }
}
else if (args[2].Equals("manifest", StringComparison.OrdinalIgnoreCase))
{
    var architectures = new HashSet<string>(StringComparer.Ordinal);
    foreach (var artifact in document.RootElement.GetProperty("artifacts").EnumerateArray())
    {
        if (!architectures.Add(artifact.GetProperty("architecture").GetString()!))
            return SemanticFailure("manifest contains duplicate architecture");
        if (artifact.GetProperty("relative_path").GetString()!.Contains("..", StringComparison.Ordinal))
            return SemanticFailure("manifest contains unsafe relative path");
    }
}
else
{
    Console.Error.WriteLine("document-kind must be manifest or result");
    return 2;
}

Console.WriteLine("valid");
return 0;

static int SemanticFailure(string message)
{
    Console.Error.WriteLine($"semantic: {message}");
    return 1;
}
