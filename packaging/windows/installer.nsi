; =============================================================================
; OrdinalV8 — NSIS 설치 스크립트
; Windows x64/ARM64 설치 파일 생성
; =============================================================================

; 컴파일러 설정
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

; ---------------------------------------------------------------------------
; 버전 정보 (빌드 시 /DVERSION=v1.0.0 /DARCH=x64 로 전달)
; ---------------------------------------------------------------------------
!ifndef VERSION
  !define VERSION "v1.0.0"
!endif
!ifndef ARCH
  !define ARCH "x64"
!endif

; 버전에서 'v' 접두사 제거
!define VERSION_CLEAN "${{VERSION}}"
!define PRODUCT_NAME "OrdinalV8"
!define PRODUCT_PUBLISHER "KaztoRay"
!define PRODUCT_WEB_SITE "https://github.com/KaztoRay/ordinalv8"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_DIR_REGKEY "Software\${PRODUCT_NAME}"

; ---------------------------------------------------------------------------
; 일반 설정
; ---------------------------------------------------------------------------
Name "${PRODUCT_NAME} ${VERSION}"
OutFile "ordinalv8-${VERSION}-${ARCH}-setup.exe"
Unicode True
SetCompressor /SOLID lzma
SetCompressorDictSize 64
RequestExecutionLevel admin

; 설치 디렉토리 — 64비트 프로그램 폴더
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" "InstallDir"

; 아이콘
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; ---------------------------------------------------------------------------
; Modern UI 설정
; ---------------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\win.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\win.bmp"
!define MUI_FINISHPAGE_RUN "$INSTDIR\ordinalv8.exe"
!define MUI_FINISHPAGE_RUN_TEXT "OrdinalV8 실행"

; ---------------------------------------------------------------------------
; 설치 페이지
; ---------------------------------------------------------------------------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; ---------------------------------------------------------------------------
; 제거 페이지
; ---------------------------------------------------------------------------
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; ---------------------------------------------------------------------------
; 언어 설정 — 한국어 + 영어
; ---------------------------------------------------------------------------
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; 버전 정보 (파일 속성에 표시)
; ---------------------------------------------------------------------------
VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=${LANG_KOREAN} "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=${LANG_KOREAN} "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=${LANG_KOREAN} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_KOREAN} "FileDescription" "V8 기반 보안 브라우저 + LLM Security Agent"
VIAddVersionKey /LANG=${LANG_KOREAN} "LegalCopyright" "Copyright 2026 ${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=${LANG_ENGLISH_US} "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH_US} "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=${LANG_ENGLISH_US} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH_US} "FileDescription" "V8-based Secure Browser + LLM Security Agent"
VIAddVersionKey /LANG=${LANG_ENGLISH_US} "LegalCopyright" "Copyright 2026 ${PRODUCT_PUBLISHER}"

