; NSIS installer for BinCue Studio. Replaces the old Qt Installer Framework
; setup, whose maintenancetool.exe (a statically-linked Qt app) cost ~50 MB on
; its own. NSIS's setup stub and uninstaller together are ~100 KB.
;
; Driven from build_installer.cmake, which passes every path/version in via /D:
;   VERSION      x.y.z (also used for the exe's version resource)
;   VIVERSION    x.y.z.0 (VIProductVersion needs four fields)
;   DIST_DIR     the deployed dist/ tree (both exes + all DLLs), native path
;   LICENSE_FILE the GPLv3 text shown on the license page
;   ICON         the .ico used for both the installer and uninstaller
;   OUTFILE      where to write the Setup exe

Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "WinMessages.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef VIVERSION
  !define VIVERSION "0.0.0.0"
!endif

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BinCue Studio"
; The system-wide environment block. BINCUE_STUDIO_HOME points at the install
; dir so a remote BinCue Studio can find the bundled cdrdao when this machine
; serves burns over SSH (see docs/remote-burning.md). QtIFW wrote this per-user;
; system-wide is both cleaner for a Program Files install and more reliable for
; the SSH/service context that actually reads it.
!define REG_ENV 'HKLM "System\CurrentControlSet\Control\Session Manager\Environment"'

Name "BinCue Studio"
BrandingText "BinCue Studio ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\BinCue Studio"
InstallDirRegKey HKLM "Software\BinCue Studio" "InstallDir"
RequestExecutionLevel admin

VIProductVersion "${VIVERSION}"
VIAddVersionKey "ProductName" "BinCue Studio"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "TheGameratorT"
VIAddVersionKey "LegalCopyright" "GNU General Public License v3"
VIAddVersionKey "FileDescription" "BinCue Studio Setup"
VIAddVersionKey "FileVersion" "${VERSION}"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "BinCue Studio" SecMain
    SetOutPath "$INSTDIR"
    ; The whole deployed tree: both exes, Qt/MinGW DLLs, cdrdao, libav*,
    ; plugin subdirs (platforms/, imageformats/, ...), notices and helpers.
    File /r "${DIST_DIR}\*"

    WriteRegExpandStr ${REG_ENV} "BINCUE_STUDIO_HOME" "$INSTDIR"
    SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000

    ; All-users shortcuts: both apps in the Start Menu, the studio on the desktop.
    SetShellVarContext all
    CreateDirectory "$SMPROGRAMS\BinCue Studio"
    CreateShortcut "$SMPROGRAMS\BinCue Studio\BinCue Studio.lnk" \
        "$INSTDIR\bincue-studio.exe" "" "$INSTDIR\bincue-studio.exe" 0 \
        SW_SHOWNORMAL "" "Assemble audio tracks, export BIN/CUE, and burn CDs"
    CreateShortcut "$SMPROGRAMS\BinCue Studio\CD Label Editor.lnk" \
        "$INSTDIR\cdlabel.exe" "" "$INSTDIR\cdlabel.exe" 0 \
        SW_SHOWNORMAL "" "Design printable CD labels"
    CreateShortcut "$DESKTOP\BinCue Studio.lnk" \
        "$INSTDIR\bincue-studio.exe" "" "$INSTDIR\bincue-studio.exe" 0 \
        SW_SHOWNORMAL "" "Assemble audio tracks, export BIN/CUE, and burn CDs"

    WriteRegStr HKLM "Software\BinCue Studio" "InstallDir" "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Add/Remove Programs entry.
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "BinCue Studio"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "TheGameratorT"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\bincue-studio.exe"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Uninstall"
    SetShellVarContext all
    Delete "$SMPROGRAMS\BinCue Studio\BinCue Studio.lnk"
    Delete "$SMPROGRAMS\BinCue Studio\CD Label Editor.lnk"
    RMDir "$SMPROGRAMS\BinCue Studio"
    Delete "$DESKTOP\BinCue Studio.lnk"

    DeleteRegValue ${REG_ENV} "BINCUE_STUDIO_HOME"
    SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000

    ; Run normally (no _?= switch), NSIS copies this uninstaller to $TEMP and
    ; executes from there, so RMDir /r removes $INSTDIR including the original
    ; Uninstall.exe.
    RMDir /r "$INSTDIR"

    DeleteRegKey HKLM "${UNINST_KEY}"
    DeleteRegKey HKLM "Software\BinCue Studio"
SectionEnd
