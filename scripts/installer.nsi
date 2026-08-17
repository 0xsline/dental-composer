; 牙片拼图 Win7 安装包（NSIS 3.x）
; 用法：先跑 scripts\build-windows.ps1，再 makensis /DAPP_DIR=..\build-win\Release scripts\installer.nsi

!ifndef APP_DIR
  !define APP_DIR "..\build-win\Release"
!endif

Name "牙片拼图"
Caption "牙片拼图 安装"
OutFile "牙片拼图-0.1.0-Win7-x64-setup.exe"
InstallDir "$PROGRAMFILES64\DentalComposer"
RequestExecutionLevel admin
Unicode true
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${APP_DIR}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\牙片拼图.lnk" "$INSTDIR\dental-composer.exe"
  CreateDirectory "$SMPROGRAMS\牙片拼图"
  CreateShortCut "$SMPROGRAMS\牙片拼图\牙片拼图.lnk" "$INSTDIR\dental-composer.exe"
  CreateShortCut "$SMPROGRAMS\牙片拼图\卸载.lnk" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer" "DisplayName" "牙片拼图"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer" "DisplayVersion" "0.1.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer" "Publisher" "晨旭口腔"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\牙片拼图.lnk"
  RMDir /r "$SMPROGRAMS\牙片拼图"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer"
SectionEnd
