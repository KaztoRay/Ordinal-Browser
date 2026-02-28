/**
 * @file about_dialog.cpp
 * @brief 정보 다이얼로그 구현 — 앱 정보, 빌드 상세, 라이선스, 업데이트 확인
 * 
 * OrdinalV8 v1.1.0 정보 표시.
 * GitHub API를 통한 업데이트 확인 기능 포함.
 * 
 * © 2026 KaztoRay — MIT License
 */

#include "about_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <QSysInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QDebug>

namespace Ordinal {

// 상수 정의
static constexpr const char* APP_VERSION  = "1.1.0";
static constexpr const char* GITHUB_URL   = "https://github.com/KaztoRay/Ordinal-Browser";
static constexpr const char* GITHUB_API   = "https://api.github.com/repos/KaztoRay/Ordinal-Browser/releases/latest";

// ============================================================
// 생성자
// ============================================================

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("OrdinalV8 정보"));
    setFixedSize(520, 640);
    setupUI();
}

// ============================================================
// UI 레이아웃 구성
// ============================================================

void AboutDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 24, 32, 24);
    mainLayout->setSpacing(8);
    mainLayout->setAlignment(Qt::AlignHCenter);

    // ---- 앱 아이콘 (128x128) ----
    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(128, 128);

    // 아이콘 로드 시도 — 없으면 텍스트 대체
    QPixmap icon(":/icons/ordinalv8.png");
    if (!icon.isNull()) {
        m_iconLabel->setPixmap(icon.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        // 앱 아이콘이 없을 경우 이모지 대체
        m_iconLabel->setText("🌐");
        QFont iconFont = m_iconLabel->font();
        iconFont.setPointSize(64);
        m_iconLabel->setFont(iconFont);
    }
    mainLayout->addWidget(m_iconLabel, 0, Qt::AlignCenter);

    // ---- 제목: "OrdinalV8" (24pt bold) ----
    m_titleLabel = new QLabel("OrdinalV8", this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);

    // ---- 버전: "v1.1.0" ----
    m_versionLabel = new QLabel(QStringLiteral("v%1").arg(APP_VERSION), this);
    QFont versionFont = m_versionLabel->font();
    versionFont.setPointSize(14);
    m_versionLabel->setFont(versionFont);
    m_versionLabel->setAlignment(Qt::AlignCenter);
    m_versionLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(m_versionLabel);

    mainLayout->addSpacing(8);

    // ---- 부제목: 브라우저 설명 ----
    m_subtitleLabel = new QLabel(
        tr("V8 기반 보안 브라우저 + LLM Security Agent"), this);
    QFont subFont = m_subtitleLabel->font();
    subFont.setPointSize(11);
    m_subtitleLabel->setFont(subFont);
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    m_subtitleLabel->setWordWrap(true);
    mainLayout->addWidget(m_subtitleLabel);

    mainLayout->addSpacing(16);

    // ---- 빌드 정보 ----
    m_buildInfoLabel = new QLabel(buildInfoString(), this);
    m_buildInfoLabel->setAlignment(Qt::AlignCenter);
    m_buildInfoLabel->setWordWrap(true);
    m_buildInfoLabel->setStyleSheet("color: #999; font-size: 10pt;");
    mainLayout->addWidget(m_buildInfoLabel);

    mainLayout->addSpacing(8);

    // ---- 저작권 ----
    m_copyrightLabel = new QLabel(
        tr("© 2026 KaztoRay — MIT License"), this);
    m_copyrightLabel->setAlignment(Qt::AlignCenter);
    QFont copyFont = m_copyrightLabel->font();
    copyFont.setPointSize(10);
    m_copyrightLabel->setFont(copyFont);
    mainLayout->addWidget(m_copyrightLabel);

    mainLayout->addSpacing(16);

    // ---- 버튼 행: GitHub + 업데이트 확인 ----
    auto* btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(12);

    m_githubBtn = new QPushButton(tr("🔗 GitHub"), this);
    m_githubBtn->setMinimumWidth(120);
    connect(m_githubBtn, &QPushButton::clicked, this, &AboutDialog::onOpenGitHub);
    btnLayout->addWidget(m_githubBtn);

    m_updateBtn = new QPushButton(tr("🔄 업데이트 확인"), this);
    m_updateBtn->setMinimumWidth(140);
    connect(m_updateBtn, &QPushButton::clicked, this, &AboutDialog::onCheckUpdate);
    btnLayout->addWidget(m_updateBtn);

    mainLayout->addLayout(btnLayout);

    mainLayout->addSpacing(12);

    // ---- 서드파티 라이선스 ----
    auto* licenseLabel = new QLabel(tr("서드파티 라이선스:"), this);
    QFont licFont = licenseLabel->font();
    licFont.setBold(true);
    licenseLabel->setFont(licFont);
    mainLayout->addWidget(licenseLabel);

    m_licensesBrowser = new QTextBrowser(this);
    m_licensesBrowser->setMinimumHeight(140);
    m_licensesBrowser->setOpenExternalLinks(true);
    m_licensesBrowser->setHtml(thirdPartyLicensesText());
    mainLayout->addWidget(m_licensesBrowser);
}

