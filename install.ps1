<#
.SYNOPSIS
    Installs (or removes) the HL7Lens plugin for Notepad++ from its GitHub releases.

.DESCRIPTION
    Finds Notepad++, works out whether it is 64-bit or 32-bit, downloads the matching HL7Lens release,
    checks its SHA-256 checksum and Authenticode signature, and copies the plugin into
    <Notepad++>\plugins\HL7Lens\ when that folder is writable. It never asks for administrator rights
    unless you pass -Admin: when the plugins folder is protected (for example under C:\Program Files),
    it saves the checked plugin and install instructions to Downloads\HL7Lens-Plugin instead.
    When it finishes it shows a pop-up and opens Notepad++ with a short getting-started guide
    (skipped with -Quiet, and when it runs as SYSTEM or without a desktop).

.EXAMPLE
    irm https://raw.githubusercontent.com/Subzero121800/HL7Lens/main/install.ps1 | iex

.EXAMPLE
    powershell -c "irm https://raw.githubusercontent.com/Subzero121800/HL7Lens/main/install.ps1|iex"
    (paste into the Windows Run box, Win+R; the window closes by itself and a pop-up shows the result)

.EXAMPLE
    install.cmd                                            # from an unpacked release zip: installs the files beside it

.EXAMPLE
    .\install.ps1 -NppPath D:\Tools\npp-portable          # portable Notepad++
    .\install.ps1 -Version 1.1.0                           # a specific release
    .\install.ps1 -ZipPath .\HL7Lens_NotepadPlusPlus_v1.1.0.zip   # offline, from a downloaded zip
    .\install.ps1 -Admin                                   # copy into C:\Program Files\Notepad++ (Windows asks for admin approval)
    .\install.ps1 -Quiet                                   # no pop-ups and do not start Notepad++ (scripted installs)
    .\install.ps1 -Uninstall

.NOTES
    HL7Lens - Copyright (c) 2026 Joseph C McGinty Jr. - GNU GPL v3 - https://github.com/Subzero121800/HL7Lens
#>
[CmdletBinding()]
param(
    [string]$Version = 'latest',
    [string]$NppPath,
    [string]$ZipPath,
    [switch]$Download,
    [switch]$Uninstall,
    [switch]$Admin,
    [switch]$Quiet,
    [switch]$Force,
    [switch]$SkipSignatureCheck,
    [string]$Repo = 'Subzero121800/HL7Lens',
    [string]$Token = $env:GITHUB_TOKEN
)

$ErrorActionPreference = 'Stop'
$PluginName = 'HL7Lens'
# SHA-1 thumbprint of the author's code-signing certificate (CN=Joseph C McGinty Jr.), as Windows shows it
$SignerThumbprint = '52C94A9DFEE33D25429354D2A7837EC227B665F4'
$OnWindows = ($PSVersionTable.PSEdition -eq 'Desktop') -or $IsWindows

function Say([string]$Text, [string]$Color = 'Gray') { Write-Host $Text -ForegroundColor $Color }
function Fail([string]$Text) { Write-Host ''; Write-Host "HL7Lens install stopped: $Text" -ForegroundColor Red; throw [System.OperationCanceledException]::new($Text) }

$Interactive = $OnWindows -and -not $Quiet -and [Environment]::UserInteractive -and
    ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value -ne 'S-1-5-18')   # never pop up for SYSTEM / service installs

function Show-Popup([string]$Text, [string]$Icon = 'Information', [string]$Buttons = 'OK') {
    # Returns the button pressed ('OK', 'Cancel', ...); 'OK' when no pop-up can be shown.
    if (-not $Interactive) { return 'OK' }
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false }
        try { return [string][System.Windows.Forms.MessageBox]::Show($owner, $Text, 'HL7Lens for Notepad++', $Buttons, $Icon) }
        finally { $owner.Dispose() }
    } catch { return 'OK' }
}

