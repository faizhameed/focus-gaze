#include "ui/DashboardWindow.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

namespace focusgaze {
namespace {

/// Shared dark palette from the Stitch focusGaze design system.
const char* kAppStyle = R"(
  QWidget#DashboardRoot {
    background: #0B0F14;
    color: #F1F5F9;
    font-family: Inter, "SF Pro Text", "Helvetica Neue", Arial, sans-serif;
  }
  QLabel#Title {
    color: #2DD4BF;
    font-size: 20px;
    font-weight: 700;
  }
  QLabel#Subtitle {
    color: #94A3B8;
    font-size: 12px;
  }
  QFrame#Card {
    background: #141A22;
    border: 1px solid #243041;
    border-radius: 12px;
  }
  QLabel#CardTitle {
    color: #94A3B8;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    text-transform: uppercase;
  }
  QLabel#CardValue {
    color: #F1F5F9;
    font-size: 14px;
    font-weight: 600;
  }
  QPushButton#Primary {
    background: #2DD4BF;
    color: #04352F;
    border: none;
    border-radius: 8px;
    padding: 10px 14px;
    font-weight: 700;
  }
  QPushButton#Primary:hover { background: #5EEAD4; }
  QPushButton#Secondary {
    background: transparent;
    color: #CBD5E1;
    border: 1px solid #243041;
    border-radius: 8px;
    padding: 9px 12px;
    font-weight: 600;
  }
  QPushButton#Secondary:hover {
    background: rgba(47, 54, 52, 0.35);
  }
  QPushButton#FocusBtn[focusOn="true"] {
    background: #2DD4BF;
    color: #04352F;
  }
  QPushButton#FocusBtn[focusOn="false"] {
    background: #243041;
    color: #F1F5F9;
  }
  QCheckBox { color: #F1F5F9; spacing: 8px; }
  QCheckBox::indicator {
    width: 16px; height: 16px;
  }
)";

QFrame* makeCard(const QString& title, QLabel** value_out, QWidget* parent) {
  auto* card = new QFrame(parent);
  card->setObjectName("Card");
  auto* lay = new QVBoxLayout(card);
  lay->setContentsMargins(14, 12, 14, 12);
  lay->setSpacing(6);
  auto* t = new QLabel(title, card);
  t->setObjectName("CardTitle");
  auto* v = new QLabel("—", card);
  v->setObjectName("CardValue");
  v->setWordWrap(true);
  lay->addWidget(t);
  lay->addWidget(v);
  *value_out = v;
  return card;
}

} // namespace

