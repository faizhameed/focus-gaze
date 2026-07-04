#include "ui/DashboardWindow.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

namespace focusgaze {
namespace {

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
  QPushButton#Nav {
    background: transparent;
    color: #94A3B8;
    border: 1px solid transparent;
    border-radius: 8px;
    padding: 8px 12px;
    font-weight: 600;
    text-align: left;
  }
  QPushButton#Nav[active="true"] {
    background: #141A22;
    color: #2DD4BF;
    border: 1px solid #243041;
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
  QComboBox {
    background: #141A22;
    color: #F1F5F9;
    border: 1px solid #243041;
    border-radius: 8px;
    padding: 6px 10px;
    min-width: 280px;
  }
  QComboBox QAbstractItemView {
    background: #141A22;
    color: #F1F5F9;
    selection-background-color: #243041;
  }
  QPlainTextEdit {
    background: #141A22;
    color: #E2E8F0;
    border: 1px solid #243041;
    border-radius: 12px;
    padding: 12px;
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    font-size: 12px;
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
  auto* v = new QLabel(QStringLiteral("—"), card);
  v->setObjectName("CardValue");
  v->setWordWrap(true);
  lay->addWidget(t);
  lay->addWidget(v);
  *value_out = v;
  return card;
}

QPushButton* makeNav(const QString& text, QWidget* parent) {
  auto* b = new QPushButton(text, parent);
  b->setObjectName("Nav");
  b->setCursor(Qt::PointingHandCursor);
  b->setProperty("active", false);
  return b;
}

} // namespace