function Get-DownloadsFolder {
    $d = try { (New-Object -ComObject Shell.Application).NameSpace('shell:Downloads').Self.Path } catch { $null }
    if ($d) { $d } else { Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads' }
}

function Open-InNotepadPlusPlus([string]$Npp, [string]$File) {
    if (-not $Interactive) { return }
    try { Start-Process -FilePath (Join-Path $Npp 'notepad++.exe') -ArgumentList ('"' + $File + '"') } catch { try { Start-Process notepad.exe ('"' + $File + '"') } catch { } }
}

function Write-GettingStarted([string]$Path) {
    @(
        'HL7Lens for Notepad++ - getting started'
        '======================================='
        ''
        'HL7Lens reads HL7 v2 messages in plain language. Everything runs on this PC; nothing is sent anywhere.'
        ''
        'TRY IT NOW'
        '  1. Press Ctrl+Alt+H to open the HL7Lens panel (or Plugins > HL7Lens > HL7Lens panel).'
        '  2. Click anywhere on the sample message at the bottom of this page.'
        '     The panel shows where you are (for example PID-5.1), the field name, its data type and what the value means.'
        ''
        'HANDY KEYS'
        '  Ctrl+Alt+H              show or hide the panel'
        '  Ctrl+Alt+G              go to a field path, for example PID-3 or OBX[2]-5'
        '  Ctrl+Alt+Right / Left   next / previous field'
        '  Ctrl+Alt+PgDn / PgUp    next / previous message'
        ''
        'MORE IN THE MENU (Plugins > HL7Lens)'
        '  Validation report             checks every message in the file'
        '  Compare message at caret      with another message, the clipboard or the other view'
        '  Build ACK                     makes an acknowledgment for the message at the caret'
        '  De-identified copy            a copy with identifying fields replaced (best effort: review it before sharing)'
        '  Repair message                fixes MLLP framing, blank and wrapped lines (Ctrl+Z undoes it)'
        '  Define custom segments        teach it your Z-segments'
        '  Install HL7 v2 syntax colors  then restart Notepad++ once; .hl7 files color automatically'
        ''
        'Questions or ideas: https://github.com/Subzero121800/HL7Lens/discussions'
        'Please share only made-up messages, never real patient data.'
        ''
        'SAMPLE MESSAGE (all made up) - click a line, then press Ctrl+Alt+H'
        ''
        'MSH|^~\&|SENDING_APP|SENDING_FACILITY|RECEIVING_APP|RECEIVING_FACILITY|20261020095500||ADT^A01^ADT_A01|MSG00001|P|2.5.1'
        'EVN|A01|20261020095500'
        'PID|1||123456789^^^HOSPITAL_MRN||Doe^John^A^^Jr||19800101|M|||123 Main St^^Anytown^PA^15063^USA||(555)555-5555|||M|NON|987654321'
        'PV1|1|I|Wards^3West^301^A||||1234^Smith^Jane^Dr|||||||||||IP|VIS123456^^^HOSPITAL_VISIT'
    ) | Set-Content -Path $Path -Encoding UTF8
}

function Find-NotepadPlusPlus {
    if ($NppPath) {
        $p = $NppPath
        if ((Test-Path $p -PathType Leaf) -and ($p -like '*.exe')) { $p = Split-Path $p -Parent }
        if (-not (Test-Path (Join-Path $p 'notepad++.exe'))) { Fail "notepad++.exe was not found in '$NppPath'." }
        return (Resolve-Path $p).Path
    }
    $candidates = @()
    if ($OnWindows) {
        foreach ($key in 'HKLM:\SOFTWARE\Notepad++', 'HKLM:\SOFTWARE\WOW6432Node\Notepad++', 'HKCU:\SOFTWARE\Notepad++') {
            try { $v = (Get-ItemProperty -Path $key -ErrorAction Stop).'(default)'; if ($v) { $candidates += $v } } catch { }
        }
    }
    foreach ($base in $env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LOCALAPPDATA) { if ($base) { $candidates += (Join-Path $base 'Notepad++') } }
    foreach ($c in $candidates) { if ($c -and (Test-Path (Join-Path $c 'notepad++.exe'))) { return $c } }
    Fail "Notepad++ was not found. For a portable copy, run again with -NppPath <folder that holds notepad++.exe>."
}

function Get-ExeArchitecture([string]$Exe) {
    $fs = [System.IO.File]::OpenRead($Exe)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Position = 0x3C; $pe = $br.ReadInt32(); $fs.Position = $pe + 4; $machine = $br.ReadUInt16()
    } finally { $fs.Dispose() }
    switch ($machine) { 0x8664 { 'x64' } 0x014C { 'x86' } 0xAA64 { 'arm64' } default { 'unknown' } }
}

