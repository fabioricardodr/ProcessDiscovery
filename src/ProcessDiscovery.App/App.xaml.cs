using System.Windows;
using System.IO;
using System.Text;

namespace ProcessDiscovery.App;

public partial class App : System.Windows.Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        try
        {
            base.OnStartup(e);

            var window = new MainWindow();
            MainWindow = window;
            window.Show();
        }
        catch (Exception exception)
        {
            HandleStartupFailure(exception);
        }
    }

    private static void HandleStartupFailure(Exception exception)
    {
        var details = new StringBuilder()
            .AppendLine("O Process Discovery não pôde ser iniciado.")
            .AppendLine()
            .AppendLine(exception.ToString())
            .ToString();

        try
        {
            var logDirectory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "ProcessDiscovery");
            Directory.CreateDirectory(logDirectory);
            File.WriteAllText(Path.Combine(logDirectory, "startup-error.log"), details);
        }
        catch
        {
            // A falha de diagnóstico não deve ocultar a falha original.
        }

        MessageBox.Show(
            details,
            "Process Discovery - falha na inicialização",
            MessageBoxButton.OK,
            MessageBoxImage.Error);
        Current.Shutdown(1);
    }
}
