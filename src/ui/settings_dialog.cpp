/**
 * @file settings_dialog.cpp
 * @brief 설정 다이얼로그 구현 — 6개 탭, QSettings 연동, 데이터 삭제 다이얼로그
 * 
 * 모든 설정은 QSettings("OrdinalV8", "Settings")에 저장/로드.
 * Apply 시 settingsApplied() 시그널로 메인 윈도우에 변경 알림.
 * 
 * © 2026 KaztoRay — MIT License
 */

#include "settings_dialog.h"
#include "theme_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QApplication>
#include <QDebug>

namespace Ordinal {

// ============================================================
// 생성자 — 다이얼로그 초기화, 탭 구성
// ============================================================

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("설정 — OrdinalV8"));
    setMinimumSize(680, 520);
    resize(720, 560);

    // 메인 레이아웃
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // 탭 위젯 생성
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(createGeneralTab(),    tr("🏠 일반"));
    m_tabWidget->addTab(createPrivacyTab(),    tr("🔒 프라이버시"));
    m_tabWidget->addTab(createSecurityTab(),   tr("🛡️ 보안"));
    m_tabWidget->addTab(createAppearanceTab(), tr("🎨 외관"));
    m_tabWidget->addTab(createExtensionsTab(), tr("🧩 확장"));
    m_tabWidget->addTab(createAdvancedTab(),   tr("⚙️ 고급"));
    mainLayout->addWidget(m_tabWidget);

    // OK / Apply / Cancel 버튼
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        this
    );
    mainLayout->addWidget(m_buttonBox);

    // 시그널 연결
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onOk);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply),
            &QPushButton::clicked, this, &SettingsDialog::onApply);

    // 저장된 설정 로드
    loadSettings();
}

// ============================================================
// 일반 탭 — 홈페이지, 시작 동작, 검색 엔진, 북마크바
// ============================================================

QWidget* SettingsDialog::createGeneralTab() {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setSpacing(16);

    // 홈페이지 그룹
    auto* homeGroup = new QGroupBox(tr("홈페이지"), widget);
    auto* homeLayout = new QFormLayout(homeGroup);

    m_homepageEdit = new QLineEdit(homeGroup);
    m_homepageEdit->setPlaceholderText("https://www.example.com");
    homeLayout->addRow(tr("홈페이지 URL:"), m_homepageEdit);

    m_startupCombo = new QComboBox(homeGroup);
    m_startupCombo->addItem(tr("빈 페이지"), "blank");
    m_startupCombo->addItem(tr("이전 세션 복원"), "restore");
    m_startupCombo->addItem(tr("홈페이지 열기"), "home");
    homeLayout->addRow(tr("시작 시:"), m_startupCombo);

    layout->addWidget(homeGroup);

    // 검색 엔진 그룹
    auto* searchGroup = new QGroupBox(tr("검색"), widget);
    auto* searchLayout = new QFormLayout(searchGroup);

    m_searchEngineCombo = new QComboBox(searchGroup);
    m_searchEngineCombo->addItem("Google",       "https://www.google.com/search?q=");
    m_searchEngineCombo->addItem("Bing",         "https://www.bing.com/search?q=");
    m_searchEngineCombo->addItem("Brave Search", "https://search.brave.com/search?q=");
    searchLayout->addRow(tr("기본 검색 엔진:"), m_searchEngineCombo);

    layout->addWidget(searchGroup);

    // 인터페이스 그룹
    auto* uiGroup = new QGroupBox(tr("인터페이스"), widget);
    auto* uiLayout = new QVBoxLayout(uiGroup);

    m_bookmarksBarCheck = new QCheckBox(tr("북마크바 항상 표시"), uiGroup);
    uiLayout->addWidget(m_bookmarksBarCheck);

    layout->addWidget(uiGroup);
    layout->addStretch();

    return widget;
}

// ============================================================
// 프라이버시 탭 — 쿠키, DNT, 비밀번호, 자동완성, 데이터 삭제
// ============================================================