DashboardWindow::DashboardWindow(QWidget* parent) : QWidget(parent) {
  setObjectName("DashboardRoot");
  setWindowTitle(tr("focusGaze"));
  setMinimumSize(760, 560);
  setStyleSheet(kAppStyle);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 16);
  root->setSpacing(14);

  // --- Left nav ---
  auto* nav = new QVBoxLayout();
  nav->setSpacing(6);
  auto* brand = new QLabel(tr("focusGaze"), this);
  brand->setObjectName("Title");
  auto* sub = new QLabel(tr("Local productivity guardian"), this);
  sub->setObjectName("Subtitle");
  nav->addWidget(brand);
  nav->addWidget(sub);
  nav->addSpacing(12);

  nav_overview_ = makeNav(tr("Overview"), this);
  nav_status_ = makeNav(tr("Status"), this);
  nav_stats_ = makeNav(tr("Last session"), this);
  connect(nav_overview_, &QPushButton::clicked, this, [this]() { showPage(Page::Overview); });
  connect(nav_status_, &QPushButton::clicked, this, [this]() { showPage(Page::Status); });
  connect(nav_stats_, &QPushButton::clicked, this, [this]() {
    emit refreshStatsRequested();
    showPage(Page::Stats);
  });
  nav->addWidget(nav_overview_);
  nav->addWidget(nav_status_);
  nav->addWidget(nav_stats_);
  nav->addStretch(1);
  root->addLayout(nav, 0);

  stack_ = new QStackedWidget(this);

  // ===== Overview page =====
  auto* overview = new QWidget(stack_);
  auto* ol = new QVBoxLayout(overview);
  ol->setContentsMargins(4, 0, 0, 0);
  ol->setSpacing(14);

  auto* top = new QHBoxLayout();
  focus_badge_ = new QLabel(tr("FOCUS OFF"), overview);
  focus_badge_->setObjectName("CardValue");
  focus_btn_ = new QPushButton(tr("Turn Focus ON"), overview);
  focus_btn_->setObjectName("FocusBtn");
  focus_btn_->setProperty("focusOn", false);
  focus_btn_->setMinimumWidth(140);
  connect(focus_btn_, &QPushButton::clicked, this, [this]() { emit focusToggled(!focus_on_); });
  top->addWidget(focus_badge_, 0, Qt::AlignVCenter);
  top->addStretch(1);
  top->addWidget(focus_btn_, 0, Qt::AlignVCenter);
  ol->addLayout(top);

  auto* grid = new QGridLayout();
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(12);
  grid->addWidget(makeCard(tr("Bridge"), &bridge_value_, overview), 0, 0);
  grid->addWidget(makeCard(tr("Camera"), &camera_value_, overview), 0, 1);
  grid->addWidget(makeCard(tr("Phone"), &phone_value_, overview), 1, 0);
  grid->addWidget(makeCard(tr("Alarms"), &alarm_value_, overview), 1, 1);
  ol->addLayout(grid);

  auto* session_card = new QFrame(overview);
  session_card->setObjectName("Card");
  auto* session_lay = new QVBoxLayout(session_card);
  auto* session_title = new QLabel(tr("Last session"), session_card);
  session_title->setObjectName("CardTitle");
  session_line_ = new QLabel(tr("No sessions yet."), session_card);
  session_line_->setObjectName("CardValue");
  session_line_->setWordWrap(true);
  session_lay->addWidget(session_title);
  session_lay->addWidget(session_line_);
  ol->addWidget(session_card);

  auto* cam_row = new QHBoxLayout();
  auto* cam_label = new QLabel(tr("Camera device"), overview);
  cam_label->setObjectName("CardTitle");
  camera_combo_ = new QComboBox(overview);
  camera_combo_->setToolTip(
      tr("Pick the Mac webcam explicitly. Continuity/iPhone often appears as another index."));
  connect(camera_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            if (suppress_camera_device_signal_) return;
            const int dev = camera_combo_->currentData().toInt();
            emit cameraDeviceChanged(dev);
          });
  cam_row->addWidget(cam_label);
  cam_row->addWidget(camera_combo_, 1);
  ol->addLayout(cam_row);

  camera_check_ = new QCheckBox(tr("Camera monitoring (phone detection in background)"), overview);
  connect(camera_check_, &QCheckBox::toggled, this, &DashboardWindow::cameraToggled);
  ol->addWidget(camera_check_);

  auto* actions = new QHBoxLayout();
  auto* preview = new QPushButton(tr("Show camera preview"), overview);
  preview->setObjectName("Secondary");
  connect(preview, &QPushButton::clicked, this, &DashboardWindow::showCameraPreviewRequested);
  auto* copy = new QPushButton(tr("Copy bridge token"), overview);
  copy->setObjectName("Secondary");
  connect(copy, &QPushButton::clicked, this, &DashboardWindow::copyTokenRequested);
  auto* store_btn = new QPushButton(tr("Get extension…"), overview);
  store_btn->setObjectName("Secondary");
  connect(store_btn, &QPushButton::clicked, this, &DashboardWindow::openExtensionStoreRequested);
  auto* connect_btn = new QPushButton(tr("Connect browser"), overview);
  connect_btn->setObjectName("Primary");
  connect(connect_btn, &QPushButton::clicked, this, &DashboardWindow::connectBrowserRequested);
  actions->addWidget(preview);
  actions->addWidget(copy);
  actions->addStretch(1);
  actions->addWidget(store_btn);
  actions->addWidget(connect_btn);
  ol->addLayout(actions);
  ol->addStretch(1);
  stack_->addWidget(overview);

  // ===== Status page =====
  auto* status_page = new QWidget(stack_);
  auto* sl = new QVBoxLayout(status_page);
  auto* st_title = new QLabel(tr("Status"), status_page);
  st_title->setObjectName("Title");
  sl->addWidget(st_title);
  auto* st_sub = new QLabel(tr("Live system state (updates while this window is open)."), status_page);
  st_sub->setObjectName("Subtitle");
  sl->addWidget(st_sub);
  status_detail_ = new QPlainTextEdit(status_page);
  status_detail_->setReadOnly(true);
  status_detail_->setPlaceholderText(tr("Status will appear here…"));
  sl->addWidget(status_detail_, 1);
  stack_->addWidget(status_page);

  // ===== Stats page =====
  auto* stats_page = new QWidget(stack_);
  auto* pl = new QVBoxLayout(stats_page);
  auto* pl_title = new QLabel(tr("Last session"), stats_page);
  pl_title->setObjectName("Title");
  pl->addWidget(pl_title);
  auto* pl_sub = new QLabel(tr("Productivity summary for the most recent focus session."), stats_page);
  pl_sub->setObjectName("Subtitle");
  pl->addWidget(pl_sub);
  stats_detail_ = new QPlainTextEdit(stats_page);
  stats_detail_->setReadOnly(true);
  stats_detail_->setPlaceholderText(tr("No session data yet. Turn Focus ON, then OFF to end a session."));
  pl->addWidget(stats_detail_, 1);
  auto* refresh = new QPushButton(tr("Refresh"), stats_page);
  refresh->setObjectName("Secondary");
  connect(refresh, &QPushButton::clicked, this, &DashboardWindow::refreshStatsRequested);
  pl->addWidget(refresh, 0, Qt::AlignLeft);
  stack_->addWidget(stats_page);

  root->addWidget(stack_, 1);
  showPage(Page::Overview);
}

