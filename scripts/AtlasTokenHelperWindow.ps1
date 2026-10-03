# The token helper's window, for Start-AtlasTokenHelper.ps1 -Gui (which
# NorthstarPS4-TokenHelper.cmd starts when double-clicked). Dot-sourced by that
# script: $helper (NsTokenHelper), $state, Save-HelperState and Start-Serving
# come from it. Every network call runs as a task on NsTokenHelper's side; a
# timer on the window's thread picks up results and activity, so the window
# never stops responding.

function Show-TokenHelperWindow {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    [Windows.Forms.Application]::EnableVisualStyles()

    $okColor = [Drawing.Color]::FromArgb(0, 120, 60)
    $errorColor = [Drawing.Color]::FromArgb(190, 30, 30)
    $plainColor = [Drawing.SystemColors]::ControlText
    $font = New-Object Drawing.Font('Segoe UI', 9.75)

    $form = New-Object Windows.Forms.Form
    $form.Text = 'NorthstarPS4 Token Helper'
    $form.Font = $font
    $form.AutoScaleMode = 'Dpi'
    $form.AutoScaleDimensions = New-Object Drawing.SizeF(96, 96)
    $form.ClientSize = New-Object Drawing.Size(560, 620)
    $form.FormBorderStyle = 'FixedSingle'
    $form.MaximizeBox = $false
    $form.StartPosition = 'CenterScreen'

    function New-Control([string] $type, [hashtable] $properties, $parent) {
        $control = New-Object "Windows.Forms.$type"
        foreach ($name in $properties.Keys) { $control.$name = $properties[$name] }
        $parent.Controls.Add($control)
        return $control
    }
    function Point([int] $x, [int] $y) { New-Object Drawing.Point($x, $y) }
    function Size([int] $w, [int] $h) { New-Object Drawing.Size($w, $h) }

    $heading = New-Control Label @{ Text = 'Sign Northstar in through the EA app'; AutoSize = $true; Location = (Point 18 14)
        Font = (New-Object Drawing.Font('Segoe UI Semibold', 14)) } $form
    New-Control Label @{ Location = (Point 20 50); Size = (Size 522 60)
        Text = 'Lets Northstar on your PS4, or in shadPS4, use the EA account signed in on this PC. Leave this window open while you play: Northstar asks it for a new sign-in when the old one expires.' } $form | Out-Null

    # 1. EA account
    $eaBox = New-Control GroupBox @{ Text = '1.  EA account'; Location = (Point 18 116); Size = (Size 524 70) } $form
    $eaStatus = New-Control Label @{ Location = (Point 14 26); Size = (Size 390 40); Text = 'Getting a token from the EA app...' } $eaBox
    $eaRetry = New-Control Button @{ Text = 'Try again'; Location = (Point 412 24); Size = (Size 96 32); Visible = $false } $eaBox

    # 2. Where the game is
    $gameBox = New-Control GroupBox @{ Text = '2.  Where is Northstar running?'; Location = (Point 18 196); Size = (Size 524 238) } $form
    $localRadio = New-Control RadioButton @{ Text = 'In shadPS4 on this PC'; Location = (Point 16 28); AutoSize = $true } $gameBox
    $remoteRadio = New-Control RadioButton @{ Text = 'On a PS4, or in shadPS4 on another computer'; Location = (Point 16 56); AutoSize = $true } $gameBox
    $hint = New-Control Label @{ Location = (Point 34 84); Size = (Size 474 38)
        Text = 'In the game, select Launch Northstar. It shows an address and a 4-digit code: type them here.' } $gameBox
    $addressLabel = New-Control Label @{ Text = 'Address'; Location = (Point 34 131); AutoSize = $true } $gameBox
    $address = New-Control TextBox @{ Location = (Point 96 127); Size = (Size 170 26) } $gameBox
    $codeLabel = New-Control Label @{ Text = 'Code'; Location = (Point 286 131); AutoSize = $true } $gameBox
    $code = New-Control TextBox @{ Location = (Point 330 127); Size = (Size 64 26); MaxLength = 4 } $gameBox
    $signIn = New-Control Button @{ Text = 'Sign in'; Location = (Point 16 172); Size = (Size 110 34); Enabled = $false } $gameBox
    $result = New-Control Label @{ Location = (Point 138 168); Size = (Size 372 62) } $gameBox
    $form.AcceptButton = $signIn

    # 3. Activity
    $activityBox = New-Control GroupBox @{ Text = '3.  Activity'; Location = (Point 18 444); Size = (Size 524 162) } $form
    $activity = New-Control ListBox @{ Location = (Point 14 26); Size = (Size 496 124); IntegralHeight = $false
        HorizontalScrollbar = $true } $activityBox

    if ($state.console) {
        $remoteRadio.Checked = $true
        $address.Text = $state.console
    } else {
        $localRadio.Checked = $true
    }

    # One network task at a time: its continuation runs on the window's thread,
    # after the function that started it has returned, so what it needs is kept
    # in script scope ($script:remote, $script:target, $script:pin, $script:identity).
    $script:pending = $null
    $script:identity = $null
    function Run-Task($task, [scriptblock] $done) {
        $script:pending = @{ task = $task; done = $done }
        Update-Controls
    }
    function Get-TaskReason($task) {
        $exception = $task.Exception
        while ($exception.InnerException) { $exception = $exception.InnerException }
        return $exception.Message
    }
    function Set-Result([string] $text, [string] $kind = 'plain') {
        $result.Text = $text
        $result.ForeColor = switch ($kind) { 'ok' { $okColor } 'error' { $errorColor } default { $plainColor } }
    }
    function Add-Activity([string] $text) {
        $activity.Items.Add($text) | Out-Null
        while ($activity.Items.Count -gt 300) { $activity.Items.RemoveAt(0) }
        $activity.TopIndex = [Math]::Max(0, $activity.Items.Count - 1)
    }
    function Update-Controls {
        $busy = $null -ne $script:pending
        $remote = $remoteRadio.Checked
        foreach ($control in @($hint, $addressLabel, $address, $codeLabel, $code)) { $control.Enabled = $remote }
        $signIn.Enabled = (-not $busy) -and ($null -ne $script:identity)
        $eaRetry.Enabled = -not $busy
        $form.UseWaitCursor = $busy
    }

    function Start-Ea {
        $eaStatus.Text = 'Getting a token from the EA app...'
        $eaStatus.ForeColor = $plainColor
        $eaRetry.Visible = $false
        Run-Task $helper.MintAsync() {
            param($task)
            if ($task.IsFaulted) {
                $script:identity = $null
                $eaStatus.Text = Get-TaskReason $task
                $eaStatus.ForeColor = $errorColor
                $eaRetry.Visible = $true
                Set-Result ''
                return
            }
            $script:identity = $task.Result
            $eaStatus.Text = "Signed in to EA as account $($script:identity[0])."
            $eaStatus.ForeColor = $okColor
            Find-Game
        }
    }

    # Signs in a game that is already running here, or the console paired last time.
    function Find-Game {
        Set-Result 'Looking for Northstar...'
        Run-Task $helper.HelloAsync('127.0.0.1') {
            param($task)
            if ($task.Result) {
                $localRadio.Checked = $true
                Start-SignIn
            } elseif ($state.console) {
                Run-Task $helper.HelloAsync($state.console) {
                    param($task)
                    if ($task.Result) { Start-SignIn }
                    else { Set-Result "Northstar is not running on $($state.console). Start it, then select Sign in." }
                }
            } else {
                Set-Result 'Choose where Northstar is running, then select Sign in.'
            }
        }
    }

    function Start-SignIn {
        $script:remote = $remoteRadio.Checked
        if ($script:remote) {
            $script:target = $address.Text.Trim()
            $script:pin = $code.Text.Trim()
            if (-not [NsTokenHelper]::IsAddress($script:target)) { Set-Result 'Type the address the game shows, for example 192.168.1.20.' 'error'; return }
            if ($script:pin -notmatch '^\d{4}$' -and -not ($script:pin -eq '' -and $script:target -eq $state.console)) {
                Set-Result 'Type the 4-digit code the game shows.' 'error'
                return
            }
        } else {
            $script:target = '127.0.0.1'
            $script:pin = ''
        }
        Set-Result 'Signing in...'
        # A token from the last minute is reused; an older one is replaced.
        Run-Task $helper.MintAsync() {
            param($task)
            if ($task.IsFaulted) { Set-Result (Get-TaskReason $task) 'error'; return }
            $script:identity = $task.Result
            if (-not $script:remote) {
                Run-Task $helper.HelloAsync($script:target) {
                    param($task)
                    if ($task.Result) {
                        Run-Task $helper.SignInAsync($script:target, '', $script:identity) { param($task) Complete-SignIn $task.Result $false }
                    } else {
                        try {
                            $helper.WriteIdentity($script:identity, "http://127.0.0.1:$Port/atlas/token")
                        } catch {
                            Set-Result "Could not save the sign-in: $(Get-Reason $_)" 'error'
                            return
                        }
                        Complete-SignIn $null $false $true
                    }
                }
            } else {
                Run-Task $helper.SignInAsync($script:target, $script:pin, $script:identity) { param($task) Complete-SignIn $task.Result $true }
            }
        }
    }

    function Complete-SignIn($failure, [bool] $remote, [bool] $saved = $false) {
        if ($failure) {
            $text = "$($failure.Substring(0, 1).ToUpper())$($failure.Substring(1))."
            Set-Result $text 'error'
            return
        }
        $serving = Start-Serving $remote
        if ($remote) {
            $state.console = $script:target
            Save-HelperState $state
            $code.Text = ''
            $message = "Signed in Northstar on $script:target. Select Launch Northstar in the game."
            Add-Activity "$(Get-Date -Format T)  signed in Northstar on $script:target"
        } elseif ($saved) {
            $message = 'Saved. Northstar in shadPS4 on this PC picks up the sign-in when it starts.'
            Add-Activity "$(Get-Date -Format T)  saved the sign-in for shadPS4 on this PC"
        } else {
            $message = 'Signed in Northstar in shadPS4 on this PC. Select Launch Northstar in the game.'
            Add-Activity "$(Get-Date -Format T)  signed in Northstar in shadPS4 on this PC"
        }
        if ($serving) { Set-Result "$message But $serving" 'error' }
        else { Set-Result $message 'ok' }
    }

    $timer = New-Object Windows.Forms.Timer
    $timer.Interval = 150
    $timer.Add_Tick({
        try {
            $line = $null
            while ($helper.Events.TryDequeue([ref] $line)) { Add-Activity $line }
            $job = $script:pending
            if ($job -and $job.task.IsCompleted) {
                $script:pending = $null
                & $job.done $job.task
                Update-Controls
            }
        } catch {
            $script:pending = $null
            Set-Result "Something went wrong: $(Get-Reason $_)" 'error'
            Update-Controls
        }
    })

    $localRadio.Add_CheckedChanged({ Update-Controls })
    $remoteRadio.Add_CheckedChanged({ Update-Controls })
    $signIn.Add_Click({ Start-SignIn })
    $eaRetry.Add_Click({ Start-Ea })
    $code.Add_KeyPress({ param($sender, $e) if (-not [char]::IsDigit($e.KeyChar) -and -not [char]::IsControl($e.KeyChar)) { $e.Handled = $true } })
    $form.Add_Shown({ $timer.Start(); Start-Ea })
    $form.Add_FormClosed({ $timer.Stop(); $helper.Stop() })

    Update-Controls
    [Windows.Forms.Application]::Run($form)
}
