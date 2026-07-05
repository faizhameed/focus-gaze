#include "ui/DashboardWindow.hpp"

#include "core/ProductivityStats.hpp"
#include "core/UrlClassifier.hpp"

#include <algorithm>

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace focusgaze {
namespace {

const char* kAppStyle = R"(
  QWidget#DashboardRoot {
    background: #0B0F14; color: #F1F5F9;
    font-family: Inter, "SF Pro Text", "Helvetica Neue", Arial, sans-serif;
  }
  QLabel#Title { color: #2DD4BF; font-size: 20px; font-weight: 700; }
  QLabel#Subtitle { color: #94A3B8; font-size: 12px; }
  QLabel#MetricBig { color: #2DD4BF; font-size: 36px; font-weight: 800; }
  QLabel#MetricLabel { color: #94A3B8; font-size: 11px; font-weight: 600; }
  QFrame#Card {
    background: #141A22; border: 1px solid #243041; border-radius: 12px;
  }
  QLabel#CardTitle { color: #94A3B8; font-size: 11px; font-weight: 600; letter-spacing: 0.06em; }
  QLabel#CardValue { color: #F1F5F9; font-size: 14px; font-weight: 600; }
  QPushButton {
    cursor: pointer;
  }
  QPushButton#Primary {
    background: #2DD4BF; color: #04352F; border: none; border-radius: 8px;
    padding: 10px 14px; font-weight: 700;
  }
  QPushButton#Primary:hover { background: #5EEAD4; }
  QPushButton#Primary:pressed { background: #14B8A6; padding-top: 11px; padding-bottom: 9px; }
  QPushButton#Secondary {
    background: transparent; color: #CBD5E1; border: 1px solid #243041;
    border-radius: 8px; padding: 9px 12px; font-weight: 600;
  }
  QPushButton#Secondary:hover { background: rgba(45, 212, 191, 0.12); border-color: #2DD4BF; color: #F1F5F9; }
  QPushButton#Secondary:pressed { background: rgba(45, 212, 191, 0.22); padding-top: 10px; padding-bottom: 8px; }
  QPushButton#Secondary:checked {
    background: rgba(45, 212, 191, 0.18); color: #2DD4BF; border-color: #2DD4BF;
  }
  QPushButton#Nav {
    background: transparent; color: #94A3B8; border: 1px solid transparent;
    border-radius: 8px; padding: 8px 12px; font-weight: 600; text-align: left;
  }
  QPushButton#Nav:hover { color: #F1F5F9; background: rgba(36, 48, 65, 0.55); }
  QPushButton#Nav:pressed { background: #243041; }
  QPushButton#Nav[active="true"] {
    background: #141A22; color: #2DD4BF; border: 1px solid #243041;
  }
  QPushButton#FocusBtn[focusOn="true"] { background: #2DD4BF; color: #04352F; border: none; border-radius: 8px; padding: 10px 14px; font-weight: 700; }
  QPushButton#FocusBtn[focusOn="true"]:hover { background: #5EEAD4; }
  QPushButton#FocusBtn[focusOn="true"]:pressed { background: #14B8A6; }
  QPushButton#FocusBtn[focusOn="false"] { background: #243041; color: #F1F5F9; border: none; border-radius: 8px; padding: 10px 14px; font-weight: 700; }
  QPushButton#FocusBtn[focusOn="false"]:hover { background: #334155; }
  QPushButton#FocusBtn[focusOn="false"]:pressed { background: #1E293B; }
  QProgressBar {
    background: #0B0F14; border: 1px solid #243041; border-radius: 6px; min-height: 18px;
    text-align: center; color: #F1F5F9; font-size: 11px; font-weight: 600;
  }
  QProgressBar::chunk { background: #2DD4BF; border-radius: 5px; }
  QCheckBox { color: #F1F5F9; spacing: 8px; }
  QComboBox, QSpinBox, QLineEdit {
    background: #141A22; color: #F1F5F9; border: 1px solid #243041;
    border-radius: 8px; padding: 6px 10px;
  }
  QPlainTextEdit {
    background: #141A22; color: #E2E8F0; border: 1px solid #243041;
    border-radius: 12px; padding: 10px; font-size: 12px;
  }
  QTableWidget {
    background: #141A22; color: #E2E8F0; border: 1px solid #243041;
    border-radius: 12px; gridline-color: #243041;
  }
  QHeaderView::section {
    background: #0B0F14; color: #94A3B8; border: none; padding: 6px; font-weight: 600;
  }
)";

QFrame* makeCard(const QString& title, QLabel** value_out, QWidget* parent) {
  auto* card = new QFrame(parent);
  card->setObjectName("Card");
  auto* lay = new QVBoxLayout(card);
  lay->setContentsMargins(14, 12, 14, 12);
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

QString fmtDur(std::int64_t sec) {
  if (sec < 0) sec = 0;
  const auto h = sec / 3600;
  const auto m = (sec % 3600) / 60;
  if (h > 0) return QString("%1h %2m").arg(h).arg(m);
  return QString("%1m").arg(m);
}

int pct(std::int64_t part, std::int64_t whole) {
  if (whole <= 0) return 0;
  return static_cast<int>((100.0 * static_cast<double>(part)) / static_cast<double>(whole) + 0.5);
}

} // namespace

DashboardWindow::DashboardWindow(QWidget* parent) : QWidget(parent) {
  setObjectName("DashboardRoot");
  setWindowTitle(tr("focusGaze"));
  setMinimumSize(820, 600);
  setStyleSheet(kAppStyle);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 16);
  root->setSpacing(14);

  auto* nav = new QVBoxLayout();
  auto* brand = new QLabel(tr("focusGaze"), this);
  brand->setObjectName("Title");
  auto* sub = new QLabel(tr("Local productivity guardian"), this);
  sub->setObjectName("Subtitle");
  nav->addWidget(brand);
  nav->addWidget(sub);
  nav->addSpacing(12);
  nav_overview_ = makeNav(tr("Overview"), this);
  nav_status_ = makeNav(tr("Status"), this);
  nav_stats_ = makeNav(tr("Statistics"), this);
  nav_settings_ = makeNav(tr("Settings"), this);
  connect(nav_overview_, &QPushButton::clicked, this, [this]() { showPage(Page::Overview); });
  connect(nav_status_, &QPushButton::clicked, this, [this]() { showPage(Page::Status); });
  connect(nav_stats_, &QPushButton::clicked, this, [this]() {
    emit refreshStatsRequested();
    showPage(Page::Stats);
  });
  connect(nav_settings_, &QPushButton::clicked, this, [this]() { showPage(Page::Settings); });
  nav->addWidget(nav_overview_);
  nav->addWidget(nav_status_);
  nav->addWidget(nav_stats_);
  nav->addWidget(nav_settings_);
  nav->addStretch(1);
  root->addLayout(nav, 0);

  stack_ = new QStackedWidget(this);
  stack_->addWidget(buildOverview());
  stack_->addWidget(buildStatus());
  stack_->addWidget(buildStats());
  stack_->addWidget(buildSettings());
  root->addWidget(stack_, 1);
  showPage(Page::Overview);
}