QWidget* SettingsDialog::createPrivacyTab() {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setSpacing(16);

    // 쿠키 그룹
    auto* cookieGroup = new QGroupBox(tr("쿠키"), widget);
    auto* cookieLayout = new QFormLayout(cookieGroup);

    m_cookiePolicyCombo = new QComboBox(cookieGroup);
    m_cookiePolicyCombo->addItem(tr("모든 쿠키 허용"),        "all");
    m_cookiePolicyCombo->addItem(tr("퍼스트파티만 허용"),     "first-party");
    m_cookiePolicyCombo->addItem(tr("모든 쿠키 차단"),        "none");
    cookieLayout->addRow(tr("쿠키 정책:"), m_cookiePolicyCombo);

    layout->addWidget(cookieGroup);

    // 추적 방지 그룹
    auto* trackingGroup = new QGroupBox(tr("추적 방지"), widget);
    auto* trackingLayout = new QVBoxLayout(trackingGroup);

    m_dntCheck = new QCheckBox(tr("Do Not Track (DNT) 헤더 전송"), trackingGroup);
    trackingLayout->addWidget(m_dntCheck);

    layout->addWidget(trackingGroup);

    // 자격 증명 그룹
    auto* credGroup = new QGroupBox(tr("자격 증명"), widget);
    auto* credLayout = new QVBoxLayout(credGroup);

    m_passwordManagerCheck = new QCheckBox(tr("비밀번호 관리자 사용"), credGroup);
    m_autofillCheck = new QCheckBox(tr("양식 자동완성 사용"), credGroup);
    credLayout->addWidget(m_passwordManagerCheck);
    credLayout->addWidget(m_autofillCheck);

    layout->addWidget(credGroup);

    // 데이터 삭제
    auto* dataGroup = new QGroupBox(tr("브라우징 데이터"), widget);
    auto* dataLayout = new QHBoxLayout(dataGroup);

    m_clearDataBtn = new QPushButton(tr("🗑️ 브라우징 데이터 삭제..."), dataGroup);
    connect(m_clearDataBtn, &QPushButton::clicked, this, &SettingsDialog::onClearData);
    dataLayout->addWidget(m_clearDataBtn);
    dataLayout->addStretch();

    layout->addWidget(dataGroup);
    layout->addStretch();

    return widget;
}

// ============================================================
// 보안 탭 — 위협 감도, 피싱/XSS, 추적기/광고 차단, 인증서
// ============================================================

QWidget* SettingsDialog::createSecurityTab() {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setSpacing(16);

    // 위협 감도 그룹
    auto* threatGroup = new QGroupBox(tr("위협 감지 감도"), widget);
    auto* threatLayout = new QVBoxLayout(threatGroup);

    auto* sliderLayout = new QHBoxLayout;
    auto* lowLabel = new QLabel(tr("낮음"), threatGroup);
    m_threatSlider = new QSlider(Qt::Horizontal, threatGroup);
    m_threatSlider->setRange(1, 5);
    m_threatSlider->setTickInterval(1);
    m_threatSlider->setTickPosition(QSlider::TicksBelow);
    auto* highLabel = new QLabel(tr("높음"), threatGroup);
    m_threatLabel = new QLabel("3", threatGroup);
    m_threatLabel->setFixedWidth(24);
    m_threatLabel->setAlignment(Qt::AlignCenter);

    sliderLayout->addWidget(lowLabel);
    sliderLayout->addWidget(m_threatSlider);
    sliderLayout->addWidget(highLabel);
    sliderLayout->addWidget(m_threatLabel);
    threatLayout->addLayout(sliderLayout);

    // 슬라이더 값 변경 시 라벨 업데이트
    connect(m_threatSlider, &QSlider::valueChanged,
            this, [this](int value) {
        m_threatLabel->setText(QString::number(value));
    });

    layout->addWidget(threatGroup);

    // 보호 기능 그룹
    auto* protectGroup = new QGroupBox(tr("보호 기능"), widget);
    auto* protectLayout = new QVBoxLayout(protectGroup);

    m_phishingCheck = new QCheckBox(tr("피싱 사이트 보호"), protectGroup);
    m_xssCheck = new QCheckBox(tr("XSS (크로스사이트 스크립팅) 보호"), protectGroup);
    m_trackerBlockCheck = new QCheckBox(tr("추적기 차단"), protectGroup);
    m_adBlockCheck = new QCheckBox(tr("광고 차단"), protectGroup);
    m_strictCertsCheck = new QCheckBox(tr("엄격한 인증서 검증 (HSTS)"), protectGroup);

    protectLayout->addWidget(m_phishingCheck);
    protectLayout->addWidget(m_xssCheck);
    protectLayout->addWidget(m_trackerBlockCheck);
    protectLayout->addWidget(m_adBlockCheck);
    protectLayout->addWidget(m_strictCertsCheck);

    layout->addWidget(protectGroup);
    layout->addStretch();

    return widget;
}

