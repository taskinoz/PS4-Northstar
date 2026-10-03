using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace NorthstarPS4.TokenHelper {

// Command-line options, shared by the window (NorthstarPS4TokenHelper.exe)
// and the terminal build (NorthstarPS4TokenHelperCli.exe). Names are matched
// without case, leading dashes or slashes, or inner dashes: --console-port,
// -ConsolePort and /consoleport are the same option.
public class Options {
    public string Target = "";   // --console "<address> [code]"
    public bool Local, Once, Help;
    public int Port = 37011, ConsolePort = 37012, LsxPort = 3216, MinSecondsBetweenTokens = 60;
    public string Output = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "shadPS4", "data", "northstar_ps4", "atlas_identity.json");
    public string KeyFile = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "NorthstarPS4", "token-helper.json");
    public string AdvertiseHost = "", MasterServer = "https://northstar.tf", LauncherVersion = "1.31.13";
    public string ContentId = "1039093", Title = "Titanfall2", ClientId = "TITANFALL2-PC-SERVER", Scope = "";

    public const string Usage =
        "NorthstarPS4 token helper: signs Northstar on a PS4, or in shadPS4, in through the EA app on this PC,\r\n" +
        "and keeps it signed in while it runs.\r\n" +
        "\r\n" +
        "  --console \"<address> <code>\"  Sign in the console at that address, with the code the game shows.\r\n" +
        "                               Just the address signs in a console paired before.\r\n" +
        "  --local                      Sign in Northstar in shadPS4 on this PC.\r\n" +
        "  --once                       Sign in, then exit without serving new tokens (terminal only).\r\n" +
        "  --output <file>              Where atlas_identity.json goes when shadPS4 on this PC is not running.\r\n" +
        "  --port <n>                   Port the game asks for new tokens on (37011).\r\n" +
        "  --console-port <n>           Port the game listens on for a sign-in (37012).\r\n" +
        "  --advertise-host <address>   The address the game should use to reach this PC.\r\n" +
        "  --help                       Show this.\r\n" +
        "\r\n" +
        "With no options the terminal build looks for a running game, then the console paired last time,\r\n" +
        "and otherwise asks. The window build takes the same options and starts signing in at once.";

    static string Normalize(string arg) {
        return arg.TrimStart('-', '/').Replace("-", "").Replace("_", "").ToLowerInvariant();
    }

    static readonly string[] ValueOptions = {
        "console", "output", "keyfile", "advertisehost", "masterserver", "launcherversion", "contentid", "title", "clientid",
        "scope", "port", "consoleport", "lsxport", "minsecondsbetweentokens"
    };

    static bool IsOption(string arg) {
        return arg.Length > 1 && (arg[0] == '-' || arg[0] == '/') && !char.IsDigit(arg[1]);
    }

    public static bool TryParse(string[] args, out Options options, out string error) {
        options = new Options();
        error = null;
        for (int i = 0; i < args.Length; i++) {
            if (!IsOption(args[i])) {
                error = "Unexpected \"" + args[i] + "\".";
                return false;
            }
            string name = Normalize(args[i]);
            switch (name) {
            case "local": options.Local = true; continue;
            case "once": options.Once = true; continue;
            case "help": case "h": case "?": options.Help = true; continue;
            case "gui": continue;  // accepted for the old script's sake; the window build always shows one
            }
            if (Array.IndexOf(ValueOptions, name) < 0) {
                error = "Unknown option " + args[i] + ".";
                return false;
            }
            if (i + 1 >= args.Length) {
                error = args[i] + " needs a value.";
                return false;
            }
            string value = args[++i];
            switch (name) {
            case "console":
                if (value.Trim().ToLowerInvariant() == "local") options.Local = true;
                else options.Target = value.Trim();
                break;
            case "output": options.Output = value; break;
            case "keyfile": options.KeyFile = value; break;
            case "advertisehost": options.AdvertiseHost = value; break;
            case "masterserver": options.MasterServer = value; break;
            case "launcherversion": options.LauncherVersion = value; break;
            case "contentid": options.ContentId = value; break;
            case "title": options.Title = value; break;
            case "clientid": options.ClientId = value; break;
            case "scope": options.Scope = value; break;
            case "port": if (!Number(value, 1, 65535, out options.Port, args[i - 1], out error)) return false; break;
            case "consoleport": if (!Number(value, 1, 65535, out options.ConsolePort, args[i - 1], out error)) return false; break;
            case "lsxport": if (!Number(value, 1, 65535, out options.LsxPort, args[i - 1], out error)) return false; break;
            case "minsecondsbetweentokens":
                if (!Number(value, 0, 86400, out options.MinSecondsBetweenTokens, args[i - 1], out error)) return false;
                break;
            default:
                error = "Unknown option " + args[i - 1] + ".";
                return false;
            }
        }
        if (options.Target != "" && !TokenService.IsAddress(SplitTarget(options.Target)[0])) {
            error = "\"" + options.Target + "\" is not an address and code, for example \"192.168.1.20 4821\".";
            return false;
        }
        return true;
    }

    static bool Number(string text, int min, int max, out int value, string option, out string error) {
        error = null;
        if (int.TryParse(text, out value) && value >= min && value <= max) return true;
        error = option + " needs a number from " + min + " to " + max + ".";
        return false;
    }

    // "<address> <code>" (or "<address>:<code>", "<address>,<code>") -> { address, code }.
    public static string[] SplitTarget(string text) {
        string[] parts = Regex.Split((text ?? "").Trim(), "[\\s,]+");
        if (parts.Length == 1) {
            Match m = Regex.Match(parts[0], "^([0-9.]+):(\\d{4})$");
            if (m.Success) return new string[] { m.Groups[1].Value, m.Groups[2].Value };
        }
        return new string[] { parts.Length > 0 ? parts[0] : "", parts.Length > 1 ? parts[1] : "" };
    }

    public TokenService CreateService(HelperState state) {
        var service = new TokenService();
        service.LsxPort = LsxPort;
        service.ContentId = ContentId;
        service.Title = Title;
        service.ClientId = ClientId;
        service.Scope = Scope;
        service.MasterServer = MasterServer;
        service.UserAgent = "R2Northstar/" + LauncherVersion + "+ps4 NorthstarPS4TokenHelper";
        service.Port = Port;
        service.ConsolePort = ConsolePort;
        service.MinSecondsBetweenTokens = MinSecondsBetweenTokens;
        service.Output = Output;
        service.AdvertiseHost = AdvertiseHost;
        service.Key = state.Key;
        return service;
    }
}

