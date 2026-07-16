# Setting up a remote burning host

BinCue Studio can burn to a CD writer attached to *another* machine. The image
is mastered on your computer, uploaded over SSH to the host, and written there
with `cdrdao`, with the live burn log streamed back into the app.

A burning host needs three things:

1. **Reachable over SSH** with **key-based authentication** — the app connects
   in SSH batch mode, so password prompts cannot appear. If `ssh <host>` works
   from a terminal without asking for a password, the app can use it.
2. **`cdrdao` available** on the host (see per-OS sections below).
3. **Access to the optical drive** for the account you log in as.

Anything your `~/.ssh/config` can express — aliases, jump hosts, alternate
ports, identity files — applies, since the app drives the regular `ssh`
client. For options the config can't express per-run, set the
`HOSTKIT_SSH_OPTS` environment variable (extra `ssh` arguments, split
shell-style) before launching the app.

The upload goes to a temporary directory on the host and is deleted when the
burn ends, so the host needs free space for one disc image (up to ~800 MB).

## Linux host

1. **Install cdrdao** from your distribution:

   ```sh
   sudo pacman -S cdrdao        # Arch
   sudo apt install cdrdao      # Debian/Ubuntu
   sudo dnf install cdrdao      # Fedora
   ```

2. **Enable the SSH server** if it isn't already:

   ```sh
   sudo systemctl enable --now sshd
   ```

3. **Set up key authentication** from the computer you'll burn from:

   ```sh
   ssh-keygen -t ed25519        # if you don't have a key yet
   ssh-copy-id user@burnhost
   ```

4. **Grant drive access.** Desktop logins usually get the optical drive
   automatically, but SSH logins are not "seated at the console", so the
   account needs to be in the drive's group — `optical` on Arch, `cdrom` on
   Debian/Ubuntu/Fedora:

   ```sh
   sudo usermod -aG optical <user>    # or: -aG cdrom
   ```

   Group changes take effect on the next login (reconnect your SSH session).

5. **Test.** From the client machine:

   ```sh
   ssh user@burnhost lsblk    # the optical drive should appear as TYPE "rom"
   ```

   In the app, open the burn dialog, enter `user@burnhost` (or a config
   alias), and the host's drives should populate the device list.

## Windows host

> ⚠️ **Prefer a Linux host if you can.** The Windows host path is tested and
> works, but it is more setup and has rougher edges than Linux. Compare the two
> sections: the **Linux host** above is a handful of one-line commands from
> packages your distro already ships, while Windows needs a self-elevating
> PowerShell script, a shared-vs-per-user key-file quirk, an `icacls`
> permission dance, and a few caveats — for the *same* end result. One example
> of the rough edges: there is no reliable way to interrupt cdrdao, because
> Windows' ConPTY won't dependably deliver a ^C to a process that isn't sitting
> there reading its console, so on Windows **Stop just hard-kills the
> process.** A burn can't be resumed anyway, so a cancelled disc is ruined
> either way.
>
> The one reason to reach for a Windows host is a drive that has no working
> Linux driver. Even then, weigh it carefully: cdrdao's support for a drive
> that Linux can't talk to is unlikely to fare much better, so a Windows host
> is no guarantee the burn will work. When you have any choice at all, **burn
> from a Linux host** (above) — that is the smoother, better-trodden path.

The bundled setup script does all of the host-side work; the manual steps
underneath spell out exactly what it does for anyone who prefers to run them by
hand.