; =============================================================================
; 섹션: 메인 프로그램 (필수)
; =============================================================================
Section "!${PRODUCT_NAME} 코어" SEC_CORE
  SectionIn RO  ; 필수 — 선택 해제 불가

  SetOutPath "$INSTDIR"

  ; -----------------------------------------------------------------------
  ; 메인 실행 파일 및 핵심 DLL
  ; -----------------------------------------------------------------------
  File "dist\ordinalv8.exe"

  ; -----------------------------------------------------------------------
  ; Qt6 런타임 DLL — windeployqt로 배포된 파일들
  ; -----------------------------------------------------------------------
  File /nonfatal "dist\Qt6Core.dll"
  File /nonfatal "dist\Qt6Gui.dll"
  File /nonfatal "dist\Qt6Widgets.dll"
  File /nonfatal "dist\Qt6Network.dll"
  File /nonfatal "dist\Qt6Svg.dll"

  ; Qt6 플러그인 — platforms
  SetOutPath "$INSTDIR\platforms"
  File /nonfatal "dist\platforms\qwindows.dll"

  ; Qt6 플러그인 — styles
  SetOutPath "$INSTDIR\styles"
  File /nonfatal "dist\styles\qwindowsvistastyle.dll"

  ; Qt6 플러그인 — imageformats
  SetOutPath "$INSTDIR\imageformats"
  File /nonfatal "dist\imageformats\*.dll"

  ; Qt6 플러그인 — tls (SSL/TLS 백엔드)
  SetOutPath "$INSTDIR\tls"
  File /nonfatal "dist\tls\qschannelbackend.dll"
  File /nonfatal "dist\tls\qopensslbackend.dll"

  SetOutPath "$INSTDIR"

  ; -----------------------------------------------------------------------
  ; V8 엔진 바이너리
  ; -----------------------------------------------------------------------
  File /nonfatal "dist\v8.dll"
  File /nonfatal "dist\v8_libbase.dll"
  File /nonfatal "dist\v8_libplatform.dll"
  File /nonfatal "dist\icuuc.dll"
  File /nonfatal "dist\icui18n.dll"
  File /nonfatal "dist\icudt.dll"
  File /nonfatal "dist\snapshot_blob.bin"

  ; -----------------------------------------------------------------------
  ; OpenSSL 바이너리
  ; -----------------------------------------------------------------------
  File /nonfatal "dist\libssl-3-x64.dll"
  File /nonfatal "dist\libcrypto-3-x64.dll"

  ; -----------------------------------------------------------------------
  ; MSVC 런타임 재배포 가능 패키지
  ; -----------------------------------------------------------------------
  File /nonfatal "dist\msvcp140.dll"
  File /nonfatal "dist\vcruntime140.dll"
  File /nonfatal "dist\vcruntime140_1.dll"
  File /nonfatal "dist\concrt140.dll"

  ; -----------------------------------------------------------------------
  ; 라이선스 및 문서
  ; -----------------------------------------------------------------------
  File "..\..\LICENSE"
  File "..\..\README.md"

  ; -----------------------------------------------------------------------
  ; 제거 프로그램 생성
  ; -----------------------------------------------------------------------
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; -----------------------------------------------------------------------
  ; 레지스트리 — 프로그램 추가/제거 항목
  ; -----------------------------------------------------------------------
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "Version" "${VERSION}"

  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\ordinalv8.exe"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoRepair" 1

  ; 설치 크기 계산 및 등록
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"

SectionEnd

; =============================================================================
; 섹션: 시작 메뉴 바로가기
; =============================================================================
Section "시작 메뉴 바로가기" SEC_STARTMENU
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\ordinalv8.exe"
  CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\제거.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

; =============================================================================
; 섹션: 바탕화면 바로가기
; =============================================================================
Section "바탕화면 바로가기" SEC_DESKTOP
  CreateShortcut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\ordinalv8.exe"
SectionEnd

; =============================================================================
; 섹션: 파일 연결 (선택)
; =============================================================================
Section /o "HTML 파일 연결 (.html, .htm)" SEC_FILEASSOC
  ; .html 연결
  WriteRegStr HKCR ".html\OpenWithProgids" "OrdinalV8.HTML" ""
  WriteRegStr HKCR "OrdinalV8.HTML" "" "HTML Document"
  WriteRegStr HKCR "OrdinalV8.HTML\DefaultIcon" "" "$INSTDIR\ordinalv8.exe,0"
  WriteRegStr HKCR "OrdinalV8.HTML\shell\open\command" "" '"$INSTDIR\ordinalv8.exe" "%1"'

  ; .htm 연결
  WriteRegStr HKCR ".htm\OpenWithProgids" "OrdinalV8.HTML" ""

  ; 시스템에 파일 연결 변경 알림
  System::Call 'shell32::SHChangeNotify(i 0x8000000, i 0, p 0, p 0)'
SectionEnd