// The pairing key, and the console signed in last time, in
// %APPDATA%\NorthstarPS4\token-helper.json: {"key": "<32 hex>", "console": "<address>"}.
public class HelperState {
    public string Key, Console;
    readonly string path;

    HelperState(string path) { this.path = path; }

    public static HelperState Load(string path) {
        var state = new HelperState(path);
        try {
            if (File.Exists(path)) {
                string text = File.ReadAllText(path);
                Match key = Regex.Match(text, "\"key\"\\s*:\\s*\"([0-9a-f]{32})\"");
                Match console = Regex.Match(text, "\"console\"\\s*:\\s*\"([^\"]*)\"");
                if (key.Success) state.Key = key.Groups[1].Value;
                if (console.Success && TokenService.IsAddress(console.Groups[1].Value)) state.Console = console.Groups[1].Value;
            }
        } catch (IOException) {
        } catch (UnauthorizedAccessException) {
        }
        if (state.Key == null) {
            var bytes = new byte[16];
            using (var random = RandomNumberGenerator.Create()) random.GetBytes(bytes);
            var sb = new StringBuilder();
            foreach (var b in bytes) sb.Append(b.ToString("x2"));
            state.Key = sb.ToString();
            state.Save();
        }
        return state;
    }

    public void Save() {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
        File.WriteAllText(path, "{\r\n  \"key\": \"" + Key + "\",\r\n  \"console\": " +
            (Console == null ? "null" : "\"" + Console + "\"") + "\r\n}\r\n");
    }
}

}
