; ZuluGO installer -- side-by-side test build of the Zulu client on the
; GeneralsOnline codebase.
;
; Build with:  installer/build-zulugo-installer.sh
;          or: makensis /DPAYLOAD_DIR=<dir> installer/ZuluGO.nsi
; Output:      build/zulugo-installer/ZuluGO_Setup.exe
;
; This is deliberately NOT Zulu.nsi. Zulu.nsi installs the mod *into* the
; Zero Hour folder, overwrites files there, writes a firewall rule and owns
; the "Zulu" Add/Remove entry. This one is for handing a test build to
; someone who already plays on GeneralsOnline, so its one hard rule is:
; installing (and uninstalling) it must leave their working install exactly
; as it was.
;
; That rule is what dictates the layout. Everything lands in ONE subfolder:
;
;   <ZeroHour>\ZuluGO\ZuluGO.exe
;   <ZeroHour>\ZuluGO\ZuluGOLauncher.exe
;   <ZeroHour>\ZuluGO\*.dll
;   <ZeroHour>\ZuluGO\Zulu.big
;   <ZeroHour>\ZuluGO\cacert.pem
;   <ZeroHour>\ZuluGO\README.txt
;   <ZeroHour>\ZuluGO\Uninstall_ZuluGO.exe
;
; Nothing else in the game folder is touched -- in particular the player's
; GeneralsOnlineZH.exe and its DLLs are left alone, which is the whole
; difference between this and unzipping the package over the game folder as
; README.txt describes.
;
; Outside the game folder we write three things: the shortcuts, one
; Add/Remove Programs key of our own, and -- new in 2.0.0 -- one Windows
; Firewall inbound allow rule for ZuluGO.exe. That last one is a deliberate
; exception to what this header used to promise ("we write exactly two
; things"), and it is worth being blunt about: while the package is installed
; it now owns a piece of machine state outside the game folder. The
; uninstaller deletes the rule again, so the install is still reversible, but
; "leaves nothing behind" is no longer true for the duration. The trade is
; that without the rule the first inbound packet raises a consent prompt
; behind a fullscreen game, and dismissing that prompt leaves a BLOCK rule
; that nothing ever cleans up. See the firewall block in the Section.
;
; The shortcuts start ZuluGOLauncher.exe out of the subfolder, with NO
; arguments, and with the working directory set to the game folder. The
; working directory is load-bearing: the engine resolves the retail archives
; (INIZH.big, W3DZH.big, Maps\, Data\) against it rather than against the
; exe, and the launcher passes its own working directory on to the game.
;
; The launcher, not this installer, composes the game's command line -- in
; particular the -mod argument, which must be an ABSOLUTE path. It cannot be
; the relative "ZuluGO\Zulu.big" that the layout suggests: parseMod() in
; GeneralsMD\Code\GameEngine\Source\Common\CommandLine.cpp prefixes any
; argument without a drive letter or leading separator with the *user data*
; directory, not the working directory, so a relative argument would be
; looked for under Documents\...\Zero Hour Data\ZuluGO\Zulu.big and silently
; not found (parseMod returns without setting m_modBIG).
;
; The launcher also re-runs this installer to update in place, as
;   ZuluGO_Setup.exe /S /D=<ZeroHour folder>
; so every path through a silent run has to finish without a dialog: MUI
; skips the pages (and with them DirectoryLeave), and the two message boxes
; that survive into the Sections are guarded with IfSilent.

!ifndef APPVERSION
    !define APPVERSION "2.0.6"
!endif

!define APPNAME       "Zulu GO"
; The name the 0.x test drops used. Their Start Menu folder is swept on install
; so a tester does not end up with two entries pointing at the same game.
!define LEGACYAPPNAME "Zulu GO Test Build"
!define SHORTNAME     "ZuluGO"
!define SUBDIR        "ZuluGO"
!define EXENAME       "ZuluGO.exe"
!define LAUNCHERNAME  "ZuluGOLauncher.exe"
!ifndef BIGNAME
    ; Must match MOD_BIG_NAME in launcher/CMakeLists.txt: the launcher passes
    ; this name to the game, and the stock client's archive sweep is recursive,
    ; so a name outside its *.big mask is what keeps our data out of it.
    !define BIGNAME   "Zulu.modbig"
!endif
!define GOEXENAME     "GeneralsOnlineZH.exe"
!define UNINSTEXE     "Uninstall_ZuluGO.exe"

; The product name as a human (and netsh) sees it. Kept separate from
; APPNAME, which still carries the "Test Build" suffix that names the Start
; Menu folder and the Add/Remove entry: renaming those would orphan the
; shortcuts and the uninstall key of every existing install, while the
; firewall rule is new in 2.0.0 and can be named properly from the start.
!define PRODUCTNAME   "Zulu GO"
!define FWRULENAME    "${PRODUCTNAME} (${EXENAME})"

; Its own Add/Remove key, deliberately not "Zulu": a tester will usually have
; the real Zulu installed as well and the two must be removable independently.
!define UNINSTREGKEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${SHORTNAME}"
!define ZHREGKEY      "SOFTWARE\Electronic Arts\EA Games\Command and Conquer Generals Zero Hour"
!define DEFAULTINSTDIR "$PROGRAMFILES\EA Games\Command and Conquer Generals Zero Hour"

; There is deliberately no shortcut command line any more. Up to 1.x the
; shortcuts ran the game exe directly and this script built the
;   -mod "<ZeroHour>\ZuluGO\Zulu.big"
; argument at install time. The launcher owns that now: it knows where it
; was installed, so it can build the same absolute path itself, and it needs
; to keep ownership of the command line anyway so that a future build can
; change the game's arguments without every existing shortcut being stale.

; Where the staged payload sits. build-zulugo-installer.sh passes /D; the
; default matches where that script stages by default.
!ifndef PAYLOAD_DIR
    !define PAYLOAD_DIR "..\build\zulugo-installer\payload"
!endif
!ifndef OUTFILE
    !define OUTFILE "..\build\zulugo-installer\ZuluGO_Setup.exe"
!endif
; The launcher binary. Its own define rather than just a name under
; PAYLOAD_DIR because it is built by a separate toolchain from the client
; and does not have to be staged alongside it; build-zulugo-installer.sh
; does stage it, and passes the staged path here.
!ifndef LAUNCHER_SOURCE
    !define LAUNCHER_SOURCE "${PAYLOAD_DIR}\${LAUNCHERNAME}"
!endif

; Fail the build rather than ship a package missing a piece. A wildcard File
; that matches nothing is already a compile error, so the DLLs need no check.
!if /FileExists "${PAYLOAD_DIR}\${EXENAME}"
!else
    !error "payload is missing ${EXENAME}: ${PAYLOAD_DIR}"
!endif
; Checked by path, not by name under PAYLOAD_DIR, so an out-of-tree
; /DLAUNCHER_SOURCE is validated too. Shipping without the launcher would
; leave every shortcut pointing at a file that is not there.
!if /FileExists "${LAUNCHER_SOURCE}"
!else
    !error "launcher not found: ${LAUNCHER_SOURCE}"
!endif
!if /FileExists "${PAYLOAD_DIR}\${BIGNAME}"
!else
    !error "payload is missing ${BIGNAME}: ${PAYLOAD_DIR}"
!endif
!if /FileExists "${PAYLOAD_DIR}\cacert.pem"
!else
    !error "payload is missing cacert.pem: ${PAYLOAD_DIR}"
!endif
!if /FileExists "${PAYLOAD_DIR}\README.txt"
!else
    !error "payload is missing README.txt: ${PAYLOAD_DIR}"
!endif

; The two .wnd files that used to ship loose under Window\Menus are inside
; ${BIGNAME} now, and loose copies in the game folder would be read by the
; stock GeneralsOnline client as well as by ours -- exactly the kind of
; bleed-through this package exists to avoid.
;
; Nothing here can install them: the Section names every file it packs and
; uses a non-recursive *.dll wildcard, and `File /r` appears nowhere in this
; script, so a Window\ folder sitting in the payload is simply ignored.
; build-zulugo-installer.sh additionally refuses to stage one.
;
; There is deliberately no `!if /FileExists "${PAYLOAD_DIR}\Window"` guard
; here: on the POSIX makensis /FileExists matches neither directories nor
; nested paths, so such a check would silently never fire and read as
; protection that is not there.

Name        "${APPNAME}"
OutFile     "${OUTFILE}"
Unicode     true
SetCompressor /SOLID lzma

; Default to wherever this installer put things last time, then fall back to
; the literal below -- which .onInit replaces with the real Zero Hour folder
; (see DetectZeroHourDir). Note the value read back is GameDirectory, not
; InstallLocation: InstallLocation is the ZuluGO subfolder, which is what
; Add/Remove Programs should show, while $INSTDIR is its parent.
InstallDirRegKey HKLM "${UNINSTREGKEY}" "GameDirectory"
InstallDir       "${DEFAULTINSTDIR}"

; The game usually lives under Program Files, and the Add/Remove key is HKLM.
RequestExecutionLevel admin

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!insertmacro un.GetFileName

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "This installs the ${APPNAME} into its own folder inside your Command & Conquer: Generals Zero Hour installation.$\r$\n$\r$\nYour existing GeneralsOnline install is not modified: no file outside the new ZuluGO folder is written, renamed or replaced. Outside the game folder it adds shortcuts, an Add/Remove Programs entry, and a Windows Firewall rule allowing ${EXENAME} to receive connections so you can host and be spectated. Uninstalling removes all of them.$\r$\n$\r$\nYou need GeneralsOnline already installed -- this build runs alongside it, not instead of it."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose your Command & Conquer: Generals Zero Hour folder -- the one that holds GeneralsOnlineZH.exe. Nothing is written directly into it; the test build goes into a ${SUBDIR} subfolder of it."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "Zero Hour folder"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

!define VIVERSION "${APPVERSION}.0"
VIProductVersion "${VIVERSION}"
VIFileVersion    "${VIVERSION}"
VIAddVersionKey "ProductName"      "${APPNAME}"
VIAddVersionKey "ProductVersion"   "${APPVERSION}"
VIAddVersionKey "FileVersion"      "${APPVERSION}"
VIAddVersionKey "FileDescription"  "${APPNAME} installer for Command & Conquer: Generals Zero Hour"
VIAddVersionKey "CompanyName"      "Bill Rich"
VIAddVersionKey "LegalCopyright"   "Bill Rich"
VIAddVersionKey "OriginalFilename" "ZuluGO_Setup.exe"

; Authenticode signing, same contract as Zulu.nsi: the caller passes
; /DSIGN_CMD=<abs path to scripts/sign.sh> and the script decides for itself
; whether signing is configured. The uninstaller has to be signed here
; because NSIS only materialises it during this compile.
!ifdef SIGN_CMD
    !uninstfinalize '"${SIGN_CMD}" "%1"' = 0
    !finalize       '"${SIGN_CMD}" "%1"' = 0
!endif

; ---------------------------------------------------------------------------
; Finding the Zero Hour install
;
; Lifted from Zulu.nsi unchanged (IsZeroHourDir, ScanForZeroHour,
; TryCandidate, GetVdfLibraryPath, NormalizeSlashes, TrySteamLibraries,
; DetectZeroHourDir). Same problem, same answer: the registry key is stale or
; absent often enough that every guess has to be proved by looking for data
; files only a Zero Hour install has. Sources in order: our own uninstall
; key (InstallDirRegKey above), the Electronic Arts key, every Steam library
; from libraryfolders.vdf, then the historical fixed paths.
;
; Kept as a copy rather than factored into a shared .nsh: Zulu.nsi is the
; shipping installer and is not to be disturbed by this test-build tooling.
; ---------------------------------------------------------------------------

Var ZHDetectedDir

; $R0 = a directory -> $R0 = "1" when Zero Hour's data files are in it.
;
; INIZH.big sits in the install root on most SKUs and under Data\INI on the
; English/Chinese/Korean discs; W3DZH.big is present either way. None of them
; exist in a plain Generals (non-Zero-Hour) folder, so this will not match the
; wrong half of a two-game install -- which is why generals.exe is NOT tested
; here even though the folder has one. See LooksLikeZeroHour for the looser
; check used when warning the user.
Function IsZeroHourDir
    Push $R1
    StrCpy $R1 "0"
    ${If} ${FileExists} "$R0\INIZH.big"
    ${OrIf} ${FileExists} "$R0\Data\INI\INIZH.big"
    ${OrIf} ${FileExists} "$R0\W3DZH.big"
        StrCpy $R1 "1"
    ${EndIf}
    StrCpy $R0 $R1
    Pop $R1
FunctionEnd

; $R0 = directory to search, $R1 = how many levels below it to look.
; -> $R0 = "1" and $ZHDetectedDir set to the match, or $R0 = "0".
Function ScanForZeroHour
    Push $R2    ; remaining depth
    Push $R3    ; directory being searched
    Push $R4    ; FindFirst handle
    Push $R5    ; directory entry

    StrCpy $R3 $R0
    StrCpy $R2 $R1

    Call IsZeroHourDir
    StrCmp $R0 "1" 0 descend
        StrCpy $ZHDetectedDir $R3
        Goto found

descend:
    IntCmp $R2 0 not_found not_found 0
    FindFirst $R4 $R5 "$R3\*"
    StrCmp $R4 "" not_found

next_entry:
    StrCmp $R5 "" close_not_found
    StrCmp $R5 "." skip_entry
    StrCmp $R5 ".." skip_entry
    StrCpy $R0 "$R3\$R5"
    IntOp $R1 $R2 - 1
    Call ScanForZeroHour
    StrCmp $R0 "1" close_found
skip_entry:
    FindNext $R4 $R5
    Goto next_entry

close_found:
    FindClose $R4
    Goto found
close_not_found:
    FindClose $R4
not_found:
    StrCpy $R0 "0"
    Goto done
found:
    StrCpy $R0 "1"
done:
    Pop $R5
    Pop $R4
    Pop $R3
    Pop $R2
FunctionEnd

; $R0 = a path that came from the registry or a fixed guess. May be empty,
; may have a trailing separator, and may name the game exe instead of the
; folder holding it (InstallPath has historically been written both ways).
; -> $R0 = "1" and $ZHDetectedDir set when it pans out.
Function TryCandidate
    Push $R1
    StrCmp $R0 "" no

    StrCpy $R1 $R0 1 -1
    StrCmp $R1 "\" 0 not_trailing
        StrCpy $R0 $R0 -1
not_trailing:

    StrCpy $R1 $R0 4 -4
    StrCmp $R1 ".exe" 0 not_exe
        ${GetParent} "$R0" $R0
not_exe:

    StrCpy $R1 1
    Call ScanForZeroHour
    Goto done
no:
    StrCpy $R0 "0"
done:
    Pop $R1
FunctionEnd

; $R0 = one line of a libraryfolders.vdf
; -> $R0 = the library root it names, or "". Both client formats put the path
;    in the line's second quoted token, so take any second token that looks
;    like an absolute path; a false positive only costs one wasted scan.
Function GetVdfLibraryPath
    Push $R1    ; scan position
    Push $R2    ; character
    Push $R3    ; first quoted token
    Push $R4    ; second quoted token
    Push $R5    ; tokens completed so far
    Push $R6    ; 1 while inside a quoted token
    Push $R7    ; the line

    StrCpy $R7 $R0
    StrCpy $R1 0
    StrCpy $R3 ""
    StrCpy $R4 ""
    StrCpy $R5 0
    StrCpy $R6 0

scan:
    StrCpy $R2 $R7 1 $R1
    StrCmp $R2 "" scan_done
    IntOp $R1 $R1 + 1
    StrCmp $R2 '"' quote
    StrCmp $R6 "1" 0 scan
    StrCmp $R5 "0" 0 append_second
    StrCpy $R3 "$R3$R2"
    Goto scan
append_second:
    StrCpy $R4 "$R4$R2"
    Goto scan

quote:
    StrCmp $R6 "1" close_token
    StrCpy $R6 1
    Goto scan
close_token:
    StrCpy $R6 0
    IntOp $R5 $R5 + 1
    IntCmp $R5 2 scan_done
    Goto scan

scan_done:
    ; Absolute path test: second character is the drive colon.
    StrCpy $R2 $R4 1 1
    StrCmp $R2 ":" 0 nope
    StrCpy $R0 $R4
    Goto done
nope:
    StrCpy $R0 ""
done:
    Pop $R7
    Pop $R6
    Pop $R5
    Pop $R4
    Pop $R3
    Pop $R2
    Pop $R1
FunctionEnd

; $R0 = path -> $R0 with forward slashes turned into backslashes and runs of
; separators squeezed to one. Steam writes its own location with forward
; slashes under HKCU, and vdf escapes every backslash. A leading "\\" is left
; alone so a UNC path survives.
Function NormalizeSlashes
    Push $R1    ; scan position
    Push $R2    ; character
    Push $R3    ; previous character
    Push $R4    ; result
    Push $R5    ; input

    StrCpy $R5 $R0
    StrCpy $R1 0
    StrCpy $R3 ""
    StrCpy $R4 ""
loop:
    StrCpy $R2 $R5 1 $R1
    StrCmp $R2 "" done
    IntOp $R1 $R1 + 1
    StrCmp $R2 "/" 0 not_slash
        StrCpy $R2 "\"
not_slash:
    StrCmp $R2 "\" 0 keep
    IntCmp $R1 2 keep keep 0
    StrCmp $R3 "\" loop
keep:
    StrCpy $R4 "$R4$R2"
    StrCpy $R3 $R2
    Goto loop
done:
    StrCpy $R0 $R4
    Pop $R5
    Pop $R4
    Pop $R3
    Pop $R2
    Pop $R1
FunctionEnd

; Walk every Steam library and look for Zero Hour in it.
; -> $R0 = "1" and $ZHDetectedDir set on a hit.
Function TrySteamLibraries
    Push $R1    ; scratch / scan depth
    Push $R6    ; vdf file handle
    Push $R7    ; Steam root
    Push $R8    ; line buffer
    Push $R9    ; library root

    ; Where Steam itself is. The machine-wide key is written with
    ; backslashes; the per-user one uses forward slashes.
    SetRegView 32
    ReadRegStr $R7 HKLM "SOFTWARE\Valve\Steam" "InstallPath"
    StrCmp $R7 "" 0 have_steam
    ReadRegStr $R7 HKCU "Software\Valve\Steam" "SteamPath"
    StrCmp $R7 "" 0 have_steam
    SetRegView 64
    ReadRegStr $R7 HKLM "SOFTWARE\Valve\Steam" "InstallPath"
    SetRegView Default
    StrCmp $R7 "" no_steam

have_steam:
    SetRegView Default
    StrCpy $R0 $R7
    Call NormalizeSlashes
    StrCpy $R7 $R0

    ; The library inside the Steam folder itself. Depth 2: the game is a
    ; folder under common\, or a folder under a collection folder there.
    StrCpy $R0 "$R7\steamapps\common"
    StrCpy $R1 2
    Call ScanForZeroHour
    StrCmp $R0 "1" done

    ; Other drives. Current clients keep the list under steamapps\, clients
    ; older than 2021 under config\.
    ClearErrors
    FileOpen $R6 "$R7\steamapps\libraryfolders.vdf" r
    IfErrors 0 read_line
    ClearErrors
    FileOpen $R6 "$R7\config\libraryfolders.vdf" r
    IfErrors no_steam

read_line:
    ClearErrors
    FileRead $R6 $R8
    IfErrors close_no

    StrCpy $R0 $R8
    Call GetVdfLibraryPath
    StrCmp $R0 "" read_line
    Call NormalizeSlashes
    StrCpy $R9 $R0

    StrCpy $R0 "$R9\steamapps\common"
    StrCpy $R1 2
    Call ScanForZeroHour
    StrCmp $R0 "1" close_yes
    Goto read_line

close_yes:
    FileClose $R6
    Goto done
close_no:
    FileClose $R6
no_steam:
    StrCpy $R0 "0"
done:
    SetRegView Default
    Pop $R9
    Pop $R8
    Pop $R7
    Pop $R6
    Pop $R1
FunctionEnd

; -> $ZHDetectedDir = the Zero Hour folder, or "" when nothing was found.
Function DetectZeroHourDir
    Push $R0
    StrCpy $ZHDetectedDir ""

    ; The key retail, Origin and the EA App write. Read it in both registry
    ; views: we are a 32-bit installer, so plain reads see WOW6432Node only,
    ; and not every store writes there.
    SetRegView 32
    ReadRegStr $R0 HKLM "${ZHREGKEY}" "InstallPath"
    Call TryCandidate
    StrCmp $R0 "1" done
    ReadRegStr $R0 HKCU "${ZHREGKEY}" "InstallPath"
    Call TryCandidate
    StrCmp $R0 "1" done
    SetRegView 64
    ReadRegStr $R0 HKLM "${ZHREGKEY}" "InstallPath"
    SetRegView Default
    Call TryCandidate
    StrCmp $R0 "1" done
    SetRegView 64
    ReadRegStr $R0 HKCU "${ZHREGKEY}" "InstallPath"
    SetRegView Default
    Call TryCandidate
    StrCmp $R0 "1" done

    Call TrySteamLibraries
    StrCmp $R0 "1" done

    ; Fixed paths, for an install whose registry entry never got written.
    StrCpy $R0 "$PROGRAMFILES\EA Games\Command and Conquer Generals Zero Hour"
    Call TryCandidate
    StrCmp $R0 "1" done
    StrCpy $R0 "$PROGRAMFILES64\EA Games\Command and Conquer Generals Zero Hour"
    Call TryCandidate
    StrCmp $R0 "1" done
    StrCpy $R0 "$PROGRAMFILES\Origin Games\Command and Conquer Generals Zero Hour"
    Call TryCandidate
    StrCmp $R0 "1" done
    StrCpy $R0 "$PROGRAMFILES64\Origin Games\Command and Conquer Generals Zero Hour"
    Call TryCandidate

done:
    SetRegView Default
    Pop $R0
FunctionEnd

; ---------------------------------------------------------------------------
; Validation of whatever folder we end up with
; ---------------------------------------------------------------------------

; $R0 = a directory -> $R0 = "1" when it plausibly holds Zero Hour. Looser
; than IsZeroHourDir on purpose: it also accepts generals.exe, so a user with
; an unusual SKU whose archive names we do not recognise is still allowed
; through without a scare. Only used for the warning, never for detection.
Function LooksLikeZeroHour
    Push $R1
    Call IsZeroHourDir
    StrCpy $R1 $R0
    StrCmp $R1 "1" done
    ${If} ${FileExists} "$INSTDIR\generals.exe"
        StrCpy $R1 "1"
    ${EndIf}
done:
    StrCpy $R0 $R1
    Pop $R1
FunctionEnd

Function .onInit
    ; $INSTDIR is already filled in: from InstallDirRegKey when this installer
    ; has run on this machine before, from /D= when one was passed, otherwise
    ; from the InstallDir literal. Only that last case is a guess.
    StrCmp $INSTDIR "${DEFAULTINSTDIR}" 0 keep
    Call DetectZeroHourDir
    StrCmp $ZHDetectedDir "" keep
    StrCpy $INSTDIR $ZHDetectedDir
keep:
FunctionEnd

; Both checks the brief asks for, run when the user leaves the directory page.
; Warn and require an explicit "yes" rather than blocking outright -- an
; unrecognised SKU layout is possible and the user knows where their game is
; -- but never continue silently.
;
; Neither box can stall the launcher's `/S /D=<ZeroHour folder>` update:
; NSIS shows no pages at all under /S, so this leave callback is never
; reached. The IfSilent below is belt and braces for the day someone calls
; this from somewhere other than the page -- the warnings are advisory, and
; an unattended run must never sit on a modal nobody is watching. The Section
; re-runs both checks and prints them to the install log instead.
Function DirectoryLeave
    Push $R0
    IfSilent ok

    StrCpy $R0 $INSTDIR
    Call LooksLikeZeroHour
    StrCmp $R0 "1" check_go
    MessageBox MB_YESNO|MB_ICONEXCLAMATION \
        "$INSTDIR does not look like a Command & Conquer: Generals Zero Hour installation.$\r$\n$\r$\n\
${APPNAME} is a mod. It loads the game's data from the folder its shortcut starts in, so installed anywhere else it will not start.$\r$\n$\r$\n\
Choose the folder holding your Zero Hour files (generals.exe, INIZH.big, Data, Maps). A Steam copy is usually under Steam\steamapps\common, not Program Files.$\r$\n$\r$\n\
Install here anyway?" \
        IDYES check_go
    Pop $R0
    Abort

check_go:
    ${If} ${FileExists} "$INSTDIR\${GOEXENAME}"
        Goto ok
    ${EndIf}
    MessageBox MB_YESNO|MB_ICONEXCLAMATION \
        "${GOEXENAME} is not in $INSTDIR.$\r$\n$\r$\n\
This is a test build of the GeneralsOnline client and expects a working GeneralsOnline install to sit alongside it in the same folder. Without one you will almost certainly not get online, and some of the support DLLs this package relies on are the ones GeneralsOnline installs.$\r$\n$\r$\n\
Install GeneralsOnline first, then run this again.$\r$\n$\r$\n\
Install here anyway?" \
        IDYES ok
    Pop $R0
    Abort

ok:
    Pop $R0
FunctionEnd

; ---------------------------------------------------------------------------

Section "${APPNAME}" SecMain
    SectionIn RO

    ; A silent install never saw the directory page. Say so in the log rather
    ; than laying files down without a word; /S is a deliberate choice, so it
    ; is not turned into an abort.
    Push $R0
    StrCpy $R0 $INSTDIR
    Call LooksLikeZeroHour
    ${If} $R0 != "1"
        DetailPrint "WARNING: $INSTDIR holds no Zero Hour files (generals.exe / INIZH.big / W3DZH.big)."
        DetailPrint "The shortcuts start the game with $INSTDIR as the working directory, so it will not start from here."
    ${EndIf}
    ${IfNot} ${FileExists} "$INSTDIR\${GOEXENAME}"
        DetailPrint "WARNING: ${GOEXENAME} is not in $INSTDIR."
        DetailPrint "This build expects a GeneralsOnline install alongside it."
    ${EndIf}
    Pop $R0

    ; --- Payload -> <ZeroHour>\ZuluGO\ ----------------------------------
    ; Everything, without exception, goes in here. Named files plus a
    ; non-recursive *.dll wildcard: the wildcard means a new support DLL in a
    ; later build needs no edit here, and being non-recursive it cannot drag
    ; in the Window\Menus\*.wnd files that used to ship loose (they are inside
    ; ${BIGNAME} now). There is no compile-time check for that folder and
    ; there cannot usefully be one -- see the note at the top of this file --
    ; so the wildcard's non-recursiveness is the actual guarantee.
    ;
    ; SetOverwrite on applies only inside this subfolder, which is ours: on a
    ; reinstall of a freshly built client the point is to replace the old
    ; files. No File command in this script ever targets $INSTDIR itself.
    SetOutPath "$INSTDIR\${SUBDIR}"
    SetOverwrite on
    File "${PAYLOAD_DIR}\${EXENAME}"
    File "${PAYLOAD_DIR}\*.dll"
    File "${PAYLOAD_DIR}\${BIGNAME}"
    File "${PAYLOAD_DIR}\cacert.pem"
    File "${PAYLOAD_DIR}\README.txt"

    ; --- The launcher, and the dance needed to overwrite it --------------
    ; Same problem Zulu.nsi solves, in the subfolder instead of the install
    ; root. When this runs as an update the launcher is RUNNING: it is what
    ; downloaded us and started us with /S, and it waits for us to exit so it
    ; can start the game. Windows holds an exclusive image lock on a running
    ; exe, so the write over it fails -- and a silent installer swallows that
    ; failure and still exits 0, so the launcher reports a successful update
    ; and keeps running the old build forever. Every launcher fix would be
    ; invisible to exactly the people who update through the launcher.
    ;
    ; A running exe can still be RENAMED, so move it aside and let the write
    ; land on a free name. The leftover is deleted below when nothing holds
    ; it (a fresh install, or one run by hand with no launcher running); when
    ; the launcher is running it survives until the Delete at the top of the
    ; next update, because the new launcher's own sweep cannot delete out of
    ; Program Files without elevation. That is one small file between
    ; updates, and ".old" is not an executable extension, so nothing will
    ; ever try to run it.
    Delete "$INSTDIR\${SUBDIR}\${LAUNCHERNAME}.old"
    Rename "$INSTDIR\${SUBDIR}\${LAUNCHERNAME}" "$INSTDIR\${SUBDIR}\${LAUNCHERNAME}.old"
    ; 'try' rather than the default 'on': a failed write has to set the error
    ; flag and carry on so the restore below can run, not abort the section
    ; and leave the shortcuts pointing at a name with no file behind it.
    SetOverwrite try
    ClearErrors
    File "/oname=${LAUNCHERNAME}" "${LAUNCHER_SOURCE}"
    SetOverwrite on
    IfErrors 0 +2
        Rename "$INSTDIR\${SUBDIR}\${LAUNCHERNAME}.old" "$INSTDIR\${SUBDIR}\${LAUNCHERNAME}"
    Delete "$INSTDIR\${SUBDIR}\${LAUNCHERNAME}.old"

    ; --- Shortcuts -------------------------------------------------------
    ; SetOutPath is doing real work here, not just choosing a directory:
    ; CreateShortcut has no working-directory parameter, it takes whatever
    ; SetOutPath last named. That has to be $INSTDIR (the Zero Hour folder)
    ; and NOT the ZuluGO subfolder, because the engine resolves INIZH.big,
    ; W3DZH.big, Maps\ and Data\ against the working directory. Started from
    ; the subfolder the game finds no retail data and dies on startup. The
    ; launcher is started with that same working directory and hands it to
    ; the game, so this is unchanged from when the shortcut ran the game.
    ;
    ; The target is the launcher and the argument list is EMPTY: the launcher
    ; builds the game's command line itself (see the -mod note in the header).
    ; The icon is taken from the game exe, which carries the GeneralsZulu
    ; insignia rather than the retail Generals icon (ZULU_APP_ICON_FILE in
    ; GeneralsMD\Code\Main\CMakeLists.txt). That is the whole point of the
    ; separate icon: this shortcut sits on a desktop that usually already has
    ; a stock GeneralsOnline one, and two identical icons is how a tester ends
    ; up reporting a bug against the wrong client.
    ;
    ; All-users context: the game is usually under Program Files and we are
    ; elevated, so every account on the box should see the shortcuts. This
    ; block runs on every install, silent ones included, so an update always
    ; refreshes them.
    SetShellVarContext all
    SetOutPath "$INSTDIR"

    ; Sweep the 0.x test drops' Start Menu folder and desktop shortcut, so a
    ; tester upgrading to 2.0 is left with one entry rather than two pointing
    ; at the same install.
    RMDir /r "$SMPROGRAMS\${LEGACYAPPNAME}"
    Delete "$DESKTOP\${LEGACYAPPNAME}.lnk"

    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" \
        "$INSTDIR\${SUBDIR}\${LAUNCHERNAME}" \
        "" \
        "$INSTDIR\${SUBDIR}\${EXENAME}" 0
    CreateShortcut "$SMPROGRAMS\${APPNAME}\README.lnk" \
        "$INSTDIR\${SUBDIR}\README.txt"
    CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" \
        "$INSTDIR\${SUBDIR}\${UNINSTEXE}"

    SetShellVarContext current

    ; --- Uninstaller + Add/Remove Programs registration ------------------
    ; The uninstaller lives inside the subfolder it removes. NSIS uninstallers
    ; copy themselves to %TEMP% and re-run from there, so deleting the folder
    ; out from under it is fine.
    WriteUninstaller "$INSTDIR\${SUBDIR}\${UNINSTEXE}"
    WriteRegStr HKLM "${UNINSTREGKEY}" "DisplayName"     "${APPNAME}"
    WriteRegStr HKLM "${UNINSTREGKEY}" "DisplayVersion"  "${APPVERSION}"
    WriteRegStr HKLM "${UNINSTREGKEY}" "DisplayIcon"     "$INSTDIR\${SUBDIR}\${EXENAME}"
    WriteRegStr HKLM "${UNINSTREGKEY}" "InstallLocation" "$INSTDIR\${SUBDIR}"
    ; The Zero Hour folder, kept separately so a reinstall defaults to the
    ; same game rather than to the subfolder (see InstallDirRegKey).
    WriteRegStr HKLM "${UNINSTREGKEY}" "GameDirectory"   "$INSTDIR"
    WriteRegStr HKLM "${UNINSTREGKEY}" "UninstallString" '"$INSTDIR\${SUBDIR}\${UNINSTEXE}"'
    WriteRegStr HKLM "${UNINSTREGKEY}" "Publisher"       "Bill Rich"
    WriteRegDWORD HKLM "${UNINSTREGKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTREGKEY}" "NoRepair" 1

    DetailPrint "Installed to $INSTDIR\${SUBDIR}"
    DetailPrint "Shortcut runs: $INSTDIR\${SUBDIR}\${LAUNCHERNAME} (no arguments)"
    DetailPrint "Working directory: $INSTDIR"

    ; --- Windows Firewall allow rule -------------------------------------
    ; Zulu.nsi's block, with the program path pointed at the subfolder. The
    ; earlier ZuluGO packages registered no rule on the grounds that netsh
    ; rules are machine state we did not want to own; the header comment
    ; above now says plainly that we do own one, and why. Short version:
    ; without a rule the inbound UDP lobby traffic (8086/8088) and the TCP
    ; 8188 observer stream are silently dropped whenever the first-listen
    ; consent prompt goes unanswered behind the fullscreen game -- and a
    ; dismissed prompt leaves a BLOCK rule that outlives the session.
    ;
    ; Note the rule is keyed on OUR exe inside the subfolder, so it cannot
    ; affect the player's GeneralsOnlineZH.exe next door.
    ;
    ; Two deletes before the add: the name-based one catches our own rule
    ; from a prior install at a different path; the program-based one wipes
    ; every other inbound rule for this exe, in particular the block rules
    ; Windows auto-creates when the consent prompt is dismissed (block
    ; overrides allow, so a stale one would defeat the rule we add).
    ;
    ; Both deletes are allowed to fail -- nothing to delete is the normal
    ; first install. The add is not: throwing its result away is how you end
    ; up with an install that reports success and silently cannot host on an
    ; unelevated run, under a policy that blocks netsh, or on a machine where
    ; a third-party firewall owns the stack. Verify by asking for the rule
    ; back rather than trusting the add's exit code, since the rule's actual
    ; presence is what decides whether inbound traffic arrives.
    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${FWRULENAME}"'
    Pop $0
    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name=all dir=in program="$INSTDIR\${SUBDIR}\${EXENAME}"'
    Pop $0
    nsExec::ExecToLog 'netsh advfirewall firewall add rule name="${FWRULENAME}" dir=in action=allow program="$INSTDIR\${SUBDIR}\${EXENAME}" enable=yes profile=any'
    Pop $0
    DetailPrint "Firewall rule add returned: $0"

    ; show rule exits non-zero when no rule matches the name.
    nsExec::ExecToLog 'netsh advfirewall firewall show rule name="${FWRULENAME}" dir=in'
    Pop $1
    DetailPrint "Firewall rule verification returned: $1"

    ${If} $1 != 0
        DetailPrint "WARNING: no inbound firewall rule for $INSTDIR\${SUBDIR}\${EXENAME}."
        DetailPrint "Hosting a LAN game and being spectated will not work until one exists."
        ; Silent installs come from the launcher's update flow, which runs
        ; unattended: a modal box there would hang the update behind a dialog
        ; nobody is watching. The detail lines above still land in the
        ; install log either way.
        IfSilent +2
        MessageBox MB_OK|MB_ICONEXCLAMATION "Windows Firewall could not be configured for ${PRODUCTNAME}.$\r$\n$\r$\nOther players may be unable to join or spectate games you host. To fix this manually, allow inbound connections for:$\r$\n$INSTDIR\${SUBDIR}\${EXENAME}$\r$\n$\r$\nRunning this installer as an administrator usually resolves it."
    ${EndIf}

    ; Deliberately no Exec of the launcher at the end of a silent run. The
    ; launcher that started us is still alive, waiting on our exit so it can
    ; start the game with the user's own argv; starting a second one here
    ; would race it and would only know the shortcut's arguments.
SectionEnd

Section "Desktop shortcut" SecDesktop
    ; Same working-directory rule as above; the section boundary resets
    ; nothing, but SetOutPath is repeated so this does not depend on the
    ; order the sections happen to run in. Same target too: the launcher,
    ; no arguments, game exe for the icon -- the insignia, not the retail
    ; Generals icon, so this is the one on the desktop you can pick out.
    ;
    ; Selected by default, and a silent install runs every selected section,
    ; so the launcher's update re-writes this shortcut as well.
    SetShellVarContext all
    SetOutPath "$INSTDIR"
    CreateShortcut "$DESKTOP\${APPNAME}.lnk" \
        "$INSTDIR\${SUBDIR}\${LAUNCHERNAME}" \
        "" \
        "$INSTDIR\${SUBDIR}\${EXENAME}" 0
    SetShellVarContext current
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The test build and its launcher, in their own ${SUBDIR} folder inside your Zero Hour install. No file outside that folder is changed; a firewall rule for ${EXENAME} is added so you can host, and removed again on uninstall."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Also put a shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------------------

Section "Uninstall"
    ; $INSTDIR here is where the uninstaller was written -- the ZuluGO
    ; subfolder, not the game folder. RMDir /r on it is only safe while that
    ; is true, and it is exactly the kind of thing that silently stops being
    ; true after an edit, so prove it before deleting anything. Getting this
    ; wrong would wipe the user's Zero Hour install.
    ${un.GetFileName} "$INSTDIR" $R0
    ${If} $R0 != "${SUBDIR}"
        ; The refusal stands either way; only the dialog is conditional, so
        ; a silent uninstall aborts instead of waiting on a box nobody sees.
        IfSilent +2
        MessageBox MB_OK|MB_ICONSTOP \
            "Refusing to uninstall: $INSTDIR is not a ${SUBDIR} folder.$\r$\n$\r$\n\
Delete $INSTDIR by hand if you are sure it is the ${APPNAME} folder."
        Abort
    ${EndIf}

    ; Everything in here is ours and was put there by this installer --
    ; including any ${LAUNCHERNAME}.old an update left behind.
    RMDir /r "$INSTDIR"

    ; Shortcuts are created all-users; sweep the per-user location too in case
    ; an install run without elevation put them there.
    SetShellVarContext all
    Delete "$DESKTOP\${APPNAME}.lnk"
    RMDir /r "$SMPROGRAMS\${APPNAME}"
    SetShellVarContext current
    Delete "$DESKTOP\${APPNAME}.lnk"
    RMDir /r "$SMPROGRAMS\${APPNAME}"

    DeleteRegKey HKLM "${UNINSTREGKEY}"

    ; Remove the firewall rule the installer registered. By name only: a
    ; program-scoped delete would also take out rules for that path that
    ; somebody else created, and the exe is about to be gone anyway.
    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${FWRULENAME}"'

    ; Nothing else. In particular: no RMDir on the parent Zero Hour folder,
    ; no files deleted from it, no rule touched but our own, and nothing
    ; touched under Documents -- the GeneralsOnline user data, replays, maps
    ; and saves there are not ours.
SectionEnd