DashboardWindow::DashboardWindow(QWidget* parent) : QWidget(parent) {
  setObjectName("DashboardRoot");
  setWindowTitle(tr("focusGaze"));
  setMinimumSize(720, 520);
  setStyleSheet(kAppStyle);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 18, 20, 18);
  root->setSpacing(14);

  auto* top = new QHBoxLayout();
  auto* brand = new QVBoxLayout();
  auto* title = new QLabel(tr("focusGaze"), this);
  title->setObjectName("Title");
  auto* sub = new QLabel(tr("Local productivity guardian"), this);
  sub->setObjectName("Subtitle");
  brand->addWidget(title);
  brand->addWidget(sub);
  top->addLayout(brand, 1);

  focus_badge_ = new QLabel(tr("FOCUS OFF"), this);
  focus_badge_->setObjectName("CardValue");
  focus_btn_ = new QPushButton(tr("Turn Focus ON"), this);
  focus_btn_->setObjectName("FocusBtn");
  focus_btn_->setProperty("focusOn", false);
  focus_btn_->setMinimumWidth(140);
  connect(focus_btn_, &QPushButton::clicked, this, [this]() {
    emit focusToggled(!focus_on_);
  });
  top->addWidget(focus_badge_, 0, Qt::AlignVCenter);
  top->addWidget(focus_btn_, 0, Qt::AlignVCenter);
  root->addLayout(top);

  auto* grid = new QGridLayout();
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(12);
  grid->addWidget(makeCard(tr("Bridge"), &bridge_value_, this), 0, 0);
  grid->addWidget(makeCard(tr("Camera"), &camera_value_, this), 0, 1);
  grid->addWidget(makeCard(tr("Phone"), &phone_value_, this), 1, 0);
  grid->addWidget(makeCard(tr("Alarms"), &alarm_value_, this), 1, 1);
  root->addLayout(grid);

  auto* session_card = new QFrame(this);
  session_card->setObjectName("Card");
  auto* session_lay = new QVBoxLayout(session_card);
  auto* session_title = new QLabel(tr("Last session"), session_card);
  session_title->setObjectName("CardTitle");
  session_line_ = new QLabel(tr("No sessions yet."), session_card);
  session_line_->setObjectName("CardValue");
  session_line_->setWordWrap(true);
  session_lay->addWidget(session_title);
  session_lay->addWidget(session_line_);
  root->addWidget(session_card);

  auto* actions = new QHBoxLayout();
  auto* preview = new QPushButton(tr("Show camera preview"), this);
  preview->setObjectName("Secondary");
  connect(preview, &QPushButton::clicked, this, &DashboardWindow::showCameraPreviewRequested);

  auto* connect_btn = new QPushButton(tr("Connect browser"), this);
  connect_btn->setObjectName("Primary");
  connect_btn->setToolTip(
      tr("Open a one-time pair page in Google Chrome so the extension receives the bridge token automatically"));
  connect(connect_btn, &QPushButton::clicked, this, &DashboardWindow::connectBrowserRequested);

  auto* store_btn = new QPushButton(tr("Get extension…"), this);
  store_btn->setObjectName("Secondary");
  store_btn->setToolTip(tr("Open the extension install page (Web Store or GitHub)"));
  connect(store_btn, &QPushButton::clicked, this, &DashboardWindow::openExtensionStoreRequested);

  auto* copy = new QPushButton(tr("Copy bridge token"), this);
  copy->setObjectName("Secondary");
  connect(copy, &QPushButton::clicked, this, &DashboardWindow::copyTokenRequested);

  auto* stats = new QPushButton(tr("Session stats"), this);
  stats->setObjectName("Secondary");
  connect(stats, &QPushButton::clicked, this, &DashboardWindow::showStatsRequested);

  auto* install = new QPushButton(tr("Dev: all profiles"), this);
  install->setObjectName("Secondary");
  install->setToolTip(tr("Developer: load unpacked extension into every Chrome profile"));
  connect(install, &QPushButton::clicked, this, &DashboardWindow::installExtensionRequested);

  actions->addWidget(preview);
  actions->addWidget(stats);
  actions->addWidget(copy);
  actions->addStretch(1);
  actions->addWidget(store_btn);
  actions->addWidget(install);
  actions->addWidget(connect_btn);
  root->addLayout(actions);

  camera_check_ = new QCheckBox(tr("Camera monitoring (phone detection in background)"), this);
  connect(camera_check_, &QCheckBox::toggled, this, &DashboardWindow::cameraToggled);
  root->addWidget(camera_check_);
  root->addStretch(1);
}

void DashboardWindow::setStatus(bool focus_on, bool bridge_ok, int bridge_port, bool camera_on,
                               bool phone_visible, bool alarm_active, const QString& alarm_text,
                               const QString& last_session_line) {
  focus_on_ = focus_on;
  focus_badge_->setText(focus_on ? tr("FOCUS ON") : tr("FOCUS OFF"));
  focus_btn_->setText(focus_on ? tr("Turn Focus OFF") : tr("Turn Focus ON"));
  focus_btn_->setProperty("focusOn", focus_on);
  focus_btn_->style()->unpolish(focus_btn_);
  focus_btn_->style()->polish(focus_btn_);

  bridge_value_->setText(bridge_ok ? tr("Online · port %1").arg(bridge_port) : tr("Stopped"));
  camera_value_->setText(camera_on ? tr("Monitoring (background)") : tr("Off"));
  phone_value_->setText(phone_visible ? tr("In use / visible") : tr("Idle / desk"));
  if (alarm_active) {
    alarm_value_->setText(alarm_text.isEmpty() ? tr("Active") : alarm_text);
    alarm_value_->setStyleSheet("color: #F87171; font-size: 14px; font-weight: 700;");
  } else {
    alarm_value_->setText(tr("None"));
    alarm_value_->setStyleSheet("color: #34D399; font-size: 14px; font-weight: 600;");
  }
  session_line_->setText(last_session_line.isEmpty() ? tr("No sessions yet.") : last_session_line);

  // Avoid feedback loops when refreshing from controller.
  const QSignalBlocker block(camera_check_);
  camera_check_->setChecked(camera_on);
}

} // namespace focusgaze
