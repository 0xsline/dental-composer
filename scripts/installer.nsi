; 牙片拼图 Win7 安装包（NSIS 3.x，脚本需 UTF-8 BOM 编码）
; 用法：先跑 scripts\build-windows.ps1，再在 scripts 目录执行
;   makensis /DAPP_DIR=..\build-win\Release installer.nsi

!include "LogicLib.nsh"

!ifndef APP_DIR
  !define APP_DIR "..\build-win\Release"
!endif

Name "牙片拼图"
Caption "牙片拼图 安装"
OutFile "DentalComposer-0.1.0-Win7-x64-setup.exe"
InstallDir "$PROGRAMFILES64\DentalComposer"
RequestExecutionLevel admin
Unicode true
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  ; 检测程序是否正在运行（文件被占用会导致覆盖失败）
  nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq dental-composer.exe" /NH'
  Pop $0
  Pop $1
  ${If} $1 != ""
    StrCpy $2 $1 4
    ${If} $2 != "INFO"
      MessageBox MB_ICONEXCLAMATION|MB_OK "检测到牙片拼图正在运行。请先关闭程序（任务栏右键关闭，或任务管理器结束 dental-composer.exe），再重新运行安装包。"
      Abort
    ${EndIf}
  ${EndIf}

  ; 清除旧文件只读属性，避免覆盖失败
  ${If} ${FileExists} "$INSTDIR\dental-composer.exe"
    SetFileAttributes "$INSTDIR\*.*" NORMAL
    SetFileAttributes "$INSTDIR\platforms\*.*" NORMAL
    SetFileAttributes "$INSTDIR\imageformats\*.*" NORMAL
    SetFileAttributes "$INSTDIR\iconengines\*.*" NORMAL
    SetFileAttributes "$INSTDIR\styles\*.*" NORMAL
  ${EndIf}

  SetOutPath "$INSTDIR"
  File /r "${APP_DIR}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\牙片拼图.lnk" "$INSTDIR\dental-composer.exe"
  CreateDirectory "$SMPROGRAMS\牙片拼图"
  CreateShortCut "$SMPROGRAMS\牙片拼图\牙片拼图.lnk" "$INSTDIR\dental-composer.exe"
  CreateShortCut "$SMPROGRAMS\牙片拼图\卸载.lnk" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer" "DisplayName" "牙片拼图"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer" "DisplayVersion" "0.1.3"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer" "Publisher" "晨旭口腔"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\牙片拼图.lnk"
  RMDir /r "$SMPROGRAMS\牙片拼图"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DentalComposer"
SectionEnd