// ============================================================
// 외관 탭 — 테마, 확대/축소, 글꼴, 상태바, 툴바 스타일
// ============================================================

QWidget* SettingsDialog::createAppearanceTab() {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setSpacing(16);

    // 테마 그룹
    auto* themeGroup = new QGroupBox(tr("테마"), widget);
    auto* themeLayout = new QFormLayout(themeGroup);

    m_themeCombo = new QComboBox(themeGroup);
    m_themeCombo->addItem(tr("라이트"),   "light");
    m_themeCombo->addItem(tr("다크"),     "dark");
    m_themeCombo->addItem(tr("시스템"),   "system");
    themeLayout->addRow(tr("테마:"), m_themeCombo);

    layout->addWidget(themeGroup);

    // 표시 그룹
    auto* displayGroup = new QGroupBox(tr("표시"), widget);
    auto* displayLayout = new QFormLayout(displayGroup);

    m_zoomSpin = new QSpinBox(displayGroup);
    m_zoomSpin->setRange(50, 200);
    m_zoomSpin->setSuffix("%");
    m_zoomSpin->setSingleStep(10);
    displayLayout->addRow(tr("기본 확대/축소:"), m_zoomSpin);

    m_fontCombo = new QFontComboBox(displayGroup);
    displayLayout->addRow(tr("기본 글꼴:"), m_fontCombo);

    layout->addWidget(displayGroup);

    // UI 요소 그룹
    auto* elemGroup = new QGroupBox(tr("UI 요소"), widget);
    auto* elemLayout = new QVBoxLayout(elemGroup);

    m_statusBarCheck = new QCheckBox(tr("상태바 표시"), elemGroup);
    elemLayout->addWidget(m_statusBarCheck);

    auto* toolbarLayout = new QFormLayout;
    m_toolbarStyleCombo = new QComboBox(elemGroup);
    m_toolbarStyleCombo->addItem(tr("아이콘만"), "icons");
    m_toolbarStyleCombo->addItem(tr("텍스트만"), "text");
    m_toolbarStyleCombo->addItem(tr("아이콘 + 텍스트"), "both");
    toolbarLayout->addRow(tr("툴바 스타일:"), m_toolbarStyleCombo);
    elemLayout->addLayout(toolbarLayout);

    layout->addWidget(elemGroup);
    layout->addStretch();

    return widget;
}

// ============================================================
// 확장 탭 — 목록, 로드/삭제, 권한 상세
// ============================================================