void DashboardWindow::setNavChecked(Page page) {
  auto apply = [](QPushButton* b, bool on) {
    if (!b) return;
    b->setProperty("active", on);
    b->style()->unpolish(b);
    b->style()->polish(b);
  };
  apply(nav_overview_, page == Page::Overview);
  apply(nav_status_, page == Page::Status);
  apply(nav_stats_, page == Page::Stats);
}

void DashboardWindow::showPage(Page page) {
  if (!stack_) return;
  stack_->setCurrentIndex(static_cast<int>(page));
  setNavChecked(page);
  raise();
  activateWindow();
}

void DashboardWindow::setStatus(bool focus_on, bool bridge_ok, int bridge_port, bool camera_on,
                               bool phone_visible, bool alarm_active, const QString& alarm_text,
                               const QString& last_session_line, int camera_device_index,
                               const QString& camera_device_label) {
  focus_on_ = focus_on;
  focus_badge_->setText(focus_on ? tr("FOCUS ON") : tr("FOCUS OFF"));
  focus_btn_->setText(focus_on ? tr("Turn Focus OFF") : tr("Turn Focus ON"));
  focus_btn_->setProperty("focusOn", focus_on);
  focus_btn_->style()->unpolish(focus_btn_);
  focus_btn_->style()->polish(focus_btn_);

  bridge_value_->setText(bridge_ok ? tr("Online · port %1").arg(bridge_port) : tr("Stopped"));
  QString cam_text = camera_on ? tr("Monitoring (background)") : tr("Off");
  if (!camera_device_label.isEmpty()) {
    cam_text += QStringLiteral("\n") + camera_device_label;
  } else {
    cam_text += tr("\nDevice index %1").arg(camera_device_index);
  }
  camera_value_->setText(cam_text);
  phone_value_->setText(phone_visible ? tr("In use / visible") : tr("Idle / desk"));
  if (alarm_active) {
    alarm_value_->setText(alarm_text.isEmpty() ? tr("Active") : alarm_text);
    alarm_value_->setStyleSheet("color: #F87171; font-size: 14px; font-weight: 700;");
  } else {
    alarm_value_->setText(tr("None"));
    alarm_value_->setStyleSheet("color: #34D399; font-size: 14px; font-weight: 600;");
  }
  session_line_->setText(last_session_line.isEmpty() ? tr("No sessions yet.") : last_session_line);

  const QSignalBlocker block(camera_check_);
  camera_check_->setChecked(camera_on);

  if (camera_combo_ && camera_combo_->count() > 0) {
    suppress_camera_device_signal_ = true;
    const int idx = camera_combo_->findData(camera_device_index);
    if (idx >= 0) camera_combo_->setCurrentIndex(idx);
    suppress_camera_device_signal_ = false;
  }
}

void DashboardWindow::setStatusDetail(const QString& text) {
  if (status_detail_) status_detail_->setPlainText(text);
}

void DashboardWindow::setStatsDetail(const QString& text) {
  if (stats_detail_) stats_detail_->setPlainText(text);
}

void DashboardWindow::setCameraDevices(const std::vector<std::pair<int, QString>>& devices,
                                      int selected_index) {
  if (!camera_combo_) return;
  suppress_camera_device_signal_ = true;
  camera_combo_->clear();
  if (devices.empty()) {
    camera_combo_->addItem(tr("No cameras detected — check Privacy → Camera"), selected_index);
  } else {
    for (const auto& d : devices) {
      camera_combo_->addItem(d.second, d.first);
    }
  }
  const int idx = camera_combo_->findData(selected_index);
  if (idx >= 0) camera_combo_->setCurrentIndex(idx);
  else if (camera_combo_->count() > 0) camera_combo_->setCurrentIndex(0);
  suppress_camera_device_signal_ = false;
}

} // namespace focusgaze