QWidget* DashboardWindow::buildOverview() {
  auto* overview = new QWidget(stack_);
  auto* ol = new QVBoxLayout(overview);
  ol->setSpacing(14);

  auto* top = new QHBoxLayout();
  focus_badge_ = new QLabel(tr("FOCUS OFF"), overview);
  focus_badge_->setObjectName("CardValue");
  focus_btn_ = new QPushButton(tr("Turn Focus ON"), overview);
  focus_btn_->setObjectName("FocusBtn");
  focus_btn_->setProperty("focusOn", false);
  connect(focus_btn_, &QPushButton::clicked, this, [this]() { emit focusToggled(!focus_on_); });
  top->addWidget(focus_badge_);
  top->addStretch(1);
  top->addWidget(focus_btn_);
  ol->addLayout(top);

  auto* grid = new QGridLayout();
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
  connect(camera_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
    if (suppress_camera_device_signal_) return;
    emit cameraDeviceChanged(camera_combo_->currentData().toInt());
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
  return overview;
}

QWidget* DashboardWindow::buildStatus() {
  auto* status_page = new QWidget(stack_);
  auto* sl = new QVBoxLayout(status_page);
  auto* st_title = new QLabel(tr("Status"), status_page);
  st_title->setObjectName("Title");
  sl->addWidget(st_title);
  auto* st_sub = new QLabel(
      tr("Live system state (~1s while this page is open). "
         "Phone pick-ups and URL events are recorded in the background even if you leave."),
      status_page);
  st_sub->setObjectName("Subtitle");
  st_sub->setWordWrap(true);
  sl->addWidget(st_sub);
  status_detail_ = new QPlainTextEdit(status_page);
  status_detail_->setReadOnly(true);
  sl->addWidget(status_detail_, 1);
  return status_page;
}

QWidget* DashboardWindow::buildStats() {
  auto* page = new QWidget(stack_);
  auto* lay = new QVBoxLayout(page);
  auto* title = new QLabel(tr("Statistics"), page);
  title->setObjectName("Title");
  lay->addWidget(title);

  auto* chips = new QHBoxLayout();
  window_group_ = new QButtonGroup(page);
  window_group_->setExclusive(true);
  auto addChip = [&](const QString& text, StatsWindow w, bool checked) {
    auto* b = new QPushButton(text, page);
    b->setObjectName("Secondary");
    b->setCheckable(true);
    b->setChecked(checked);
    b->setCursor(Qt::PointingHandCursor);
    window_group_->addButton(b);
    chips->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, w]() {
      stats_window_ = w;
      emit statsWindowChanged(w);
      emit refreshStatsRequested();
    });
  };
  addChip(tr("Last session"), StatsWindow::LastSession, true);
  addChip(tr("Today"), StatsWindow::Today, false);
  addChip(tr("Yesterday"), StatsWindow::Yesterday, false);
  addChip(tr("This week"), StatsWindow::ThisWeek, false);
  addChip(tr("Last week"), StatsWindow::LastWeek, false);
  addChip(tr("Last 7 days"), StatsWindow::Last7Days, false);
  addChip(tr("This month"), StatsWindow::Month, false);
  chips->addStretch(1);
  lay->addLayout(chips);

  window_label_ = new QLabel(tr("Window: Last session"), page);
  window_label_->setObjectName("Subtitle");
  lay->addWidget(window_label_);

  score_help_ = new QLabel(
      tr("Three metrics over Focus time: PRODUCTIVE (work allowlist + other non-blocked browsing), "
         "UNPRODUCTIVE SITES (blocklist), PHONE (camera). "
         "Score 0–100 = 100×productive/focus − 40×unproductive/focus − 30×phone/focus."),
      page);
  score_help_->setObjectName("Subtitle");
  score_help_->setWordWrap(true);
  lay->addWidget(score_help_);

  auto* metrics = new QHBoxLayout();
  auto addMetric = [&](QLabel** value, const QString& label) {
    auto* card = new QFrame(page);
    card->setObjectName("Card");
    auto* cl = new QVBoxLayout(card);
    auto* v = new QLabel(QStringLiteral("—"), card);
    v->setObjectName("MetricBig");
    auto* l = new QLabel(label, card);
    l->setObjectName("MetricLabel");
    cl->addWidget(v);
    cl->addWidget(l);
    *value = v;
    metrics->addWidget(card);
  };
  addMetric(&score_value_, tr("SCORE / 100"));
  addMetric(&duration_value_, tr("FOCUS TIME"));
  lay->addLayout(metrics);

  auto* bars_card = new QFrame(page);
  bars_card->setObjectName("Card");
  auto* bl = new QVBoxLayout(bars_card);
  auto addBar = [&](QLabel** lbl, QProgressBar** bar, const QString& name) {
    auto* row = new QHBoxLayout();
    auto* l = new QLabel(name, bars_card);
    l->setObjectName("CardTitle");
    l->setMinimumWidth(150);
    auto* p = new QProgressBar(bars_card);
    p->setRange(0, 100);
    p->setValue(0);
    p->setTextVisible(true);
    p->setFormat(QStringLiteral("%p%"));
    p->setMinimumHeight(22);
    row->addWidget(l);
    row->addWidget(p, 1);
    bl->addLayout(row);
    *lbl = l;
    *bar = p;
  };
  addBar(&lbl_productive_, &bar_productive_, tr("PRODUCTIVE"));
  addBar(&lbl_unproductive_, &bar_unproductive_, tr("UNPRODUCTIVE SITES"));
  addBar(&lbl_phone_, &bar_phone_, tr("PHONE"));
  lay->addWidget(bars_card);

  auto* week_title = new QLabel(tr("FOCUS QUALITY BY DAY (selected window)"), page);
  week_title->setObjectName("CardTitle");
  lay->addWidget(week_title);
  week_bars_host_ = new QWidget(page);
  week_bars_layout_ = new QHBoxLayout(week_bars_host_);
  week_bars_layout_->setContentsMargins(0, 0, 0, 0);
  lay->addWidget(week_bars_host_);

  sessions_table_ = new QTableWidget(0, 5, page);
  sessions_table_->setHorizontalHeaderLabels(
      {tr("Session"), tr("Duration"), tr("Score"), tr("Unproductive"), tr("Phone")});
  sessions_table_->horizontalHeader()->setStretchLastSection(true);
  sessions_table_->verticalHeader()->setVisible(false);
  sessions_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  sessions_table_->setSelectionMode(QAbstractItemView::NoSelection);
  sessions_table_->setMaximumHeight(180);
  lay->addWidget(sessions_table_);

  auto* actions = new QHBoxLayout();
  auto* refresh = new QPushButton(tr("Refresh"), page);
  refresh->setObjectName("Secondary");
  refresh->setCursor(Qt::PointingHandCursor);
  connect(refresh, &QPushButton::clicked, this, &DashboardWindow::refreshStatsRequested);
  actions->addWidget(refresh);
  actions->addStretch(1);
  lay->addLayout(actions);
  return page;
}