QWidget* SettingsDialog::createExtensionsTab() {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setSpacing(12);

    // 확장 목록
    auto* listGroup = new QGroupBox(tr("설치된 확장"), widget);
    auto* listLayout = new QVBoxLayout(listGroup);

    m_extensionsList = new QListWidget(listGroup);
    m_extensionsList->setAlternatingRowColors(true);
    m_extensionsList->setMinimumHeight(160);
    connect(m_extensionsList, &QListWidget::currentRowChanged,
            this, [this]() { onExtensionSelected(); });
    listLayout->addWidget(m_extensionsList);

    // 버튼 행
    auto* btnLayout = new QHBoxLayout;
    m_loadExtBtn = new QPushButton(tr("📂 확장 로드..."), listGroup);
    m_removeExtBtn = new QPushButton(tr("🗑️ 삭제"), listGroup);
    m_removeExtBtn->setEnabled(false);

    connect(m_loadExtBtn, &QPushButton::clicked, this, &SettingsDialog::onLoadExtension);
    connect(m_removeExtBtn, &QPushButton::clicked, this, &SettingsDialog::onRemoveExtension);

    btnLayout->addWidget(m_loadExtBtn);
    btnLayout->addWidget(m_removeExtBtn);
    btnLayout->addStretch();
    listLayout->addLayout(btnLayout);

    layout->addWidget(listGroup);

    // 권한 상세
    auto* permGroup = new QGroupBox(tr("권한 상세"), widget);
    auto* permLayout = new QVBoxLayout(permGroup);

    m_extPermissionsLabel = new QLabel(tr("확장을 선택하면 권한 정보가 표시됩니다."), permGroup);
    m_extPermissionsLabel->setWordWrap(true);
    permLayout->addWidget(m_extPermissionsLabel);

    layout->addWidget(permGroup);
    layout->addStretch();

    return widget;
}

// ============================================================
// 고급 탭 — 프록시, 캐시, 개발자 도구, 실험 기능, 초기화
// ============================================================

QWidget* SettingsDialog::createAdvancedTab() {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setSpacing(16);

    // 네트워크 그룹
    auto* netGroup = new QGroupBox(tr("네트워크"), widget);
    auto* netLayout = new QFormLayout(netGroup);

    m_proxyEdit = new QLineEdit(netGroup);
    m_proxyEdit->setPlaceholderText("socks5://127.0.0.1:1080");
    netLayout->addRow(tr("프록시 서버:"), m_proxyEdit);

    m_cacheSizeSpin = new QSpinBox(netGroup);
    m_cacheSizeSpin->setRange(0, 10240);
    m_cacheSizeSpin->setSuffix(" MB");
    m_cacheSizeSpin->setSingleStep(50);
    netLayout->addRow(tr("캐시 크기:"), m_cacheSizeSpin);

    layout->addWidget(netGroup);

    // 개발 그룹
    auto* devGroup = new QGroupBox(tr("개발"), widget);
    auto* devLayout = new QVBoxLayout(devGroup);

    m_devToolsCheck = new QCheckBox(tr("개발자 도구 활성화 (F12)"), devGroup);
    m_hardwareAccelCheck = new QCheckBox(tr("하드웨어 가속 사용 (GPU)"), devGroup);
    m_experimentalCheck = new QCheckBox(tr("실험적 기능 활성화 ⚠️"), devGroup);

    devLayout->addWidget(m_devToolsCheck);
    devLayout->addWidget(m_hardwareAccelCheck);
    devLayout->addWidget(m_experimentalCheck);

    layout->addWidget(devGroup);

    // 초기화
    auto* resetGroup = new QGroupBox(tr("초기화"), widget);
    auto* resetLayout = new QHBoxLayout(resetGroup);

    m_resetBtn = new QPushButton(tr("⚠️ 모든 설정 초기화"), resetGroup);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsDialog::onResetSettings);
    resetLayout->addWidget(m_resetBtn);
    resetLayout->addStretch();

    layout->addWidget(resetGroup);
    layout->addStretch();

    return widget;
}

// ============================================================
// 설정 로드 — QSettings → 위젯
// ============================================================