function Invoke-Download([string]$Url, [string]$OutFile, [bool]$Api) {
    $headers = @{ 'User-Agent' = 'HL7Lens-installer' }
    if ($Api) { $headers['Accept'] = 'application/vnd.github+json' } else { $headers['Accept'] = 'application/octet-stream' }
    if ($Token) { $headers['Authorization'] = "Bearer $Token" }
    if ($OutFile) { Invoke-WebRequest -Uri $Url -Headers $headers -OutFile $OutFile -UseBasicParsing | Out-Null }
    else { Invoke-RestMethod -Uri $Url -Headers $headers }
}

function Get-ReleaseZip([string]$WorkDir) {
    if ($ZipPath) { if (-not (Test-Path $ZipPath)) { Fail "'$ZipPath' does not exist." }; return (Resolve-Path $ZipPath).Path }
    try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }
    $api = "https://api.github.com/repos/$Repo/releases/" + $(if ($Version -eq 'latest') { 'latest' } else { 'tags/v' + $Version.TrimStart('v') })
    Say "Looking up release $Version of $Repo ..."
    try { $rel = Invoke-Download $api $null $true }
    catch {
        if (Get-Command gh -ErrorAction SilentlyContinue) {
            Say 'GitHub API not reachable without sign-in; trying the GitHub CLI (gh) ...'
            $tag = if ($Version -eq 'latest') { @() } else { @('v' + $Version.TrimStart('v')) }
            & gh release download @tag --repo $Repo --pattern 'HL7Lens_NotepadPlusPlus_v*.zip*' --dir $WorkDir --clobber
            if ($LASTEXITCODE -ne 0) { Fail 'gh release download failed. Run "gh auth login" first.' }
            $zip = Get-ChildItem $WorkDir -Filter 'HL7Lens_NotepadPlusPlus_v*.zip' | Select-Object -First 1
            if (-not $zip) { Fail 'The release has no HL7Lens zip.' }
            Test-Checksum $zip.FullName (Join-Path $WorkDir ($zip.Name + '.sha256'))
            return $zip.FullName
        }
        Fail "Could not read the release from GitHub ($($_.Exception.Message)). If the repository is private, pass -Token <token> or install the GitHub CLI and sign in."
    }
    $asset = $rel.assets | Where-Object { $_.name -like 'HL7Lens_NotepadPlusPlus_v*.zip' } | Select-Object -First 1
    if (-not $asset) { Fail "Release $($rel.tag_name) has no HL7Lens zip attached." }
    $zipFile = Join-Path $WorkDir $asset.name
    Say "Downloading $($asset.name) ($([math]::Round($asset.size / 1MB, 1)) MB) ..."
    $dl = if ($Token) { $asset.url } else { $asset.browser_download_url }
    Invoke-Download $dl $zipFile $false
    $sum = $rel.assets | Where-Object { $_.name -eq ($asset.name + '.sha256') } | Select-Object -First 1
    if ($sum) { $sumFile = Join-Path $WorkDir $sum.name; Invoke-Download $(if ($Token) { $sum.url } else { $sum.browser_download_url }) $sumFile $false; Test-Checksum $zipFile $sumFile }
    else { Say 'No checksum file in this release; relying on the signature check.' 'Yellow' }
    return $zipFile
}

