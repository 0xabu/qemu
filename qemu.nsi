;!/usr/bin/makensis

; This NSIS script creates an installer for QEMU on Windows.

; Copyright (C) 2006-2011 Stefan Weil
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 2 of the License, or
; (at your option) version 3 or any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.

; NSIS_WIN32_MAKENSIS

!define PRODUCT "QEMU"
!define URL     "http://www.qemu.org/"

!define UNINST_EXE "$INSTDIR\qemu-uninstall.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

!ifndef BINDIR
!define BINDIR ../bin/win/nsis.tmp
!endif
!ifndef SRCDIR
!define SRCDIR .
!endif
!ifndef OUTFILE
!define OUTFILE "qemu-setup.exe"
!endif

; Optionally install documentation.
!ifndef CONFIG_DOCUMENTATION
!define CONFIG_DOCUMENTATION
!endif

!include "MUI2.nsh"

; The name of the installer.
Name "QEMU"

; The file to write
OutFile "${OUTFILE}"

; The default installation directory.
InstallDir $PROGRAMFILES\qemu

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\qemu" "Install_Dir"

; Request administrator privileges for Windows Vista.
RequestExecutionLevel admin

;--------------------------------
; Interface Settings.
; !define MUI_HEADERIMAGE "qemu-install.bmp"
; !define MUI_SPECIALBITMAP "qemu.bmp"
!define MUI_ICON "${SRCDIR}\pc-bios\qemu-icon.ico"
!define MUI_UNICON "${SRCDIR}\pc-bios\qemu-icon.ico"
; !define MUI_WELCOMEFINISHPAGE_BITMAP "qemu.bmp"
; !define MUI_HEADERIMAGE_BITMAP "qemu-install.bmp"
; !define MUI_HEADERIMAGE_UNBITMAP "qemu-uninstall.bmp"
; !define MUI_COMPONENTSPAGE_SMALLDESC
; !define MUI_WELCOMEPAGE_TEXT "Insert text here.$\r$\n$\r$\n$\r$\n$_CLICK"

;--------------------------------
; Pages.

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\COPYING"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_LINK "Visit the QEMU Wiki online!"
!define MUI_FINISHPAGE_LINK_LOCATION "${URL}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages.

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "German"

;--------------------------------

; The stuff to install.
Section "${PRODUCT} (required)"

    SectionIn RO

    ; Set output path to the installation directory.
    SetOutPath "$INSTDIR"

    File "${SRCDIR}\Changelog"
    File "${SRCDIR}\COPYING"
    File "${SRCDIR}\COPYING.LIB"
    File "${SRCDIR}\README"
    File "${SRCDIR}\VERSION"

    File "${BINDIR}\*.bmp"
    File "${BINDIR}\*.bin"
    File "${BINDIR}\*.dll"
    File "${BINDIR}\*.dtb"
    File "${BINDIR}\*.rom"
    File "${BINDIR}\openbios-*"

    File /r "${BINDIR}\keymaps"
    File /r "${BINDIR}\qemu"

    ; Write the installation path into the registry
    WriteRegStr HKLM SOFTWARE\${PRODUCT} "Install_Dir" "$INSTDIR"

    ; Write the uninstall keys for Windows
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "QEMU"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"${UNINST_EXE}"'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    WriteUninstaller "qemu-uninstall.exe"
SectionEnd

Section "Tools" SectionTools
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu-img.exe"
    File "${BINDIR}\qemu-io.exe"
SectionEnd

Section "PC (i386) System Emulation" SectionQemu
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu.exe"
SectionEnd

Section "Other System Emulations" SectionOther
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu-system-*.exe"
SectionEnd

!ifdef CONFIG_DOCUMENTATION
Section "Documentation" SectionDoc
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu-doc.html"
    File "${BINDIR}\qemu-tech.html"
    CreateDirectory "$SMPROGRAMS\${PRODUCT}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\User Documentation.lnk" "$INSTDIR\qemu-doc.html" "" "$INSTDIR\qemu-doc.html" 0
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\Technical Documentation.lnk" "$INSTDIR\qemu-tech.html" "" "$INSTDIR\qemu-tech.html" 0
SectionEnd
!endif

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts" SectionMenu
    CreateDirectory "$SMPROGRAMS\${PRODUCT}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\Uninstall.lnk" "${UNINST_EXE}" "" "${UNINST_EXE}" 0
SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
    ; Remove registry keys
    DeleteRegKey HKLM "${UNINST_KEY}"
    DeleteRegKey HKLM SOFTWARE\${PRODUCT}

    ; Remove shortcuts, if any
    Delete "$SMPROGRAMS\${PRODUCT}\User Documentation.lnk"
    Delete "$SMPROGRAMS\${PRODUCT}\Technical Documentation.lnk"
    Delete "$SMPROGRAMS\${PRODUCT}\Uninstall.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT}"

    ; Remove files and directories used
    Delete "$INSTDIR\Changelog"
    Delete "$INSTDIR\COPYING"
    Delete "$INSTDIR\COPYING.LIB"
    Delete "$INSTDIR\README"
    Delete "$INSTDIR\VERSION"
    Delete "$INSTDIR\*.bmp"
    Delete "$INSTDIR\*.bin"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\*.dtb"
    Delete "$INSTDIR\*.rom"
    Delete "$INSTDIR\openbios-*"
    Delete "$INSTDIR\qemu-img.exe"
    Delete "$INSTDIR\qemu-io.exe"
    Delete "$INSTDIR\qemu.exe"
    Delete "$INSTDIR\qemu-system-*.exe"
    Delete "$INSTDIR\qemu-doc.html"
    Delete "$INSTDIR\qemu-tech.html"
    RMDir /r "$INSTDIR\keymaps"
    RMDir /r "$INSTDIR\qemu"
    ; Remove uninstaller
    Delete "${UNINST_EXE}"
    RMDir "$INSTDIR"
SectionEnd

;--------------------------------

; Descriptions (mouse-over).
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionQemu}  "PC system emulation (i386)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionOther} "Additional system emulations."
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionTools} "Tools."
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionDoc}   "Documentation."
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionMenu}  "Menu entries."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Functions.

Function .onInit
    !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

