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

1. **Install BinCue Studio on the host** with the regular Windows installer.
   It bundles `cdrdao.exe`, so there is nothing else to install — the
   installer records the install directory in the `BINCUE_STUDIO_HOME`
   environment variable, which is how the client finds cdrdao. (For an
   install made with an older installer, either reinstall or add the
   install directory to the account's PATH.)

2. **Enable the OpenSSH Server** (a built-in Windows optional feature). In an
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

3. **Set up key authentication.** Windows has no `ssh-copy-id`, and there is a
   quirk: for accounts in the Administrators group (most personal machines),
   OpenSSH reads keys from a shared system file instead of the user profile.

   *Administrator accounts* — from your client machine (this one prompts for
   the Windows password; that's fine in a terminal, only the app itself can't
   answer prompts):

   ```sh
   cat ~/.ssh/id_ed25519.pub | ssh user@winhost 'powershell -NoProfile -Command "[Console]::In.ReadToEnd() | Add-Content -Force -Path C:\ProgramData\ssh\administrators_authorized_keys"'
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

4. **Test.** From the client machine (should print your burner without asking
   for a password):

   ```sh
   ssh user@winhost "powershell -NoProfile -Command Get-CimInstance Win32_CDROMDrive"
   ```

## Troubleshooting

- **"Permission denied" from the app but `ssh <host>` works in a terminal** —
  the terminal is falling back to a password prompt, which the app cannot do.
  Verify `ssh -o BatchMode=yes <host> true` succeeds.
- **Connects but no drives are listed** — Linux: check the group membership in
  step 4 and reconnect; Windows: confirm the test command in step 4 prints the
  drive.
- **Windows key refused for an admin account** — the key must be in
  `administrators_authorized_keys` (not the user profile) *and* the `icacls`
  permission fix must have been applied.
- **Burn starts but fails opening the device** — another program may hold the
  drive (a file manager auto-mounting an inserted disc, or on Linux a leftover
  mount; `umount /dev/sr0` and retry).
