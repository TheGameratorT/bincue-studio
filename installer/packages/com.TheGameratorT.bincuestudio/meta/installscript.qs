function Component()
{
}

Component.prototype.createOperations = function()
{
    // Default implementation extracts the package data files.
    component.createOperations();

    if (systemInfo.productType === "windows") {
        // Records where the bundled cdrdao lives, so a remote BinCue Studio
        // can find it when this machine serves burns over SSH (see
        // docs/remote-burning.md). Persistent, per-user; undone on uninstall.
        component.addOperation("EnvironmentVariable",
            "BINCUE_STUDIO_HOME", "@TargetDir@", true, false);

        // Both applications get a Start Menu entry; only the main studio
        // gets a desktop shortcut.
        component.addOperation("CreateShortcut",
            "@TargetDir@/bincue-studio.exe",
            "@StartMenuDir@/BinCue Studio.lnk",
            "workingDirectory=@TargetDir@",
            "iconPath=@TargetDir@/bincue-studio.exe",
            "iconId=0",
            "description=Assemble audio tracks, export BIN/CUE, and burn CDs");

        component.addOperation("CreateShortcut",
            "@TargetDir@/cdlabel.exe",
            "@StartMenuDir@/CD Label Editor.lnk",
            "workingDirectory=@TargetDir@",
            "iconPath=@TargetDir@/cdlabel.exe",
            "iconId=0",
            "description=Design printable CD labels");

        component.addOperation("CreateShortcut",
            "@TargetDir@/bincue-studio.exe",
            "@DesktopDir@/BinCue Studio.lnk",
            "workingDirectory=@TargetDir@",
            "iconPath=@TargetDir@/bincue-studio.exe",
            "iconId=0",
            "description=Assemble audio tracks, export BIN/CUE, and burn CDs");
    }
}
