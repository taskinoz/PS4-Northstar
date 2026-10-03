using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace NorthstarPS4.TokenHelper {

// NorthstarPS4TokenHelper.exe: the helper's window. Network calls run as
// tasks (TokenService.*Async) awaited on the window's thread, one step at a
// time, so the window never stops responding; a timer moves the service's
// activity lines into the list.
public class MainWindow : Form {
    public const string WindowTitle = "NorthstarPS4 Token Helper";

    static readonly Color OkColor = Color.FromArgb(0, 120, 60);
    static readonly Color ErrorColor = Color.FromArgb(190, 30, 30);

    readonly Options options;
    readonly HelperState state;
    readonly TokenService service;
    string[] identity;
    bool busy;

    readonly Label eaStatus, hint, addressLabel, codeLabel, result;
    readonly Button eaRetry, signIn;
    readonly RadioButton localRadio, remoteRadio;
    readonly TextBox address, code;
    readonly ListBox activity;
    readonly Timer timer;

    public MainWindow(Options options) {
        this.options = options;
        state = HelperState.Load(options.KeyFile);
        service = options.CreateService(state);

        Text = WindowTitle;
        Font = new Font("Segoe UI", 9.75f);
        AutoScaleMode = AutoScaleMode.Dpi;
        AutoScaleDimensions = new SizeF(96, 96);
        ClientSize = new Size(560, 620);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (ArgumentException) { }

        Add(this, new Label { Text = "Sign Northstar in through the EA app", AutoSize = true, Location = new Point(18, 14),
            Font = new Font("Segoe UI Semibold", 14f) });
        Add(this, new Label { Location = new Point(20, 50), Size = new Size(522, 60),
            Text = "Lets Northstar on your PS4, or in shadPS4, use the EA account signed in on this PC. Leave this window " +
                   "open while you play: Northstar asks it for a new sign-in when the old one expires." });

        // 1. EA account
        var eaBox = Add(this, new GroupBox { Text = "1.  EA account", Location = new Point(18, 116), Size = new Size(524, 70) });
        eaStatus = Add(eaBox, new Label { Location = new Point(14, 26), Size = new Size(390, 40), Text = "Getting a token from the EA app..." });
        eaRetry = Add(eaBox, new Button { Text = "Try again", Location = new Point(412, 24), Size = new Size(96, 32), Visible = false });

        // 2. Where the game is
        var gameBox = Add(this, new GroupBox { Text = "2.  Where is Northstar running?", Location = new Point(18, 196), Size = new Size(524, 238) });
        localRadio = Add(gameBox, new RadioButton { Text = "In shadPS4 on this PC", Location = new Point(16, 28), AutoSize = true });
        remoteRadio = Add(gameBox, new RadioButton { Text = "On a PS4, or in shadPS4 on another computer", Location = new Point(16, 56), AutoSize = true });
        hint = Add(gameBox, new Label { Location = new Point(34, 84), Size = new Size(474, 38),
            Text = "In the game, select Launch Northstar. It shows an address and a 4-digit code: type them here." });
        addressLabel = Add(gameBox, new Label { Text = "Address", Location = new Point(34, 131), AutoSize = true });
        address = Add(gameBox, new TextBox { Location = new Point(96, 127), Size = new Size(170, 26) });
        codeLabel = Add(gameBox, new Label { Text = "Code", Location = new Point(286, 131), AutoSize = true });
        code = Add(gameBox, new TextBox { Location = new Point(330, 127), Size = new Size(64, 26), MaxLength = 4 });
        signIn = Add(gameBox, new Button { Text = "Sign in", Location = new Point(16, 172), Size = new Size(110, 34), Enabled = false });
        result = Add(gameBox, new Label { Location = new Point(138, 168), Size = new Size(372, 62) });
        AcceptButton = signIn;

        // 3. Activity
        var activityBox = Add(this, new GroupBox { Text = "3.  Activity", Location = new Point(18, 444), Size = new Size(524, 162) });
        activity = Add(activityBox, new ListBox { Location = new Point(14, 26), Size = new Size(496, 124), IntegralHeight = false,
            HorizontalScrollbar = true });

        string[] target = Options.SplitTarget(options.Target);
        if (options.Local) {
            localRadio.Checked = true;
        } else if (options.Target != "") {
            remoteRadio.Checked = true;
            address.Text = target[0];
            code.Text = target[1];
        } else if (state.Console != null) {
            remoteRadio.Checked = true;
            address.Text = state.Console;
        } else {
            localRadio.Checked = true;
        }

        localRadio.CheckedChanged += delegate { UpdateControls(); };
        remoteRadio.CheckedChanged += delegate { UpdateControls(); };
        code.KeyPress += delegate(object sender, KeyPressEventArgs e) {
            if (!char.IsDigit(e.KeyChar) && !char.IsControl(e.KeyChar)) e.Handled = true;
        };
        signIn.Click += async delegate { await SignInAsync(); };
        eaRetry.Click += async delegate { await StartAsync(); };

        timer = new Timer { Interval = 150 };
        timer.Tick += delegate {
            string line;
            while (service.Events.TryDequeue(out line)) AddActivity(line);
        };
        Shown += async delegate { timer.Start(); await StartAsync(); };
        FormClosed += delegate { timer.Stop(); service.Stop(); };
        UpdateControls();
    }

    static T Add<T>(Control parent, T control) where T : Control {
        parent.Controls.Add(control);
        return control;
    }

    void SetResult(string text, Color? color = null) {
        result.Text = text;
        result.ForeColor = color ?? SystemColors.ControlText;
    }

    void AddActivity(string text) {
        activity.Items.Add(text);
        while (activity.Items.Count > 300) activity.Items.RemoveAt(0);
        activity.TopIndex = Math.Max(0, activity.Items.Count - 1);
    }

    void UpdateControls() {
        bool remote = remoteRadio.Checked;
        foreach (Control control in new Control[] { hint, addressLabel, address, codeLabel, code }) control.Enabled = remote;
        signIn.Enabled = !busy && identity != null;
        eaRetry.Enabled = !busy;
        UseWaitCursor = busy;
    }

    void SetBusy(bool value) {
        busy = value;
        UpdateControls();
    }

    static string Reason(Exception e) {
        while (e.InnerException != null) e = e.InnerException;
        return e.Message;
    }

    static string Sentence(string text) {
        if (string.IsNullOrEmpty(text)) return text;
        text = char.ToUpperInvariant(text[0]) + text.Substring(1);
        return text.EndsWith(".") ? text : text + ".";
    }

    // Gets a token, then signs in what was asked for on the command line, a
    // game running on this PC, or the console paired last time.
    async Task StartAsync() {
        SetBusy(true);
        eaStatus.Text = "Getting a token from the EA app...";
        eaStatus.ForeColor = SystemColors.ControlText;
        eaRetry.Visible = false;
        try {
            identity = await service.MintAsync();
        } catch (Exception e) {
            identity = null;
            eaStatus.Text = Reason(e);
            eaStatus.ForeColor = ErrorColor;
            eaRetry.Visible = true;
            SetResult("");
            SetBusy(false);
            return;
        }
        eaStatus.Text = "Signed in to EA as account " + identity[0] + ".";
        eaStatus.ForeColor = OkColor;
        SetBusy(false);

        if (options.Local || options.Target != "") {
            await SignInAsync();
            return;
        }
        SetBusy(true);
        SetResult("Looking for Northstar...");
        bool here = await service.HelloAsync("127.0.0.1");
        bool paired = !here && state.Console != null && await service.HelloAsync(state.Console);
        SetBusy(false);
        if (here) {
            localRadio.Checked = true;
            await SignInAsync();
        } else if (paired) {
            remoteRadio.Checked = true;
            address.Text = state.Console;
            await SignInAsync();
        } else if (state.Console != null) {
            SetResult("Northstar is not running on " + state.Console + ". Start it, then select Sign in.");
        } else {
            SetResult("Choose where Northstar is running, then select Sign in.");
        }
    }

    async Task SignInAsync() {
        if (busy || identity == null) return;
        bool remote = remoteRadio.Checked;
        string target = remote ? address.Text.Trim() : "127.0.0.1";
        string pin = remote ? code.Text.Trim() : "";
        if (remote && !TokenService.IsAddress(target)) {
            SetResult("Type the address the game shows, for example 192.168.1.20.", ErrorColor);
            return;
        }
        if (remote && pin.Length != 4 && !(pin == "" && target == state.Console)) {
            SetResult("Type the 4-digit code the game shows.", ErrorColor);
            return;
        }
        SetBusy(true);
        SetResult("Signing in...");
        try {
            // A token from the last minute is reused; an older one is replaced.
            identity = await service.MintAsync();
            string failure = null;
            bool saved = false;
            if (!remote) {
                if (await service.HelloAsync(target)) {
                    failure = await service.SignInAsync(target, "", identity);
                } else {
                    service.WriteIdentity(identity, service.LocalRefreshUrl);
                    saved = true;
                }
            } else {
                failure = await service.SignInAsync(target, pin, identity);
            }
            if (failure != null) {
                SetResult(Sentence(failure), ErrorColor);
                return;
            }

            service.WriteIdentityOnRefresh = !remote;
            string serving = service.StartServing(remote);
            string message;
            if (remote) {
                state.Console = target;
                state.Save();
                code.Text = "";
                message = "Signed in Northstar on " + target + ". Select Launch Northstar in the game.";
                AddActivity(DateTime.Now.ToString("T") + "  signed in Northstar on " + target);
            } else if (saved) {
                message = "Saved. Northstar in shadPS4 on this PC picks up the sign-in when it starts.";
                AddActivity(DateTime.Now.ToString("T") + "  saved the sign-in for shadPS4 on this PC");
            } else {
                message = "Signed in Northstar in shadPS4 on this PC. Select Launch Northstar in the game.";
                AddActivity(DateTime.Now.ToString("T") + "  signed in Northstar in shadPS4 on this PC");
            }
            if (serving != null) SetResult(message + " But " + serving, ErrorColor);
            else SetResult(message, OkColor);
        } catch (Exception e) {
            SetResult(Sentence(Reason(e)), ErrorColor);
        } finally {
            SetBusy(false);
        }
    }
}

}