function Test-Checksum([string]$File, [string]$SumFile) {
    if (-not (Test-Path $SumFile)) { return }
    $want = ((Get-Content $SumFile -Raw).Trim() -split '\s+')[0].ToLower()
    $have = (Get-FileHash $File -Algorithm SHA256).Hash.ToLower()
    if ($want -ne $have) { Fail "Checksum mismatch for $(Split-Path $File -Leaf): expected $want, got $have." }
    Say 'SHA-256 checksum OK.' 'Green'
}

function Test-Signature([string]$Dll) {
    if ($SkipSignatureCheck) { Say 'Signature check skipped (-SkipSignatureCheck).' 'Yellow'; return }
    if (-not (Get-Command Get-AuthenticodeSignature -ErrorAction SilentlyContinue)) { Say 'Signature check not available on this system; skipped.' 'Yellow'; return }
    $sig = Get-AuthenticodeSignature -FilePath $Dll
    if (-not $sig.SignerCertificate) { Fail "$(Split-Path $Dll -Leaf) is not signed. Refusing to install (use -SkipSignatureCheck to override)." }
    if ($sig.SignerCertificate.Thumbprint -ne $SignerThumbprint) { Fail "$(Split-Path $Dll -Leaf) is signed by '$($sig.SignerCertificate.Subject)', not by the HL7Lens author. Refusing to install." }
    Say "Signed by $($sig.SignerCertificate.Subject) (thumbprint $SignerThumbprint)." 'Green'
    if ($sig.Status -ne 'Valid') { Say "Windows reports the signature as '$($sig.Status)': the author's certificate is self-issued and not in this PC's trusted store. See README > Signature to trust it (optional)." 'DarkGray' }
}

function Test-Writable([string]$Dir) {
    try { if (-not (Test-Path $Dir)) { New-Item -ItemType Directory -Path $Dir -ErrorAction Stop | Out-Null }; $t = Join-Path $Dir ('.hl7lens-' + [guid]::NewGuid().ToString('N')); [IO.File]::WriteAllText($t, ''); Remove-Item $t -Force; return $true } catch { return $false }
}

function Quote([string]$Path) { "'" + $Path.Replace("'", "''") + "'" }   # single-quoted PowerShell literal

function Invoke-Elevated([string]$Command) {
    # Returns $true when the elevated copy finished, $false when there are no admin rights or the prompt was cancelled.
    if (-not $OnWindows) { return $false }
    Say 'The Notepad++ plugins folder needs administrator rights; Windows will ask for permission ...' 'Yellow'
    $exe = (Get-Process -Id $PID).Path
    try { $p = Start-Process -FilePath $exe -Verb RunAs -Wait -PassThru -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $Command) }
    catch { return $false }
    return ($p.ExitCode -eq 0)
}