1. **Install BinCue Studio on the host** with the regular Windows installer.
   It bundles `cdrdao.exe`, so there is nothing else to install — the
   installer records the install directory in the `BINCUE_STUDIO_HOME`
   environment variable, which is how the client finds cdrdao. It also drops
   `bincue-host-setup.ps1` next to the executables. (For an install made with
   an older installer, either reinstall or add the install directory to the
   account's PATH.)

2. **Run the setup script on the host.** It enables the OpenSSH Server,
   installs your client's public key, and applies the file permissions OpenSSH
   requires — all host-side, so it makes no difference whether you burn *from*
   Linux or Windows. In any PowerShell (it self-elevates to administrator):

   ```powershell
   cd "$env:BINCUE_STUDIO_HOME"
   .\bincue-host-setup.ps1 -PublicKey "ssh-ed25519 AAAA... you@client"
   ```

   Pass your client's public key as the argument, or omit it and the script
   prompts. To read the key on the *client* machine: `cat ~/.ssh/id_ed25519.pub`
   on Linux, or `type $env:USERPROFILE\.ssh\id_ed25519.pub` in a Windows
   PowerShell. The script prints a login test command when it finishes; skip to
   *Test* below.

### Manual setup

Everything the script does, by hand.

1. **Enable the OpenSSH Server** (a built-in Windows optional feature). In an
   *administrator* PowerShell:

   ```powershell
   Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
   Set-Service sshd -StartupType Automatic
   Start-Service sshd
   ```

   (Or: Settings → System → Optional features → Add a feature → "OpenSSH
   Server".) The matching inbound firewall rule (`OpenSSH-Server-In-TCP`) is
   created automatically. The account's default SSH shell doesn't matter —
   the app invokes PowerShell explicitly.

2. **Set up key authentication.** Windows has no `ssh-copy-id`, and there is a
   quirk: for accounts in the Administrators group (most personal machines),
   OpenSSH reads keys from a shared system file instead of the user profile.

   *Administrator accounts* — append your public key from the client machine.
   This prompts for the Windows password; that's fine in a terminal, only the
   app itself can't answer prompts. **From a Linux client:**

   ```sh
   cat ~/.ssh/id_ed25519.pub | ssh user@winhost 'powershell -NoProfile -Command "[Console]::In.ReadToEnd() | Add-Content -Force -Path C:\ProgramData\ssh\administrators_authorized_keys"'
   ```

   **From a Windows client** (PowerShell — note the `\"` escaping needed to
   pass the inner quotes through to the host, which is exactly the fiddliness
   the setup script exists to avoid):

   ```powershell
   Get-Content $env:USERPROFILE\.ssh\id_ed25519.pub | ssh user@winhost "powershell -NoProfile -Command \"[Console]::In.ReadToEnd() | Add-Content -Force -Path C:\ProgramData\ssh\administrators_authorized_keys\""
   ```

   then, once, in an administrator PowerShell on the host (OpenSSH refuses
   the file with default inherited permissions):

   ```powershell
   icacls.exe "C:\ProgramData\ssh\administrators_authorized_keys" /inheritance:r /grant "Administrators:F" /grant "SYSTEM:F"
   ```

   *Standard (non-admin) accounts* use the usual per-user file instead:
   `C:\Users\<user>\.ssh\authorized_keys`, no permission fix needed. Note that
   raw disc writing may require administrator rights, so an admin account is
   the safe choice for a burning host.

### Test

From the client machine (should print your burner without asking for a
password):

```sh
ssh user@winhost "powershell -NoProfile -Command Get-CimInstance Win32_CDROMDrive"
```

## Troubleshooting

- **"Permission denied" from the app but `ssh <host>` works in a terminal** —
  the terminal is falling back to a password prompt, which the app cannot do.
  Verify `ssh -o BatchMode=yes <host> true` succeeds.
- **Connects but no drives are listed** — Linux: check the group membership in
  step 4 and reconnect; Windows: confirm the *Test* command prints the drive.
- **Windows key refused for an admin account** — the key must be in
  `administrators_authorized_keys` (not the user profile) *and* the `icacls`
  permission fix must have been applied.
- **Burn starts but fails opening the device** — another program may hold the
  drive (a file manager auto-mounting an inserted disc, or on Linux a leftover
  mount; `umount /dev/sr0` and retry).