QWidget* DashboardWindow::buildSettings() {
  auto* page = new QWidget(stack_);
  auto* lay = new QVBoxLayout(page);
  auto* title = new QLabel(tr("Settings"), page);
  title->setObjectName("Title");
  lay->addWidget(title);

  auto* form = new QGridLayout();
  int r = 0;
  auto addLabel = [&](const QString& t) {
    auto* l = new QLabel(t, page);
    l->setObjectName("CardTitle");
    form->addWidget(l, r, 0);
  };

  addLabel(tr("RESUME FOCUS ON LAUNCH"));
  set_resume_ = new QCheckBox(tr("Restore open Focus session after restart"), page);
  form->addWidget(set_resume_, r++, 1);

  addLabel(tr("PRIVACY"));
  set_privacy_ = new QCheckBox(tr("Redact URL query strings when logging"), page);
  form->addWidget(set_privacy_, r++, 1);

  addLabel(tr("ALARM SOUND"));
  set_alarm_sound_ = new QCheckBox(tr("Play sound while sticky alarm is active"), page);
  form->addWidget(set_alarm_sound_, r++, 1);

  addLabel(tr("SOUND PRESET"));
  auto* sound_row = new QHBoxLayout();
  set_alarm_sound_name_ = new QComboBox(page);
  for (const char* name :
       {"default", "sosumi", "basso", "blow", "bottle", "frog", "funk", "glass", "hero", "morse",
        "ping", "pop", "purr", "submarine", "tink"}) {
    set_alarm_sound_name_->addItem(QString::fromUtf8(name));
  }
  auto* test_sound = new QPushButton(tr("Test alarm sound"), page);
  test_sound->setObjectName("Secondary");
  connect(test_sound, &QPushButton::clicked, this, &DashboardWindow::testAlarmSoundRequested);
  sound_row->addWidget(set_alarm_sound_name_, 1);
  sound_row->addWidget(test_sound);
  form->addLayout(sound_row, r++, 1);

  addLabel(tr("PHONE THRESHOLD (SECONDS)"));
  set_phone_threshold_ = new QSpinBox(page);
  set_phone_threshold_->setRange(5, 3600);
  set_phone_threshold_->setValue(60);
  set_phone_threshold_->setToolTip(tr("Cumulative in-use seconds inside the rolling window to alarm"));
  form->addWidget(set_phone_threshold_, r++, 1);

  addLabel(tr("PHONE WINDOW (MINUTES)"));
  set_phone_window_min_ = new QSpinBox(page);
  set_phone_window_min_->setRange(1, 240);
  set_phone_window_min_->setValue(30);
  form->addWidget(set_phone_window_min_, r++, 1);

  addLabel(tr("CAMERA DEVICE INDEX"));
  set_camera_index_ = new QSpinBox(page);
  set_camera_index_->setRange(0, 16);
  form->addWidget(set_camera_index_, r++, 1);

  addLabel(tr("BRIDGE PORT"));
  set_bridge_port_ = new QSpinBox(page);
  set_bridge_port_->setRange(1024, 65535);
  set_bridge_port_->setValue(18765);
  form->addWidget(set_bridge_port_, r++, 1);

  addLabel(tr("BRIDGE TOKEN"));
  set_token_ = new QLineEdit(page);
  set_token_->setEchoMode(QLineEdit::Password);
  set_token_->setPlaceholderText(tr("Leave blank to keep current token"));
  form->addWidget(set_token_, r++, 1);

  lay->addLayout(form);

  auto* bl_lab = new QLabel(
      tr("BLOCKLIST (one domain per line — sitename.com also matches www / m / subdomains)"), page);
  bl_lab->setObjectName("CardTitle");
  lay->addWidget(bl_lab);
  set_blocklist_ = new QPlainTextEdit(page);
  set_blocklist_->setMaximumHeight(100);
  set_blocklist_->setPlaceholderText(tr("example.com\ninstagram.com"));
  lay->addWidget(set_blocklist_);

  auto* al_lab = new QLabel(tr("ALLOWLIST (one domain per line)"), page);
  al_lab->setObjectName("CardTitle");
  lay->addWidget(al_lab);
  set_allowlist_ = new QPlainTextEdit(page);
  set_allowlist_->setMaximumHeight(80);
  lay->addWidget(set_allowlist_);

  auto* actions = new QHBoxLayout();
  auto* save = new QPushButton(tr("Save settings"), page);
  save->setObjectName("Primary");
  connect(save, &QPushButton::clicked, this, &DashboardWindow::saveSettingsRequested);
  auto* reset = new QPushButton(tr("Reset defaults"), page);
  reset->setObjectName("Secondary");
  connect(reset, &QPushButton::clicked, this, &DashboardWindow::resetSettingsRequested);
  auto* copy = new QPushButton(tr("Copy token"), page);
  copy->setObjectName("Secondary");
  connect(copy, &QPushButton::clicked, this, &DashboardWindow::copyTokenRequested);
  actions->addWidget(save);
  actions->addWidget(reset);
  actions->addWidget(copy);
  actions->addStretch(1);
  lay->addLayout(actions);
  lay->addStretch(1);
  return page;
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
  apply(nav_settings_, page == Page::Settings);
}