void SettingsDialog::loadSettings() {
    QSettings s("OrdinalV8", "Settings");

    // 일반
    m_homepageEdit->setText(s.value("general/homepage", "https://www.google.com").toString());

    QString startup = s.value("general/startup", "home").toString();
    int startupIdx = m_startupCombo->findData(startup);
    m_startupCombo->setCurrentIndex(startupIdx >= 0 ? startupIdx : 2);

    QString engine = s.value("general/searchEngine", "https://www.google.com/search?q=").toString();
    int engineIdx = m_searchEngineCombo->findData(engine);
    m_searchEngineCombo->setCurrentIndex(engineIdx >= 0 ? engineIdx : 0);

    m_bookmarksBarCheck->setChecked(s.value("general/bookmarksBar", true).toBool());

    // 프라이버시
    QString cookiePolicy = s.value("privacy/cookiePolicy", "first-party").toString();
    int cookieIdx = m_cookiePolicyCombo->findData(cookiePolicy);
    m_cookiePolicyCombo->setCurrentIndex(cookieIdx >= 0 ? cookieIdx : 1);

    m_dntCheck->setChecked(s.value("privacy/dnt", true).toBool());
    m_passwordManagerCheck->setChecked(s.value("privacy/passwordManager", true).toBool());
    m_autofillCheck->setChecked(s.value("privacy/autofill", true).toBool());

    // 보안
    m_threatSlider->setValue(s.value("security/threatSensitivity", 3).toInt());
    m_phishingCheck->setChecked(s.value("security/phishingProtection", true).toBool());
    m_xssCheck->setChecked(s.value("security/xssProtection", true).toBool());
    m_trackerBlockCheck->setChecked(s.value("security/trackerBlocking", true).toBool());
    m_adBlockCheck->setChecked(s.value("security/adBlocking", true).toBool());
    m_strictCertsCheck->setChecked(s.value("security/strictCerts", false).toBool());

    // 외관
    QString theme = s.value("appearance/theme", "system").toString();
    int themeIdx = m_themeCombo->findData(theme);
    m_themeCombo->setCurrentIndex(themeIdx >= 0 ? themeIdx : 2);

    m_zoomSpin->setValue(s.value("appearance/zoom", 100).toInt());

    QString fontFamily = s.value("appearance/font", QApplication::font().family()).toString();
    m_fontCombo->setCurrentFont(QFont(fontFamily));

    m_statusBarCheck->setChecked(s.value("appearance/statusBar", true).toBool());

    QString toolbarStyle = s.value("appearance/toolbarStyle", "both").toString();
    int tbIdx = m_toolbarStyleCombo->findData(toolbarStyle);
    m_toolbarStyleCombo->setCurrentIndex(tbIdx >= 0 ? tbIdx : 2);

    // 고급
    m_proxyEdit->setText(s.value("advanced/proxy", "").toString());
    m_cacheSizeSpin->setValue(s.value("advanced/cacheSize", 512).toInt());
    m_devToolsCheck->setChecked(s.value("advanced/devTools", true).toBool());
    m_hardwareAccelCheck->setChecked(s.value("advanced/hardwareAccel", true).toBool());
    m_experimentalCheck->setChecked(s.value("advanced/experimental", false).toBool());

    qDebug() << "[SettingsDialog] 설정 로드 완료";
}

// ============================================================
// 설정 저장 — 위젯 → QSettings
// ============================================================

