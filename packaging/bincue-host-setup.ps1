<#
.SYNOPSIS
    Configure this Windows machine as a BinCue Studio remote burning host.

.DESCRIPTION
    Enables the OpenSSH Server, installs a client's public key for
    password-free (SSH batch mode) login, and applies the file permissions
    OpenSSH insists on. Run this ON THE HOST: it does everything host-side, so
    it makes no difference whether the machine you burn *from* runs Linux or
    Windows. See docs/remote-burning.md for the full picture.

    Safe to re-run — every step checks the current state before changing it.

.PARAMETER PublicKey
    The client's SSH public key as a single line
    (e.g. "ssh-ed25519 AAAA... you@laptop"). If neither this nor
    -PublicKeyFile is given, the script prompts for it.

.PARAMETER PublicKeyFile
    Path to a .pub file on this host to read the key from instead.

.PARAMETER User
    The local account you will SSH in as. Defaults to the current user. This
    decides which authorized_keys file is used: members of the Administrators
    group share one system file, standard users get a per-profile file.

.EXAMPLE
    .\bincue-host-setup.ps1 -PublicKey "ssh-ed25519 AAAA... me@laptop"

.EXAMPLE
    .\bincue-host-setup.ps1 -PublicKeyFile C:\Users\me\key.pub -User me
#>
[CmdletBinding()]
param(
    [string] $PublicKey,
    [string] $PublicKeyFile,
    [string] $User = $env:USERNAME
)

$ErrorActionPreference = 'Stop'

# --- Re-launch elevated if needed -------------------------------------------
# Installing the capability, managing the service, and the icacls fix under
# C:\ProgramData all need administrator rights.
$principal = [Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Elevation required - relaunching as administrator..." -ForegroundColor Yellow
    $argList = @('-NoExit', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                 '-File', "`"$PSCommandPath`"", '-User', "`"$User`"")
    if ($PublicKey)     { $argList += @('-PublicKey',     "`"$PublicKey`"") }
    if ($PublicKeyFile) { $argList += @('-PublicKeyFile', "`"$PublicKeyFile`"") }
    Start-Process powershell -Verb RunAs -ArgumentList $argList
    return
}

# --- Resolve the public key -------------------------------------------------
if ($PublicKeyFile) {
    $PublicKey = (Get-Content -Raw -Path $PublicKeyFile).Trim()
}
if (-not $PublicKey) {
    Write-Host "Paste the client's SSH public key (one line - the contents of"
    Write-Host "~/.ssh/id_ed25519.pub on the machine you'll burn from):"
    $PublicKey = (Read-Host 'Public key').Trim()
}
if ($PublicKey -notmatch '^(ssh-ed25519|ssh-rsa|ecdsa-sha2-\S+|sk-\S+)\s') {
    throw "That does not look like an SSH public key: '$PublicKey'"
}

# --- Enable the OpenSSH Server ----------------------------------------------
$cap = Get-WindowsCapability -Online -Name 'OpenSSH.Server*' |
    Select-Object -First 1
if (-not $cap) {
    throw "OpenSSH Server is not an available Windows capability on this build. " +
          "Install it via Settings -> System -> Optional features."
}
if ($cap.State -ne 'Installed') {
    Write-Host "Installing OpenSSH Server..." -ForegroundColor Cyan
    Add-WindowsCapability -Online -Name $cap.Name | Out-Null
} else {
    Write-Host "OpenSSH Server already installed."
}

Set-Service -Name sshd -StartupType Automatic
if ((Get-Service sshd).Status -ne 'Running') { Start-Service sshd }
Write-Host "sshd is running and set to start automatically."

# The capability normally creates the inbound rule; add it if it is missing.
if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' `
        -DisplayName 'OpenSSH SSH Server (sshd)' `
        -Enabled True -Direction Inbound -Protocol TCP -Action Allow `
        -LocalPort 22 | Out-Null
    Write-Host "Opened inbound TCP 22 for sshd."
}

# --- Decide which authorized_keys file to use -------------------------------
# OpenSSH on Windows reads keys for members of the Administrators group from a
# single shared system file, not the user profile. Match by well-known SID
# (S-1-5-32-544) so this works regardless of the UI language.
try {
    $adminMembers = Get-LocalGroupMember -SID 'S-1-5-32-544' |
        ForEach-Object { $_.Name.Split('\')[-1] }
    $isAdminUser = $adminMembers -contains $User
} catch {
    # Domain-joined or otherwise not enumerable: assume admin, which is the
    # safe choice for a burn host and the shared file most personal accounts use.
    Write-Warning "Could not enumerate the Administrators group; assuming '$User' is an administrator."
    $isAdminUser = $true
}

if ($isAdminUser) {
    $keyFile = Join-Path $env:ProgramData 'ssh\administrators_authorized_keys'
} else {
    $profileDir = Get-CimInstance Win32_UserProfile |
        Where-Object { $_.LocalPath -like "*\$User" } |
        Select-Object -First 1 -ExpandProperty LocalPath
    if (-not $profileDir) { $profileDir = Join-Path $env:SystemDrive "Users\$User" }
    $sshDir = Join-Path $profileDir '.ssh'
    if (-not (Test-Path $sshDir)) {
        New-Item -ItemType Directory -Path $sshDir | Out-Null
    }
    $keyFile = Join-Path $sshDir 'authorized_keys'
}

# --- Install the key (idempotent) -------------------------------------------
$keyBody = ($PublicKey -split '\s+')[1]   # the base64 blob - the stable part
$already = $false
if (Test-Path $keyFile) {
    $already = @(Get-Content $keyFile |
        Where-Object { $_ -match [regex]::Escape($keyBody) }).Count -gt 0
}
if ($already) {
    Write-Host "Key already present in $keyFile."
} else {
    Add-Content -Path $keyFile -Value $PublicKey -Encoding ascii
    Write-Host "Added key to $keyFile." -ForegroundColor Green
}

# --- Fix the permissions OpenSSH insists on ---------------------------------
# Break inheritance and grant full control only to SYSTEM plus the owning
# principal; anything looser and sshd silently refuses the file. SIDs again
# for language independence: S-1-5-18 = SYSTEM, S-1-5-32-544 = Administrators.
if ($isAdminUser) {
    icacls.exe $keyFile /inheritance:r `
        /grant '*S-1-5-32-544:F' /grant '*S-1-5-18:F' | Out-Null
} else {
    $userSid = (New-Object Security.Principal.NTAccount($User)).Translate(
        [Security.Principal.SecurityIdentifier]).Value
    icacls.exe $keyFile /inheritance:r `
        /grant "*${userSid}:F" /grant '*S-1-5-18:F' | Out-Null
}
Write-Host "Locked down permissions on $keyFile."

# --- Done -------------------------------------------------------------------
$hostAddr = Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.PrefixOrigin -ne 'WellKnown' -and $_.IPAddress -ne '127.0.0.1' } |
    Select-Object -First 1 -ExpandProperty IPAddress
if (-not $hostAddr) { $hostAddr = $env:COMPUTERNAME }

Write-Host ""
Write-Host "This machine is ready as a BinCue Studio burning host." -ForegroundColor Green
Write-Host "From the machine you'll burn from, verify password-free login:"
Write-Host ""
Write-Host "    ssh $User@$hostAddr `"powershell -NoProfile -Command Get-CimInstance Win32_CDROMDrive`""
Write-Host ""
Write-Host "It should print your CD writer without asking for a password. Then"
Write-Host "enter  $User@$hostAddr  in the app's burn dialog."
