using System.Diagnostics;
using ProcessDiscovery.Application.Backend;
using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class NativeBridgeIntegrationTests
{
    [Fact]
    public async Task QueryAsync_UsesNativeBridgeAndMaterializesJsonV3()
    {
        var bridgePath = LocateBridge();
        using var backend = new NativeBridgeProcessQueryBackend(bridgePath);
        var service = new ProcessQueryService(backend);
        var request = new QueryRequest(
            QueryMode.ByPid,
            (uint)Environment.ProcessId,
            IncludeImagePath: true,
            IncludeInternalName: true,
            IncludeSignature: true,
            IncludeModules: true);

        var response = await service.QueryAsync(request, CancellationToken.None);

        Assert.False(response.IsFatal);
        var process = Assert.Single(response.Snapshot.Processes);
        Assert.Equal((uint)Environment.ProcessId, process.Pid);
        Assert.NotNull(process.Modules);
        Assert.NotNull(process.Signature);
    }

    private static string LocateBridge()
    {
        var architecture = Environment.Is64BitProcess ? "x64" : "x86";
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null && !File.Exists(Path.Combine(directory.FullName, "ProcessDiscovery.slnx")))
            directory = directory.Parent;

        var root = directory?.FullName
            ?? throw new DirectoryNotFoundException("Raiz do workspace não encontrada.");
        var packageRoot = Path.Combine(root, "artifacts", architecture);
        var path = Path.Combine(packageRoot, "ProcessDiscovery.Bridge.dll");
        if (!File.Exists(path))
            throw new FileNotFoundException($"Bridge {architecture} não encontrada: {path}", path);
        return path;
    }
}
