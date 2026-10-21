using System.IO;

namespace ProcessDiscovery.App;

internal static class NativeBridgePathResolver
{
    public static string Resolve()
    {
        var architecture = Environment.Is64BitProcess ? "x64" : "x86";
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        var roots = EnumerateRoots(directory).ToList();
        var candidates = roots
            .SelectMany(root => new[]
            {
                root,
                Path.Combine(root, architecture, "Release"),
                Path.Combine(root, "artifacts", architecture)
            })
            .Distinct(StringComparer.OrdinalIgnoreCase);

        foreach (var candidate in candidates)
        {
            var bridge = Path.Combine(candidate, "ProcessDiscovery.Bridge.dll");
            if (File.Exists(bridge)
                && File.Exists(Path.Combine(candidate, "ProcessDiscovery.dll"))
                && File.Exists(Path.Combine(candidate, "artifacts-manifest.json")))
                return bridge;
        }

        throw new FileNotFoundException(
            $"Pacote nativo {architecture} não encontrado. " +
            "São necessários ProcessDiscovery.Bridge.dll, ProcessDiscovery.dll e artifacts-manifest.json.");
    }

    private static IEnumerable<string> EnumerateRoots(DirectoryInfo start)
    {
        for (var current = start; current is not null; current = current.Parent)
            yield return current.FullName;
    }
}
