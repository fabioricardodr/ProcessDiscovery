using System.Text.Json;
using ProcessDiscovery.Application.Backend;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class ProcessResultV3DeserializerTests
{
    [Fact]
    public void Deserialize_MapsModulesSignatureAndDiagnostics()
    {
        var snapshot = ProcessResultV3Deserializer.Deserialize("""
        {
          "schema_version": 3,
          "processes": [{
            "pid": 42,
            "ppid": {"state":"available","value":1,"diagnostic":null},
            "name": {"state":"available","value":"app","diagnostic":null},
            "image_path": {"state":"available","value":"C:/app.exe","diagnostic":null},
            "parent_name": {"state":"available","value":"parent","diagnostic":null},
            "creation_time_100ns": {"state":"available","value":100,"diagnostic":null},
            "internal_name": {"state":"available","value":"app.internal","diagnostic":null},
            "modules": {"state":"available","value":{"items":[{"name":{"state":"available","value":"m.dll","diagnostic":null},"path":{"state":"available","value":"C:/m.dll","diagnostic":null},"base_address":4096}],"state":"partial","diagnostics":[]},"diagnostic":null},
            "signature": {"state":"available","value":{"presence":"present","trust":"valid","wintrust_status":0,"last_error":null,"policy":"default"},"diagnostic":null},
            "observation_consistency":"snapshot_only",
            "diagnostics":[]
          }],
          "state":"complete",
          "fatal_error":null,
          "diagnostics":[],
          "partial_reasons":[],
          "unevaluable_processes":0
        }
        """);

        var process = Assert.Single(snapshot.Processes);
        Assert.Equal("app.internal", process.InternalName!.Value);
        Assert.Equal(FieldState.Available, process.Modules!.State);
        Assert.Equal(ModuleCollectionState.Partial, process.Modules.Value!.State);
        Assert.Equal("m.dll", process.Modules.Value.Items[0].Name.Value);
        Assert.Equal(SignatureTrust.Valid, process.Signature!.Value!.Trust);
    }

    [Fact]
    public void Deserialize_PreservesUnavailableAndErrorDiagnostics()
    {
        var snapshot = ProcessResultV3Deserializer.Deserialize("""
        {
          "schema_version": 3,
          "processes": [{
            "pid": 7,
            "ppid": {"state":"not_requested","value":null,"diagnostic":null},
            "name": {"state":"available","value":"x","diagnostic":null},
            "image_path": {"state":"unavailable","value":null,"diagnostic":null},
            "parent_name": {"state":"not_requested","value":null,"diagnostic":null},
            "creation_time_100ns": {"state":"not_requested","value":null,"diagnostic":null},
            "internal_name": {"state":"not_requested","value":null,"diagnostic":null},
            "modules": {"state":"access_denied","value":null,"diagnostic":null},
            "signature": {"state":"error","value":null,"diagnostic":{"diagnostic_id":"sig","diagnostic_kind":"target_inspection","origin":"field","operation":"signature","native_code":5,"message":"denied","domain":"wintrust","scope":"field","process_pid":7,"field":"signature"}},
            "observation_consistency":"best_effort_unverified",
            "diagnostics":[]
          }],
          "state":"partial",
          "fatal_error":null,
          "diagnostics":[],
          "partial_reasons":["target_inspection"],
          "unevaluable_processes":1
        }
        """);

        var process = Assert.Single(snapshot.Processes);
        Assert.Equal(FieldState.AccessDenied, process.Modules!.State);
        Assert.Null(process.Modules.Value);
        Assert.Equal(FieldState.Error, process.Signature!.State);
        Assert.Equal("sig", process.Signature.Diagnostic!.DiagnosticId);
    }

    [Fact]
    public async Task JsonBackend_MapsFailedResultToFatalResponse()
    {
        var backend = new JsonProcessQueryBackend((_, _) => Task.FromResult("""
        {"schema_version":3,"processes":[],"state":"failed","fatal_error":{"diagnostic_id":"fatal","diagnostic_kind":"fatal","origin":"fatal_error","operation":"query","native_code":1,"message":"failed","domain":"bridge","scope":"global"},"diagnostics":[],"partial_reasons":[],"unevaluable_processes":0}
        """));

        var response = await backend.QueryAsync(new(QueryMode.Enumerate), CancellationToken.None);

        Assert.True(response.IsFatal);
        Assert.Equal(ResultState.Failed, response.Snapshot.State);
    }

    [Fact]
    public void Deserialize_RejectsUnsupportedSchemaVersion()
    {
        Assert.Throws<JsonException>(() => ProcessResultV3Deserializer.Deserialize(
            "{\"schema_version\":2}"));
    }
}