function Save-ToDownloads([string]$Src, [string]$Target, [string]$Arch) {
    # Protected plugins folder and no -Admin: leave the checked plugin and instructions in the user's Downloads.
    $root = Join-Path (Get-DownloadsFolder) 'HL7Lens-Plugin'
    if (Test-Path $root) { Remove-Item $root -Recurse -Force }
    New-Item -ItemType Directory -Path $root | Out-Null
    Copy-Item $Src (Join-Path $root $PluginName) -Recurse -Force
    $dll = Join-Path (Join-Path $root $PluginName) "$PluginName.dll"
    $hash = (Get-FileHash $dll -Algorithm SHA256).Hash
    $ver = (Get-Item $dll).VersionInfo.ProductVersion
    $plugins = Split-Path $Target -Parent
    @(
        'HL7Lens plugin for Notepad++ - how to install'
        ''
        "Notepad++ on this PC keeps its plugins in a protected folder, so this copy was saved here instead:"
        "    $root"
        ''
        'TO INSTALL'
        '  1. Close Notepad++.'
        '  2. Copy the HL7Lens folder that sits next to this file into:'
        "         $plugins"
        "     so that this file exists:  $(Join-Path $Target "$PluginName.dll")"
        '     (Windows asks for administrator approval for that folder.)'
        '  3. Start Notepad++ and press Ctrl+Alt+H.'
        ''
        'NO ADMIN RIGHTS? (most State, Federal and hospital PCs)'
        '  Open an IT ticket, attach this file, and ask IT to do step 2. The details they usually need:'
        ''
        "  Version:    $ver ($Arch)"
        "  SHA-256:    $hash ($PluginName.dll)"
        "  Signed by:  CN=Joseph C McGinty Jr. (thumbprint $SignerThumbprint)"
        "  Source:     https://github.com/$Repo (GPL-3.0, free and open source)"
        '  Behavior:   runs offline inside Notepad++; makes no network connections, no telemetry, stores no message content.'
        '              (Its "Support development" menu item and About box only open a web page in the browser when clicked.)'
        ''
        '  Silent install for many PCs (run as administrator or SYSTEM, Notepad++ closed):'
        '      powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 -ZipPath <path to HL7Lens_NotepadPlusPlus zip>'
    ) | Set-Content -Path (Join-Path $root 'HOW-TO-INSTALL.txt') -Encoding UTF8
    Say ''
    Say "Notepad++'s plugins folder is protected on this PC, so nothing was changed there and no admin rights were needed." 'Yellow'
    Say "HL7Lens and install instructions are saved in: $root" 'Green'
    Say "Next: copy the HL7Lens folder into $plugins (or ask IT to). Details are in HOW-TO-INSTALL.txt."
    if ($Interactive) {
        try { Start-Process explorer.exe ('"' + $root + '"') } catch { }
        [void](Show-Popup ("Notepad++ keeps its plugins in a protected folder on this PC, so HL7Lens was saved to your Downloads instead:`n`n    $root`n`n" +
            "Copy the HL7Lens folder into:`n    $plugins`n`nNo admin rights? Send HOW-TO-INSTALL.txt to IT with your request. It opens next."))
        Open-InNotepadPlusPlus (Split-Path $plugins -Parent) (Join-Path $root 'HOW-TO-INSTALL.txt')
    }
}

function Stop-NotepadPlusPlusIfRunning([string]$Dir) {
    $find = { Get-Process -Name 'notepad++' -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -and ((Split-Path $_.Path -Parent) -eq $Dir) } catch { $true } } }
    $running = & $find
    if (-not $running) { return }
    if ($Force) { Say 'Closing Notepad++ (-Force) ...' 'Yellow'; $running | Stop-Process -Force; Start-Sleep -Milliseconds 800; return }
    if ($Interactive -and (Show-Popup "Notepad++ is open and has to close for the install.`n`nClick OK to close it (it will ask about any unsaved work), or Cancel to stop." 'Warning' 'OKCancel') -eq 'OK') {
        $running | ForEach-Object { [void]$_.CloseMainWindow() }
        for ($i = 0; $i -lt 60 -and (& $find); $i++) { Start-Sleep -Milliseconds 500 }
        if (-not (& $find)) { return }
    }
    Fail 'Notepad++ is running. Close it (save your work first) and run this again, or add -Force to close it.'
}

