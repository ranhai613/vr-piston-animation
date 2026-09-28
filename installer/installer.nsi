Unicode true

!include "MUI2.nsh"

; makensis starts in the script directory by default. Move to the project root.
!cd ..

!ifndef APP_BUILD_DIR
    !define APP_BUILD_DIR "build\Release"
!endif
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\VRPistonAnimation"

Name "VR Piston Animation"
OutFile "build\VR-Piston-Animation-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\VR Piston Animation"
InstallDirRegKey HKCU "Software\VRPistonAnimation" "InstallDir"
RequestExecutionLevel user

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"
    File "${APP_BUILD_DIR}\vr-piston-animation.exe"
    File "${APP_BUILD_DIR}\openvr_api.dll"
    File "LICENSE"
    File "steamvr\manifest.vrmanifest"
    File "third-party\openvr\LICENSE-VALVE"
    File "third-party\nlhomann\LICENSE-MIT"

    ExecWait '"$INSTDIR\vr-piston-animation.exe" --install-steamvr' $0
    StrCmp $0 "0" registration_done
    MessageBox MB_ICONEXCLAMATION|MB_OK \
        "The app was installed, but SteamVR registration or startup activation failed. Start SteamVR and enable the app under Settings > Startup / Shutdown > Choose startup overlay apps. If it is missing, restart SteamVR and run the installer again."
registration_done:

    CreateDirectory "$SMPROGRAMS\VR Piston Animation"
    CreateShortcut "$SMPROGRAMS\VR Piston Animation\VR Piston Animation.lnk" \
        "$INSTDIR\vr-piston-animation.exe"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "Software\VRPistonAnimation" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "VR Piston Animation"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
    ExecWait '"$INSTDIR\vr-piston-animation.exe" --uninstall-steamvr' $0
    StrCmp $0 "0" unregistration_done
    MessageBox MB_ICONEXCLAMATION|MB_OK \
        "SteamVR registration could not be removed. The app files will still be uninstalled; the SteamVR entry may need manual cleanup."
unregistration_done:

    Delete "$SMPROGRAMS\VR Piston Animation\VR Piston Animation.lnk"
    RMDir "$SMPROGRAMS\VR Piston Animation"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegKey HKCU "Software\VRPistonAnimation"

    Delete "$INSTDIR\vr-piston-animation.exe"
    Delete "$INSTDIR\openvr_api.dll"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\manifest.vrmanifest"
    Delete "$INSTDIR\LICENSE-VALVE"
    Delete "$INSTDIR\LICENSE-MIT"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