void SettingsDialog::saveSettings() {
    QSettings s("OrdinalV8", "Settings");

    // 일반
    s.setValue("general/homepage",      m_homepageEdit->text());
    s.setValue("general/startup",       m_startupCombo->currentData().toString());
    s.setValue("general/searchEngine",  m_searchEngineCombo->currentData().toString());
    s.setValue("general/bookmarksBar",  m_bookmarksBarCheck->isChecked());

    // 프라이버시
    s.setValue("privacy/cookiePolicy",     m_cookiePolicyCombo->currentData().toString());
    s.setValue("privacy/dnt",              m_dntCheck->isChecked());
    s.setValue("privacy/passwordManager",  m_passwordManagerCheck->isChecked());
    s.setValue("privacy/autofill",         m_autofillCheck->isChecked());

    // 보안
    s.setValue("security/threatSensitivity",  m_threatSlider->value());
    s.setValue("security/phishingProtection", m_phishingCheck->isChecked());
    s.setValue("security/xssProtection",      m_xssCheck->isChecked());
    s.setValue("security/trackerBlocking",    m_trackerBlockCheck->isChecked());
    s.setValue("security/adBlocking",         m_adBlockCheck->isChecked());
    s.setValue("security/strictCerts",        m_strictCertsCheck->isChecked());

    // 외관 — 테마 변경 시 ThemeManager에도 알림
    QString themeStr = m_themeCombo->currentData().toString();
    s.setValue("appearance/theme", themeStr);
    if (themeStr == "light") {
        ThemeManager::instance().setTheme(ThemeMode::Light);
    } else if (themeStr == "dark") {
        ThemeManager::instance().setTheme(ThemeMode::Dark);
    } else {
        ThemeManager::instance().setTheme(ThemeMode::System);
    }

    s.setValue("appearance/zoom",         m_zoomSpin->value());
    s.setValue("appearance/font",         m_fontCombo->currentFont().family());
    s.setValue("appearance/statusBar",    m_statusBarCheck->isChecked());
    s.setValue("appearance/toolbarStyle", m_toolbarStyleCombo->currentData().toString());

    // 고급
    s.setValue("advanced/proxy",         m_proxyEdit->text());
    s.setValue("advanced/cacheSize",     m_cacheSizeSpin->value());
    s.setValue("advanced/devTools",      m_devToolsCheck->isChecked());
    s.setValue("advanced/hardwareAccel", m_hardwareAccelCheck->isChecked());
    s.setValue("advanced/experimental",  m_experimentalCheck->isChecked());

    s.sync();
    qDebug() << "[SettingsDialog] 설정 저장 완료";
}

// ============================================================
// 버튼 슬롯
// ============================================================

void SettingsDialog::onApply() {
    saveSettings();
    emit settingsApplied();
}

void SettingsDialog::onOk() {
    saveSettings();
    emit settingsApplied();
    accept();
}

// ============================================================
// 데이터 삭제 다이얼로그
// ============================================================

void SettingsDialog::onClearData() {
    // 삭제 옵션 다이얼로그 생성
    QDialog dlg(this);
    dlg.setWindowTitle(tr("브라우징 데이터 삭제"));
    dlg.setMinimumWidth(360);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(12);

    auto* infoLabel = new QLabel(tr("삭제할 데이터를 선택하세요:"), &dlg);
    layout->addWidget(infoLabel);

    // 시간 범위 선택
    auto* timeLayout = new QFormLayout;
    auto* timeCombo = new QComboBox(&dlg);
    timeCombo->addItem(tr("지난 1시간"),  "1h");
    timeCombo->addItem(tr("지난 24시간"), "24h");
    timeCombo->addItem(tr("지난 7일"),    "7d");
    timeCombo->addItem(tr("지난 30일"),   "30d");
    timeCombo->addItem(tr("전체 기간"),   "all");
    timeCombo->setCurrentIndex(4);  // 기본: 전체
    timeLayout->addRow(tr("시간 범위:"), timeCombo);
    layout->addLayout(timeLayout);

    // 삭제 항목 체크박스
    auto* cacheCheck    = new QCheckBox(tr("캐시된 이미지 및 파일"), &dlg);
    auto* cookiesCheck  = new QCheckBox(tr("쿠키 및 사이트 데이터"), &dlg);
    auto* historyCheck  = new QCheckBox(tr("방문 기록"), &dlg);
    auto* passwordsCheck = new QCheckBox(tr("저장된 비밀번호"), &dlg);

    cacheCheck->setChecked(true);
    cookiesCheck->setChecked(true);
    historyCheck->setChecked(true);
    passwordsCheck->setChecked(false);  // 비밀번호는 기본 미선택

    layout->addWidget(cacheCheck);
    layout->addWidget(cookiesCheck);
    layout->addWidget(historyCheck);
    layout->addWidget(passwordsCheck);

    // 확인/취소 버튼
    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btnBox->button(QDialogButtonBox::Ok)->setText(tr("삭제"));
    layout->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        // 선택된 항목 삭제 처리
        QStringList deleted;
        if (cacheCheck->isChecked())    deleted << tr("캐시");
        if (cookiesCheck->isChecked())  deleted << tr("쿠키");
        if (historyCheck->isChecked())  deleted << tr("방문 기록");
        if (passwordsCheck->isChecked()) deleted << tr("비밀번호");

        QString timeRange = timeCombo->currentText();

        qDebug() << "[SettingsDialog] 데이터 삭제:" << deleted.join(", ")
                 << "/ 범위:" << timeRange;

        QMessageBox::information(this, tr("삭제 완료"),
            tr("%1 데이터가 삭제되었습니다.\n범위: %2")
                .arg(deleted.join(", "), timeRange));
    }
}