; =============================================================================
; 섹션 설명
; =============================================================================
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_CORE} "OrdinalV8 코어 파일 (필수)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_STARTMENU} "시작 메뉴에 바로가기 생성"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} "바탕화면에 바로가기 생성"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_FILEASSOC} "HTML 파일을 OrdinalV8로 열기"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; =============================================================================
; 설치 전 콜백 — 시스템 요구사항 확인
; =============================================================================
Function .onInit
  ; Windows 10 이상 확인
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "OrdinalV8는 Windows 10 이상이 필요합니다."
    Abort
  ${EndIf}

  ; 64비트 OS 확인 (ARM64도 64비트)
  ${IfNot} ${RunningX64}
    ; ARM64인지 추가 확인
    System::Call 'kernel32::IsWow64Process2(p -1, *i .r0, *i .r1)'
    ${If} $1 != 0xAA64
      MessageBox MB_OK|MB_ICONSTOP "OrdinalV8는 64비트 Windows가 필요합니다."
      Abort
    ${EndIf}
  ${EndIf}

  ; 이전 설치 확인 — 업그레이드 안내
  ReadRegStr $0 HKLM "${PRODUCT_DIR_REGKEY}" "InstallDir"
  ${If} $0 != ""
    MessageBox MB_YESNO|MB_ICONQUESTION \
      "OrdinalV8가 이미 설치되어 있습니다.$\n기존 버전을 업그레이드하시겠습니까?" \
      IDYES upgrade
      Abort
    upgrade:
      ; 기존 설치 디렉토리를 기본값으로 사용
      StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

; =============================================================================
; 제거 섹션
; =============================================================================
Section "Uninstall"
  ; -----------------------------------------------------------------------
  ; 실행 중인 프로세스 종료 시도
  ; -----------------------------------------------------------------------
  nsExec::ExecToLog 'taskkill /F /IM ordinalv8.exe'

  ; -----------------------------------------------------------------------
  ; 파일 삭제
  ; -----------------------------------------------------------------------
  ; 메인 실행 파일
  Delete "$INSTDIR\ordinalv8.exe"

  ; Qt6 DLL
  Delete "$INSTDIR\Qt6Core.dll"
  Delete "$INSTDIR\Qt6Gui.dll"
  Delete "$INSTDIR\Qt6Widgets.dll"
  Delete "$INSTDIR\Qt6Network.dll"
  Delete "$INSTDIR\Qt6Svg.dll"

  ; Qt6 플러그인
  Delete "$INSTDIR\platforms\qwindows.dll"
  RMDir "$INSTDIR\platforms"
  Delete "$INSTDIR\styles\qwindowsvistastyle.dll"
  RMDir "$INSTDIR\styles"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\tls"

  ; V8 엔진
  Delete "$INSTDIR\v8.dll"
  Delete "$INSTDIR\v8_libbase.dll"
  Delete "$INSTDIR\v8_libplatform.dll"
  Delete "$INSTDIR\icuuc.dll"
  Delete "$INSTDIR\icui18n.dll"
  Delete "$INSTDIR\icudt.dll"
  Delete "$INSTDIR\snapshot_blob.bin"

  ; OpenSSL
  Delete "$INSTDIR\libssl-3-x64.dll"
  Delete "$INSTDIR\libcrypto-3-x64.dll"

  ; MSVC 런타임
  Delete "$INSTDIR\msvcp140.dll"
  Delete "$INSTDIR\vcruntime140.dll"
  Delete "$INSTDIR\vcruntime140_1.dll"
  Delete "$INSTDIR\concrt140.dll"

  ; 문서
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\README.md"

  ; 제거 프로그램 자체
  Delete "$INSTDIR\uninstall.exe"

  ; 설치 디렉토리 삭제 (비어있는 경우만)
  RMDir "$INSTDIR"

  ; -----------------------------------------------------------------------
  ; 바로가기 삭제
  ; -----------------------------------------------------------------------
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\제거.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  ; -----------------------------------------------------------------------
  ; 레지스트리 정리
  ; -----------------------------------------------------------------------
  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"

  ; 파일 연결 제거
  DeleteRegValue HKCR ".html\OpenWithProgids" "OrdinalV8.HTML"
  DeleteRegValue HKCR ".htm\OpenWithProgids" "OrdinalV8.HTML"
  DeleteRegKey HKCR "OrdinalV8.HTML"

  ; 시스템에 파일 연결 변경 알림
  System::Call 'shell32::SHChangeNotify(i 0x8000000, i 0, p 0, p 0)'

SectionEnd
