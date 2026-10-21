using Xunit;

namespace ProcessDiscovery.Application.Tests;

public sealed class PresentationShellContractTests
{
    [Fact]
    public void WpfShell_DeclaresRequiredAccessibleAndVirtualizedControls()
    {
        var root = FindWorkspaceRoot();
        var xaml = File.ReadAllText(Path.Combine(root, "src", "ProcessDiscovery.App", "MainWindow.xaml"));
        var codeBehind = File.ReadAllText(Path.Combine(root, "src", "ProcessDiscovery.App", "MainWindow.xaml.cs"));

        Assert.Contains("AutomationProperties.LiveSetting=\"Polite\"", xaml);
        Assert.Contains("AutomationProperties.Name=\"Tabela de processos\"", xaml);
        Assert.Contains("AutomationProperties.Name=\"Árvore de processos\"", xaml);
        Assert.Contains("EnableRowVirtualization=\"True\"", xaml);
        Assert.Contains("EnableColumnVirtualization=\"True\"", xaml);
        Assert.Contains("VirtualizingPanel.IsVirtualizing=\"True\"", xaml);
        Assert.Contains("VirtualizingPanel.VirtualizationMode=\"Recycling\"", xaml);
        Assert.Contains("ProcessTable_OnSelectionChanged", xaml);
        Assert.Contains("ProcessTree_OnSelectedItemChanged", xaml);
        Assert.Contains("ApplyStateOnUiThread", codeBehind);
    }

    [Fact]
    public void WpfApp_OpensMainWindowOnStartup()
    {
        var root = FindWorkspaceRoot();
        var startup = File.ReadAllText(Path.Combine(root, "src", "ProcessDiscovery.App", "App.xaml.cs"));

        Assert.Contains("new MainWindow()", startup);
        Assert.Contains("MainWindow = window", startup);
        Assert.Contains("window.Show()", startup);
    }

    private static string FindWorkspaceRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null && !File.Exists(Path.Combine(directory.FullName, "ProcessDiscovery.slnx")))
            directory = directory.Parent;

        return directory?.FullName
            ?? throw new DirectoryNotFoundException("Raiz do workspace não encontrada.");
    }
}