// ============================================================
// 빌드 정보 문자열 생성
// ============================================================

QString AboutDialog::buildInfoString() const {
    return QStringLiteral(
        "Qt %1 | %2 | %3")
        .arg(qVersion(),                       // Qt 런타임 버전
             QSysInfo::prettyProductName(),     // OS 이름
             QSysInfo::currentCpuArchitecture() // CPU 아키텍처
        );
}

// ============================================================
// 서드파티 라이선스 텍스트
// ============================================================

QString AboutDialog::thirdPartyLicensesText() const {
    return QStringLiteral(R"(
<style>
    body { font-size: 9pt; }
    h4 { margin: 8px 0 4px 0; color: #888; }
    p { margin: 2px 0; }
</style>

<h4>V8 JavaScript Engine</h4>
<p>Copyright The V8 project authors. BSD 3-Clause License.</p>

<h4>Qt Framework</h4>
<p>Copyright The Qt Company Ltd. LGPL v3 / Commercial License.</p>

<h4>OpenSSL</h4>
<p>Copyright The OpenSSL Project. Apache License 2.0.</p>

<h4>libcurl</h4>
<p>Copyright Daniel Stenberg. MIT/X derivate License.</p>

<h4>Catppuccin Color Palette</h4>
<p>Copyright Catppuccin contributors. MIT License.</p>

<h4>EasyList / EasyPrivacy</h4>
<p>Copyright EasyList authors. GPL v3 License.</p>

<h4>python-whois</h4>
<p>Copyright Richard Penman. MIT License.</p>

<h4>Protocol Buffers (protobuf)</h4>
<p>Copyright Google LLC. BSD 3-Clause License.</p>

<h4>gRPC</h4>
<p>Copyright The gRPC Authors. Apache License 2.0.</p>

<h4>PyTorch (libtorch)</h4>
<p>Copyright Meta Platforms, Inc. BSD 3-Clause License.</p>
)");
}

// ============================================================
// GitHub 열기
// ============================================================

void AboutDialog::onOpenGitHub() {
    QDesktopServices::openUrl(QUrl(GITHUB_URL));
    qDebug() << "[AboutDialog] GitHub 페이지 열림:" << GITHUB_URL;
}

// ============================================================
// 업데이트 확인 — GitHub Releases API 조회
// ============================================================

void AboutDialog::onCheckUpdate() {
    m_updateBtn->setEnabled(false);
    m_updateBtn->setText(tr("확인 중..."));

    auto* manager = new QNetworkAccessManager(this);

    QNetworkRequest request(QUrl(GITHUB_API));
    request.setHeader(QNetworkRequest::UserAgentHeader, "OrdinalV8/1.1.0");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    QNetworkReply* reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        m_updateBtn->setEnabled(true);
        m_updateBtn->setText(tr("🔄 업데이트 확인"));

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[AboutDialog] 업데이트 확인 실패:" << reply->errorString();
            QMessageBox::warning(this, tr("업데이트 확인"),
                tr("업데이트를 확인할 수 없습니다.\n%1").arg(reply->errorString()));
            reply->deleteLater();
            manager->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();

        QString latestTag = obj.value("tag_name").toString();
        QString latestVersion = latestTag.startsWith('v') ? latestTag.mid(1) : latestTag;
        QString currentVersion = APP_VERSION;

        qDebug() << "[AboutDialog] 최신 버전:" << latestVersion
                 << "/ 현재 버전:" << currentVersion;

        if (latestVersion > currentVersion) {
            QString releaseUrl = obj.value("html_url").toString();
            QMessageBox::information(this, tr("업데이트 가능"),
                tr("새 버전이 있습니다!\n\n"
                   "현재: v%1\n"
                   "최신: v%2\n\n"
                   "GitHub에서 다운로드할 수 있습니다.")
                    .arg(currentVersion, latestVersion));
        } else {
            QMessageBox::information(this, tr("최신 버전"),
                tr("현재 최신 버전(v%1)을 사용 중입니다.").arg(currentVersion));
        }

        reply->deleteLater();
        manager->deleteLater();
    });
}

} // namespace Ordinal
