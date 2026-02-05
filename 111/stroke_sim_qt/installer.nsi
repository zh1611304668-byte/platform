!define APP_NAME "Stroke Simulator Qt"
!define APP_EXE "stroke_sim_qt.exe"
!define APP_DIR "..\\..\\build\\stroke_sim_qt_mingw_qt\\dist"
!define APP_VERSION "1.0.0"
!define COMPANY_NAME "Lenovo"
!define APP_ICON "..\\..\\tools\\stroke_sim_qt\\assets\\app_icon.ico"

OutFile "..\\..\\build\\stroke_sim_qt_mingw_qt\\StrokeSimSetup.exe"
InstallDir "$PROGRAMFILES64\\StrokeSimulatorQt"
RequestExecutionLevel admin

Icon "${APP_ICON}"
UninstallIcon "${APP_ICON}"

VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "CompanyName" "${COMPANY_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "${COMPANY_NAME}"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${APP_DIR}\\*"

  CreateDirectory "$SMPROGRAMS\\Stroke Simulator Qt"
  CreateShortcut "$SMPROGRAMS\\Stroke Simulator Qt\\Stroke Simulator Qt.lnk" "$INSTDIR\\${APP_EXE}" "" "$INSTDIR\\app_icon.ico"
  CreateShortcut "$DESKTOP\\Stroke Simulator Qt.lnk" "$INSTDIR\\${APP_EXE}" "" "$INSTDIR\\app_icon.ico"

  WriteUninstaller "$INSTDIR\\Uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\\Stroke Simulator Qt.lnk"
  Delete "$SMPROGRAMS\\Stroke Simulator Qt\\Stroke Simulator Qt.lnk"
  RMDir "$SMPROGRAMS\\Stroke Simulator Qt"
  Delete "$INSTDIR\\Uninstall.exe"
  RMDir /r "$INSTDIR"
SectionEnd