void DashboardWindow::showPage(Page page) {
  if (!stack_) return;
  stack_->setCurrentIndex(static_cast<int>(page));
  setNavChecked(page);
  raise();
  activateWindow();
}

DashboardWindow::Page DashboardWindow::currentPage() const {
  if (!stack_) return Page::Overview;
  const int idx = stack_->currentIndex();
  if (idx < 0 || idx > static_cast<int>(Page::Settings)) return Page::Overview;
  return static_cast<Page>(idx);
}

void DashboardWindow::setStatus(bool focus_on, bool bridge_ok, int bridge_port, bool camera_on,
                               bool phone_visible, bool alarm_active, const QString& alarm_text,
                               const QString& last_session_line, int camera_device_index,
                               const QString& camera_device_label, const QString& phone_detail) {
  focus_on_ = focus_on;
  focus_badge_->setText(focus_on ? tr("FOCUS ON") : tr("FOCUS OFF"));
  focus_btn_->setText(focus_on ? tr("Turn Focus OFF") : tr("Turn Focus ON"));
  focus_btn_->setProperty("focusOn", focus_on);
  focus_btn_->style()->unpolish(focus_btn_);
  focus_btn_->style()->polish(focus_btn_);

  bridge_value_->setText(bridge_ok ? tr("Online · port %1").arg(bridge_port) : tr("Stopped"));
  QString cam_text = camera_on ? tr("Monitoring (background)") : tr("Off");
  cam_text += QStringLiteral("\n") +
              (camera_device_label.isEmpty() ? tr("Device index %1").arg(camera_device_index)
                                            : camera_device_label);
  camera_value_->setText(cam_text);
  if (!phone_detail.isEmpty()) {
    phone_value_->setText((phone_visible ? tr("In use") : tr("Idle")) + QStringLiteral("\n") +
                          phone_detail);
  } else {
    phone_value_->setText(phone_visible ? tr("In use / visible") : tr("Idle / desk"));
  }
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

void DashboardWindow::setStatistics(const WindowStats& window,
                                   const std::vector<DailyStats>& day_chart) {
  auto applyBar = [](QProgressBar* bar, QLabel* lbl, const QString& title, int percent,
                     const QString& extra = {}) {
    if (!bar) return;
    percent = std::clamp(percent, 0, 100);
    if (bar->value() == percent) bar->setValue(percent == 0 ? 1 : percent - 1);
    bar->setValue(percent);
    bar->setFormat(QStringLiteral("%1%").arg(percent));
    bar->update();
    if (lbl) {
      lbl->setText(extra.isEmpty() ? QStringLiteral("%1  %2%").arg(title).arg(percent)
                                   : QStringLiteral("%1  %2% · %3").arg(title).arg(percent).arg(extra));
    }
  };

  if (window_label_) {
    window_label_->setText(tr("Window: %1 · %2 session(s)")
                               .arg(QString::fromStdString(window.label))
                               .arg(window.session_count));
  }

  if (window.session_count > 0 || window.focus_seconds > 0) {
    score_value_->setText(QString::number(window.score, 'f', 1));
    duration_value_->setText(fmtDur(window.focus_seconds));
    const auto fs = std::max<std::int64_t>(1, window.focus_seconds);
    applyBar(bar_productive_, lbl_productive_, tr("PRODUCTIVE"),
             pct(window.productive_seconds, fs), fmtDur(window.productive_seconds));
    applyBar(bar_unproductive_, lbl_unproductive_, tr("UNPRODUCTIVE SITES"),
             pct(window.unproductive_seconds, fs), fmtDur(window.unproductive_seconds));
    applyBar(bar_phone_, lbl_phone_, tr("PHONE"), pct(window.phone_seconds, fs),
             fmtDur(window.phone_seconds));
  } else {
    score_value_->setText(QStringLiteral("—"));
    duration_value_->setText(QStringLiteral("—"));
    applyBar(bar_productive_, lbl_productive_, tr("PRODUCTIVE"), 0, fmtDur(0));
    applyBar(bar_unproductive_, lbl_unproductive_, tr("UNPRODUCTIVE SITES"), 0, fmtDur(0));
    applyBar(bar_phone_, lbl_phone_, tr("PHONE"), 0, fmtDur(0));
  }

  while (QLayoutItem* it = week_bars_layout_->takeAt(0)) {
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }
  for (const auto& d : day_chart) {
    auto* col = new QWidget(week_bars_host_);
    auto* cl = new QVBoxLayout(col);
    cl->setContentsMargins(2, 0, 2, 0);
    auto* bar = new QProgressBar(col);
    bar->setOrientation(Qt::Vertical);
    bar->setRange(0, 100);
    // Productive for the day = allowlist browser time + neutral (non-blocklist) share of focus.
    const std::int64_t prod_day =
        d.productive_seconds > 0
            ? d.productive_seconds
            : std::max<std::int64_t>(0, d.focus_seconds - d.social_seconds - d.phone_seconds);
    const int sc = std::clamp(
        static_cast<int>(ProductivityStats::computeScoreParts(d.focus_seconds, prod_day,
                                                              d.social_seconds, d.phone_seconds) +
                         0.5),
        0, 100);
    bar->setValue(sc);
    bar->setFixedWidth(28);
    bar->setFixedHeight(90);
    bar->setTextVisible(true);
    bar->setFormat(QString::number(sc));
    bar->setToolTip(tr("%1\nFocus %2 · Unproductive sites %3 · Phone %4")
                        .arg(QString::fromStdString(d.day))
                        .arg(fmtDur(d.focus_seconds))
                        .arg(fmtDur(d.social_seconds))
                        .arg(fmtDur(d.phone_seconds)));
    auto* day = new QLabel(QString::fromStdString(d.day).mid(5), col);
    day->setObjectName("MetricLabel");
    day->setAlignment(Qt::AlignCenter);
    cl->addWidget(bar, 0, Qt::AlignHCenter);
    cl->addWidget(day);
    week_bars_layout_->addWidget(col);
  }
  week_bars_layout_->addStretch(1);

  const auto& recent = window.sessions;
  sessions_table_->setRowCount(static_cast<int>(recent.size()));
  for (int i = 0; i < static_cast<int>(recent.size()); ++i) {
    const auto& s = recent[static_cast<std::size_t>(i)];
    sessions_table_->setItem(i, 0, new QTableWidgetItem(QString::number(s.session_id)));
    sessions_table_->setItem(i, 1, new QTableWidgetItem(fmtDur(s.focus_seconds)));
    sessions_table_->setItem(i, 2, new QTableWidgetItem(QString::number(s.score, 'f', 1)));
    sessions_table_->setItem(i, 3, new QTableWidgetItem(fmtDur(s.social_seconds)));
    sessions_table_->setItem(i, 4, new QTableWidgetItem(fmtDur(s.phone_seconds)));
  }
  if (score_value_) score_value_->update();
  if (duration_value_) duration_value_->update();
}


void DashboardWindow::setCameraDevices(const std::vector<std::pair<int, QString>>& devices,
                                      int selected_index) {
  if (!camera_combo_) return;
  suppress_camera_device_signal_ = true;
  camera_combo_->clear();
  if (devices.empty()) {
    camera_combo_->addItem(tr("No cameras detected"), selected_index);
  } else {
    for (const auto& d : devices) camera_combo_->addItem(d.second, d.first);
  }
  const int idx = camera_combo_->findData(selected_index);
  if (idx >= 0) camera_combo_->setCurrentIndex(idx);
  suppress_camera_device_signal_ = false;
}

void DashboardWindow::loadSettingsForm(const Settings& s) {
  set_resume_->setChecked(s.resume_focus_on_launch);
  set_privacy_->setChecked(s.privacy_redact);
  set_alarm_sound_->setChecked(s.alarm_sound_enabled);
  {
    const QString sound = QString::fromStdString(s.alarm_sound);
    int idx = set_alarm_sound_name_->findText(sound, Qt::MatchFixedString);
    if (idx < 0) {
      set_alarm_sound_name_->addItem(sound);
      idx = set_alarm_sound_name_->findText(sound);
    }
    if (idx >= 0) set_alarm_sound_name_->setCurrentIndex(idx);
  }
  set_phone_threshold_->setValue(static_cast<int>(s.phone_threshold_seconds));
  set_phone_window_min_->setValue(static_cast<int>(s.phone_window_seconds / 60));
  set_camera_index_->setValue(s.camera_device_index);
  set_bridge_port_->setValue(s.bridge_port);
  set_token_->clear();
  set_token_->setPlaceholderText(tr("Current token set (%1 chars) — leave blank to keep")
                                     .arg(static_cast<int>(s.bridge_token.size())));
  QString bl;
  for (const auto& d : s.blocklist) bl += QString::fromStdString(d) + "\n";
  set_blocklist_->setPlainText(bl.trimmed());
  QString al;
  for (const auto& d : s.allowlist) al += QString::fromStdString(d) + "\n";
  set_allowlist_->setPlainText(al.trimmed());
}

Settings DashboardWindow::readSettingsForm(const Settings& base) const {
  Settings s = base;
  s.resume_focus_on_launch = set_resume_->isChecked();
  s.privacy_redact = set_privacy_->isChecked();
  s.alarm_sound_enabled = set_alarm_sound_->isChecked();
  s.alarm_sound = set_alarm_sound_name_->currentText().trimmed().toStdString();
  if (s.alarm_sound.empty()) s.alarm_sound = "default";
  s.phone_threshold_seconds = set_phone_threshold_->value();
  s.phone_window_seconds = static_cast<std::int64_t>(set_phone_window_min_->value()) * 60;
  s.camera_device_index = set_camera_index_->value();
  s.bridge_port = set_bridge_port_->value();
  const QString tok = set_token_->text().trimmed();
  if (!tok.isEmpty()) s.bridge_token = tok.toStdString();

  auto parseLines = [](const QString& text) {
    std::vector<std::string> out;
    for (const QString& line : text.split('\n')) {
      const QString t = line.trimmed();
      if (t.isEmpty() || t.startsWith('#')) continue;
      out.push_back(t.toStdString());
    }
    // Normalize sitename.com / www / full URLs into bare domains (matches variants in backend).
    return UrlClassifier::normalizeDomainList(out);
  };
  s.blocklist = parseLines(set_blocklist_->toPlainText());
  s.allowlist = parseLines(set_allowlist_->toPlainText());
  return s;
}

} // namespace focusgaze
