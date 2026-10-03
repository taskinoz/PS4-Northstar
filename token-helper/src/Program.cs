using System;
using System.Threading;
using System.Windows.Forms;

namespace NorthstarPS4.TokenHelper {

// One source, two programs (token-helper/build.ps1): the window,
// NorthstarPS4TokenHelper.exe, and with CLI defined the terminal build,
// NorthstarPS4TokenHelperCli.exe. Both take the options in Options.cs.
static class Program {
    [STAThread]
    static int Main(string[] args) {
        Options options;
        string error;
        bool parsed = Options.TryParse(args, out options, out error);
#if CLI
        if (!parsed) {
            Console.Error.WriteLine(error);
            Console.Error.WriteLine();
            Console.Error.WriteLine(Options.Usage);
            return 2;
        }
        if (options.Help) {
            Console.WriteLine(Options.Usage);
            return 0;
        }
        return Cli.Run(options);
#else
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        if (!parsed || options.Help) {
            MessageBox.Show((parsed ? "" : error + "\r\n\r\n") + Options.Usage, MainWindow.WindowTitle, MessageBoxButtons.OK,
                parsed ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
            return parsed ? 0 : 2;
        }
        bool first;
        using (new Mutex(true, "NorthstarPS4TokenHelperWindow", out first)) {
            if (!first) {
                MessageBox.Show("The token helper is already open. Use that window, or close it first.", MainWindow.WindowTitle,
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return 1;
            }
            Application.Run(new MainWindow(options));
        }
        return 0;
#endif
    }
}

}