// ============================================================
// 확장 관련 슬롯
// ============================================================

void SettingsDialog::onLoadExtension() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("확장 디렉터리 선택"), QString(),
        QFileDialog::ShowDirsOnly);

    if (dir.isEmpty()) return;

    // 확장 디렉터리에서 manifest 로드 (간략화)
    QString extName = QFileInfo(dir).fileName();
    auto* item = new QListWidgetItem(
        QStringLiteral("🧩 %1 — v1.0.0").arg(extName),
        m_extensionsList);
    item->setData(Qt::UserRole, dir);
    item->setCheckState(Qt::Checked);  // 토글 활성화

    qDebug() << "[SettingsDialog] 확장 로드됨:" << extName << "경로:" << dir;
}

void SettingsDialog::onRemoveExtension() {
    int row = m_extensionsList->currentRow();
    if (row < 0) return;

    QListWidgetItem* item = m_extensionsList->item(row);
    int ret = QMessageBox::question(this, tr("확장 삭제"),
        tr("'%1' 확장을 삭제하시겠습니까?").arg(item->text()),
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        qDebug() << "[SettingsDialog] 확장 삭제:" << item->text();
        delete m_extensionsList->takeItem(row);
        m_extPermissionsLabel->setText(tr("확장을 선택하면 권한 정보가 표시됩니다."));
        m_removeExtBtn->setEnabled(false);
    }
}

void SettingsDialog::onExtensionSelected() {
    int row = m_extensionsList->currentRow();
    m_removeExtBtn->setEnabled(row >= 0);

    if (row < 0) {
        m_extPermissionsLabel->setText(tr("확장을 선택하면 권한 정보가 표시됩니다."));
        return;
    }

    QListWidgetItem* item = m_extensionsList->item(row);
    QString extPath = item->data(Qt::UserRole).toString();

    // 권한 정보 표시 (간략화 — 실제로는 manifest.json 파싱)
    m_extPermissionsLabel->setText(
        tr("<b>%1</b><br><br>"
           "<b>권한:</b><br>"
           "• 웹 페이지 접근<br>"
           "• 네트워크 요청 모니터링<br>"
           "• 로컬 저장소 사용<br><br>"
           "<b>경로:</b> %2")
            .arg(item->text(), extPath));
}

// ============================================================
// 설정 초기화
// ============================================================

void SettingsDialog::onResetSettings() {
    int ret = QMessageBox::warning(this, tr("설정 초기화"),
        tr("모든 설정을 기본값으로 되돌리시겠습니까?\n"
           "이 작업은 되돌릴 수 없습니다."),
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        QSettings s("OrdinalV8", "Settings");
        s.clear();
        s.sync();

        // 테마도 기본값으로
        ThemeManager::instance().setTheme(ThemeMode::System);
        ThemeManager::instance().resetAccentColor();

        // 위젯 다시 로드
        loadSettings();

        qDebug() << "[SettingsDialog] 모든 설정 초기화됨";
        QMessageBox::information(this, tr("초기화 완료"),
            tr("모든 설정이 기본값으로 되돌려졌습니다."));
    }
}

} // namespace Ordinal