# ------------------------------------------------------------------------------------------------
function Install-HL7Lens {
    Say ''
    Say 'HL7Lens for Notepad++  -  installer' 'Cyan'
    $npp = Find-NotepadPlusPlus
    $arch = Get-ExeArchitecture (Join-Path $npp 'notepad++.exe')
    $target = Join-Path (Join-Path $npp 'plugins') $PluginName
    Say "Notepad++: $npp ($arch)"
    if ($arch -eq 'arm64') { Fail 'This is ARM64 Notepad++. HL7Lens ships x64 and x86 builds only; install the x64 Notepad++ to use it.' }
    if ($arch -eq 'unknown') { Fail 'Could not tell whether notepad++.exe is 64-bit or 32-bit.' }
    if ($Admin -or (Test-Writable (Split-Path $target -Parent))) { Stop-NotepadPlusPlusIfRunning $npp }   # only when we will write there

    if ($Uninstall) {
        if (-not (Test-Path $target)) { Say 'HL7Lens is not installed in this Notepad++.' 'Yellow'; return }
        if (Test-Writable (Split-Path $target -Parent)) { Remove-Item $target -Recurse -Force }
        elseif (-not $Admin -or -not (Invoke-Elevated "Remove-Item -LiteralPath $(Quote $target) -Recurse -Force")) { Fail "Removing HL7Lens needs administrator rights on this PC. Run again with -Admin, or ask IT to delete the folder $target" }
        Say "Removed $target. Your settings stay in plugins\Config\HL7Lens (delete that folder too for a clean removal)." 'Green'
        [void](Show-Popup 'HL7Lens was removed from Notepad++.')
        return
    }

    $work = Join-Path ([IO.Path]::GetTempPath()) ('hl7lens-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        # run from an unpacked release (install.cmd / install.ps1 next to x64\ and x86\): use those files, no download
        $here = if ($PSScriptRoot) { $PSScriptRoot } else { $null }
        $local = if ($here -and -not $ZipPath -and -not $Download -and $Version -eq 'latest') { Join-Path (Join-Path $here $arch) $PluginName } else { $null }
        if ($local -and (Test-Path (Join-Path $local "$PluginName.dll"))) {
            Say "Installing from this folder: $here"
            $src = $local
        } else {
            $zip = Get-ReleaseZip $work
            $unz = Join-Path $work 'x'
            Expand-Archive -LiteralPath $zip -DestinationPath $unz -Force
            $src = Join-Path (Join-Path $unz $arch) $PluginName
        }
        $dll = Join-Path $src "$PluginName.dll"
        if (-not (Test-Path $dll)) { Fail "The zip has no $arch\$PluginName\$PluginName.dll." }
        Test-Signature $dll
        if ($OnWindows) { Get-ChildItem $src -Recurse -File | Unblock-File }
        $plugins = Split-Path $target -Parent
        if (Test-Writable $plugins) {
            if (Test-Path $target) { Remove-Item $target -Recurse -Force }
            Copy-Item $src $target -Recurse -Force
        } elseif (-not $Admin) {
            Save-ToDownloads $src $target $arch
            return
        } elseif (-not (Invoke-Elevated "if (Test-Path -LiteralPath $(Quote $target)) { Remove-Item -LiteralPath $(Quote $target) -Recurse -Force }; Copy-Item -LiteralPath $(Quote $src) -Destination $(Quote $target) -Recurse -Force")) {
            Save-ToDownloads $src $target $arch
            return
        }
        if (-not (Test-Path (Join-Path $target "$PluginName.dll"))) { Fail 'The plugin was not copied.' }
        Test-Signature (Join-Path $target "$PluginName.dll")
        $ver = (Get-Item (Join-Path $target "$PluginName.dll")).VersionInfo.ProductVersion
        Say ''
        Say ("HL7Lens " + $(if ($ver) { "$ver " } else { '' }) + "installed to $target") 'Green'
        Say 'Start Notepad++, then Plugins > HL7Lens > HL7Lens panel (Ctrl+Alt+H).'
        if ($Interactive) {
            $guide = Join-Path (Get-DownloadsFolder) 'HL7Lens-Getting-Started.txt'
            try { Write-GettingStarted $guide } catch { $guide = $null }
            [void](Show-Popup ("HL7Lens " + $(if ($ver) { "$ver " } else { '' }) + "is installed.`n`nNotepad++ opens next with a short getting-started guide and a sample message. Press Ctrl+Alt+H there to open the HL7Lens panel."))
            if ($guide) { Open-InNotepadPlusPlus $npp $guide }
        }
    } finally {
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

try { Install-HL7Lens }
catch [System.OperationCanceledException] { $global:LASTEXITCODE = 1; [void](Show-Popup ("HL7Lens was not installed.`n`n" + $_.Exception.Message) 'Warning') }
catch { [void](Show-Popup ("HL7Lens was not installed because of an unexpected error:`n`n" + $_.Exception.Message) 'Error'); throw }   # message already shown; no stack trace, and never 'exit' (would close a shell running 'irm | iex')
