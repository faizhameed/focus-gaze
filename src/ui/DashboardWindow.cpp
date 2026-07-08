#include "ui/DashboardWindow.hpp"

#include "core/UrlClassifier.hpp"
#include "ui/StatsViewModel.hpp"

#include <algorithm>

#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace focusgaze {
namespace {

// 8pt spacing scale: 8 / 12 / 16 / 24 / 32
const char* kAppStyle = R"(
  QWidget#DashboardRoot {
    background: #0B0F14; color: #F1F5F9;
    font-family: Inter, "SF Pro Text", "Helvetica Neue", Arial, sans-serif;
  }
  QWidget#NavRail {
    background: #0B0F14;
    border-right: 1px solid #1E293B;
  }
  QLabel#Title { color: #F1F5F9; font-size: 22px; font-weight: 700; letter-spacing: -0.02em; }
  QLabel#Brand { color: #2DD4BF; font-size: 15px; font-weight: 800; letter-spacing: 0.04em; }
  QLabel#Subtitle { color: #64748B; font-size: 13px; font-weight: 500; }
  QLabel#MetricBig {
    color: #F1F5F9; font-size: 40px; font-weight: 700; letter-spacing: -0.03em;
  }
  QLabel#MetricBig[accent="true"] { color: #2DD4BF; }
  QLabel#MetricLabel {
    color: #64748B; font-size: 11px; font-weight: 600; letter-spacing: 0.1em;
  }
  QLabel#CardTitle {
    color: #64748B; font-size: 11px; font-weight: 600; letter-spacing: 0.08em;
  }
  QLabel#CardValue { color: #F1F5F9; font-size: 16px; font-weight: 600; }
  QFrame#Card {
    background: #141A22; border: 1px solid #1E293B; border-radius: 16px;
  }
  QFrame#ChipGroup {
    background: #141A22; border: 1px solid #1E293B; border-radius: 12px;
  }
  QPushButton#Primary {
    background: #2DD4BF; color: #04352F; border: none; border-radius: 12px;
    padding: 11px 18px; font-weight: 700; font-size: 13px;
  }
  QPushButton#Primary:hover { background: #5EEAD4; }
  QPushButton#Secondary {
    background: transparent; color: #94A3B8; border: 1px solid #1E293B;
    border-radius: 12px; padding: 10px 14px; font-weight: 600; font-size: 13px;
  }
  QPushButton#Secondary:hover { color: #F1F5F9; border-color: #334155; background: #141A22; }
  QPushButton#Chip {
    background: transparent; color: #64748B; border: none; border-radius: 8px;
    padding: 8px 14px; font-weight: 600; font-size: 12px; min-height: 20px;
  }
  QPushButton#Chip:hover { color: #E2E8F0; }
  QPushButton#Chip:checked {
    color: #04352F; background: #2DD4BF; font-weight: 700;
  }
  QPushButton#Nav {
    background: transparent; color: #64748B; border: none; border-radius: 10px;
    padding: 11px 14px; font-weight: 600; font-size: 13px; text-align: left;
  }
  QPushButton#Nav:hover { color: #F1F5F9; background: #141A22; }
  QPushButton#Nav[active="true"] {
    color: #2DD4BF; background: #141A22;
  }
  QPushButton#FocusBtn[focusOn="true"] {
    background: #2DD4BF; color: #04352F; border: none; border-radius: 12px;
    padding: 11px 20px; font-weight: 700; font-size: 13px;
  }
  QPushButton#FocusBtn[focusOn="false"] {
    background: #1E293B; color: #F1F5F9; border: none; border-radius: 12px;
    padding: 11px 20px; font-weight: 700; font-size: 13px;
  }
  QPushButton#FocusBtn[focusOn="false"]:hover { background: #334155; }
  QProgressBar {
    background: #0B0F14; border: none; border-radius: 5px;
    min-height: 8px; max-height: 8px; text-align: center; color: transparent;
  }
  QProgressBar::chunk { background: #2DD4BF; border-radius: 5px; }
  QProgressBar#BarBad::chunk { background: #F87171; }
  QProgressBar#BarPhone::chunk { background: #A78BFA; }
  QProgressBar#DayBar {
    background: #0B0F14; border: none; border-radius: 6px;
    min-width: 28px; max-width: 28px;
  }
  QProgressBar#DayBar::chunk { background: #2DD4BF; border-radius: 6px; }
  QCheckBox { color: #94A3B8; spacing: 10px; font-size: 13px; }
  QCheckBox::indicator {
    width: 16px; height: 16px; border-radius: 4px;
    border: 1px solid #334155; background: #0B0F14;
  }
  QCheckBox::indicator:checked { background: #2DD4BF; border-color: #2DD4BF; }
  QComboBox, QSpinBox, QLineEdit, QDateEdit {
    background: #0B0F14; color: #F1F5F9; border: 1px solid #1E293B;
    border-radius: 10px; padding: 8px 12px; min-height: 20px; font-size: 13px;
  }
  QComboBox:hover, QDateEdit:hover { border-color: #334155; }
  QDateEdit::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 30px; border-left: 1px solid #1E293B;
  }
  QDateEdit::down-arrow {
    width: 0; height: 0;
    border-left: 4px solid transparent; border-right: 4px solid transparent;
    border-top: 6px solid #2DD4BF; margin-right: 8px;
  }
  QCalendarWidget { background: #141A22; color: #F1F5F9; border: 1px solid #1E293B; }
  QCalendarWidget QToolButton {
    color: #F1F5F9; background: #0B0F14; border: 1px solid #1E293B;
    border-radius: 8px; padding: 6px 10px;
  }
  QCalendarWidget QAbstractItemView:enabled {
    color: #E2E8F0; selection-background-color: #2DD4BF; selection-color: #04352F;
  }
  QFrame#DatePickerPanel {
    background: #141A22; border: 1px solid #1E293B; border-radius: 14px;
  }
  QToolButton#DatePopupBtn {
    background: #0B0F14; color: #2DD4BF; border: 1px solid #1E293B;
    border-radius: 10px; padding: 8px 12px; min-width: 36px; min-height: 20px;
  }
  QToolButton#DatePopupBtn:hover { border-color: #2DD4BF; background: rgba(45,212,191,0.1); }
  QPlainTextEdit {
    background: #0B0F14; color: #94A3B8; border: 1px solid #1E293B;
    border-radius: 12px; padding: 12px 14px; font-size: 12px;
    font-family: "SF Mono", Menlo, Consolas, monospace;
  }
  QPlainTextEdit#SessionsList {
    background: transparent; border: none; padding: 0;
    color: #94A3B8; font-size: 12px;
  }
)";

QFrame* makeCard(const QString& title, QLabel** value_out, QWidget* parent) {
  auto* card = new QFrame(parent);
  card->setObjectName("Card");
  auto* lay = new QVBoxLayout(card);
  lay->setContentsMargins(20, 18, 20, 18);
  lay->setSpacing(10);
  auto* t = new QLabel(title, card);
  t->setObjectName("CardTitle");
  auto* v = new QLabel(QStringLiteral("—"), card);
  v->setObjectName("CardValue");
  v->setWordWrap(true);
  lay->addWidget(t);
  lay->addStretch(1);
  lay->addWidget(v);
  *value_out = v;
  return card;
}

QPushButton* makeNav(const QString& text, QWidget* parent) {
  auto* b = new QPushButton(text, parent);
  b->setObjectName("Nav");
  b->setCursor(Qt::PointingHandCursor);
  b->setProperty("active", false);
  b->setMinimumHeight(40);
  return b;
}

QVBoxLayout* pageLayout(QWidget* page) {
  auto* lay = new QVBoxLayout(page);
  lay->setContentsMargins(8, 4, 8, 8);
  lay->setSpacing(0);
  return lay;
}

} // namespace

DashboardWindow::DashboardWindow(QWidget* parent) : QWidget(parent) {
  setObjectName("DashboardRoot");
  setWindowTitle(tr("focusGaze"));
  setMinimumSize(880, 600);
  resize(960, 680);
  setStyleSheet(kAppStyle);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // Fixed-width side rail (Stitch-style)
  auto* rail = new QWidget(this);
  rail->setObjectName("NavRail");
  rail->setFixedWidth(168);
  auto* nav = new QVBoxLayout(rail);
  nav->setContentsMargins(16, 24, 16, 24);
  nav->setSpacing(4);
  auto* brand = new QLabel(tr("focusGaze"), rail);
  brand->setObjectName("Brand");
  nav->addWidget(brand);
  nav->addSpacing(28);
  nav_overview_ = makeNav(tr("Home"), rail);
  nav_status_ = makeNav(tr("Status"), rail);
  nav_stats_ = makeNav(tr("Stats"), rail);
  nav_settings_ = makeNav(tr("Settings"), rail);
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
  root->addWidget(rail, 0);

  auto* content = new QWidget(this);
  auto* content_lay = new QVBoxLayout(content);
  content_lay->setContentsMargins(32, 28, 36, 28);
  content_lay->setSpacing(0);
  stack_ = new QStackedWidget(content);
  stack_->addWidget(buildOverview());
  stack_->addWidget(buildStatus());
  stack_->addWidget(buildStats());
  stack_->addWidget(buildSettings());
  content_lay->addWidget(stack_, 1);
  root->addWidget(content, 1);
  showPage(Page::Overview);
}

QWidget* DashboardWindow::buildOverview() {
  auto* page = new QWidget(stack_);
  auto* lay = pageLayout(page);
  lay->setSpacing(20);

  auto* top = new QHBoxLayout();
  top->setSpacing(16);
  focus_badge_ = new QLabel(tr("Off"), page);
  focus_badge_->setObjectName("Title");
  focus_btn_ = new QPushButton(tr("Start focus"), page);
  focus_btn_->setObjectName("FocusBtn");
  focus_btn_->setProperty("focusOn", false);
  focus_btn_->setCursor(Qt::PointingHandCursor);
  connect(focus_btn_, &QPushButton::clicked, this, [this]() { emit focusToggled(!focus_on_); });
  top->addWidget(focus_badge_, 0, Qt::AlignVCenter);
  top->addStretch(1);
  top->addWidget(focus_btn_, 0, Qt::AlignVCenter);
  lay->addLayout(top);

  auto* grid = new QGridLayout();
  grid->setHorizontalSpacing(16);
  grid->setVerticalSpacing(16);
  grid->addWidget(makeCard(tr("BRIDGE"), &bridge_value_, page), 0, 0);
  grid->addWidget(makeCard(tr("CAMERA"), &camera_value_, page), 0, 1);
  grid->addWidget(makeCard(tr("PHONE"), &phone_value_, page), 1, 0);
  grid->addWidget(makeCard(tr("ALARM"), &alarm_value_, page), 1, 1);
  for (int c = 0; c < 2; ++c) grid->setColumnStretch(c, 1);
  lay->addLayout(grid);

  auto* session_card = new QFrame(page);
  session_card->setObjectName("Card");
  auto* session_lay = new QVBoxLayout(session_card);
  session_lay->setContentsMargins(20, 18, 20, 18);
  session_lay->setSpacing(10);
  auto* st = new QLabel(tr("LAST SESSION"), session_card);
  st->setObjectName("CardTitle");
  session_line_ = new QLabel(tr("No sessions yet"), session_card);
  session_line_->setObjectName("CardValue");
  session_line_->setWordWrap(true);
  session_lay->addWidget(st);
  session_lay->addWidget(session_line_);
  lay->addWidget(session_card);

  auto* tools = new QFrame(page);
  tools->setObjectName("Card");
  auto* tools_lay = new QVBoxLayout(tools);
  tools_lay->setContentsMargins(20, 16, 20, 16);
  tools_lay->setSpacing(14);
  auto* cam_row = new QHBoxLayout();
  cam_row->setSpacing(14);
  camera_check_ = new QCheckBox(tr("Watch for phone"), tools);
  connect(camera_check_, &QCheckBox::toggled, this, &DashboardWindow::cameraToggled);
  camera_combo_ = new QComboBox(tools);
  camera_combo_->setMinimumWidth(180);
  connect(camera_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
    if (suppress_camera_device_signal_) return;
    emit cameraDeviceChanged(camera_combo_->currentData().toInt());
  });
  cam_row->addWidget(camera_check_);
  cam_row->addStretch(1);
  cam_row->addWidget(camera_combo_, 0);
  tools_lay->addLayout(cam_row);

  auto* actions = new QHBoxLayout();
  actions->setSpacing(12);
  auto* preview = new QPushButton(tr("Preview camera"), tools);
  preview->setObjectName("Secondary");
  preview->setCursor(Qt::PointingHandCursor);
  connect(preview, &QPushButton::clicked, this, &DashboardWindow::showCameraPreviewRequested);
  auto* connect_btn = new QPushButton(tr("Connect browser"), tools);
  connect_btn->setObjectName("Primary");
  connect_btn->setCursor(Qt::PointingHandCursor);
  connect(connect_btn, &QPushButton::clicked, this, &DashboardWindow::connectBrowserRequested);
  actions->addWidget(preview);
  actions->addStretch(1);
  actions->addWidget(connect_btn);
  tools_lay->addLayout(actions);
  lay->addWidget(tools);
  lay->addStretch(1);
  return page;
}

QWidget* DashboardWindow::buildStatus() {
  auto* page = new QWidget(stack_);
  auto* lay = pageLayout(page);
  lay->setSpacing(20);
  auto* title = new QLabel(tr("Status"), page);
  title->setObjectName("Title");
  lay->addWidget(title);
  status_detail_ = new QPlainTextEdit(page);
  status_detail_->setReadOnly(true);
  status_detail_->setFrameShape(QFrame::NoFrame);
  lay->addWidget(status_detail_, 1);
  return page;
}

QWidget* DashboardWindow::buildStats() {
  auto* page = new QWidget(stack_);
  auto* lay = pageLayout(page);
  lay->setSpacing(20);

  // Header: title + pill chip group
  auto* header = new QHBoxLayout();
  header->setSpacing(16);
  auto* title = new QLabel(tr("Stats"), page);
  title->setObjectName("Title");
  header->addWidget(title, 0, Qt::AlignVCenter);
  header->addStretch(1);

  auto* chip_wrap = new QFrame(page);
  chip_wrap->setObjectName("ChipGroup");
  auto* chip_inner = new QHBoxLayout(chip_wrap);
  chip_inner->setContentsMargins(4, 4, 4, 4);
  chip_inner->setSpacing(2);

  window_group_ = new QButtonGroup(page);
  window_group_->setExclusive(true);
  auto addChip = [&](const QString& text, StatsWindow w, bool on) -> QPushButton* {
    auto* b = new QPushButton(text, chip_wrap);
    b->setObjectName("Chip");
    b->setCheckable(true);
    b->setChecked(on);
    b->setCursor(Qt::PointingHandCursor);
    window_group_->addButton(b);
    chip_inner->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, w]() { selectStatsWindow(w); });
    return b;
  };
  addChip(tr("Today"), StatsWindow::Today, false);
  addChip(tr("Week"), StatsWindow::Last7Days, false);
  addChip(tr("Session"), StatsWindow::LastSession, true);
  stats_custom_chip_ = addChip(tr("Custom"), StatsWindow::Custom, false);
  header->addWidget(chip_wrap, 0, Qt::AlignVCenter);
  lay->addLayout(header);

  // Custom range row (hidden)
  date_picker_panel_ = new QFrame(page);
  date_picker_panel_->setObjectName("DatePickerPanel");
  date_picker_panel_->setVisible(false);
  auto* dp = new QHBoxLayout(date_picker_panel_);
  dp->setContentsMargins(16, 12, 16, 12);
  dp->setSpacing(10);
  date_from_ = new QDateEdit(date_picker_panel_);
  date_to_ = new QDateEdit(date_picker_panel_);
  for (QDateEdit* edit : {date_from_, date_to_}) {
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    edit->setCalendarPopup(true);
    edit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    edit->setMinimumDate(QDate(2020, 1, 1));
    edit->setMaximumDate(QDate::currentDate());
    edit->setDate(QDate::currentDate());
    auto* cal = new QCalendarWidget(edit);
    cal->setGridVisible(true);
    cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    cal->setMinimumDate(QDate(2020, 1, 1));
    cal->setMaximumDate(QDate::currentDate());
    edit->setCalendarWidget(cal);
  }
  auto* from_btn = new QToolButton(date_picker_panel_);
  from_btn->setObjectName("DatePopupBtn");
  from_btn->setText(QString::fromUtf8("📅"));
  from_btn->setCursor(Qt::PointingHandCursor);
  connect(from_btn, &QToolButton::clicked, this, [this]() { openCalendarPopupFor(true); });
  auto* to_btn = new QToolButton(date_picker_panel_);
  to_btn->setObjectName("DatePopupBtn");
  to_btn->setText(QString::fromUtf8("📅"));
  to_btn->setCursor(Qt::PointingHandCursor);
  connect(to_btn, &QToolButton::clicked, this, [this]() { openCalendarPopupFor(false); });
  auto* apply = new QPushButton(tr("Apply"), date_picker_panel_);
  apply->setObjectName("Primary");
  apply->setCursor(Qt::PointingHandCursor);
  connect(apply, &QPushButton::clicked, this, &DashboardWindow::applyCustomDateRangeFromEditors);
  dp->addWidget(date_from_, 1);
  dp->addWidget(from_btn);
  dp->addWidget(date_to_, 1);
  dp->addWidget(to_btn);
  dp->addWidget(apply);
  lay->addWidget(date_picker_panel_);
  connect(date_from_, &QDateEdit::dateChanged, this, &DashboardWindow::onFromDateEdited);
  connect(date_to_, &QDateEdit::dateChanged, this, &DashboardWindow::onToDateEdited);

  window_label_ = new QLabel(tr("Session"), page);
  window_label_->setObjectName("Subtitle");
  lay->addWidget(window_label_);
  lay->addSpacing(4);

  // Hero card: score + focus
  auto* hero_card = new QFrame(page);
  hero_card->setObjectName("Card");
  auto* hero_lay = new QHBoxLayout(hero_card);
  hero_lay->setContentsMargins(28, 24, 28, 24);
  hero_lay->setSpacing(48);
  auto addHero = [&](QLabel** value, const QString& label, bool accent) {
    auto* col = new QVBoxLayout();
    col->setSpacing(8);
    auto* v = new QLabel(QStringLiteral("—"), hero_card);
    v->setObjectName("MetricBig");
    v->setProperty("accent", accent);
    auto* l = new QLabel(label, hero_card);
    l->setObjectName("MetricLabel");
    col->addWidget(v);
    col->addWidget(l);
    *value = v;
    hero_lay->addLayout(col);
  };
  addHero(&score_value_, tr("SCORE"), true);
  addHero(&duration_value_, tr("FOCUS TIME"), false);
  hero_lay->addStretch(1);
  lay->addWidget(hero_card);

  // Breakdown card
  auto* break_card = new QFrame(page);
  break_card->setObjectName("Card");
  auto* break_lay = new QVBoxLayout(break_card);
  break_lay->setContentsMargins(24, 20, 24, 20);
  break_lay->setSpacing(16);
  auto* break_title = new QLabel(tr("BREAKDOWN"), break_card);
  break_title->setObjectName("CardTitle");
  break_lay->addWidget(break_title);

  auto addBar = [&](QLabel** lbl, QProgressBar** bar, const QString& name, const char* obj) {
    auto* row = new QHBoxLayout();
    row->setSpacing(14);
    auto* l = new QLabel(name, break_card);
    l->setObjectName("Subtitle");
    l->setFixedWidth(72);
    auto* p = new QProgressBar(break_card);
    p->setObjectName(QString::fromUtf8(obj));
    p->setRange(0, 100);
    p->setValue(0);
    p->setTextVisible(false);
    auto* val = new QLabel(QStringLiteral("0m"), break_card);
    val->setObjectName("Subtitle");
    val->setFixedWidth(52);
    val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(l);
    row->addWidget(p, 1);
    row->addWidget(val);
    *lbl = val;
    *bar = p;
    break_lay->addLayout(row);
  };
  addBar(&lbl_productive_, &bar_productive_, tr("On track"), "BarOk");
  addBar(&lbl_unproductive_, &bar_unproductive_, tr("Sites"), "BarBad");
  addBar(&lbl_phone_, &bar_phone_, tr("Phone"), "BarPhone");
  lay->addWidget(break_card);

  // Days card
  auto* days_card = new QFrame(page);
  days_card->setObjectName("Card");
  auto* days_lay = new QVBoxLayout(days_card);
  days_lay->setContentsMargins(24, 20, 24, 16);
  days_lay->setSpacing(12);
  auto* days_title = new QLabel(tr("BY DAY"), days_card);
  days_title->setObjectName("CardTitle");
  days_lay->addWidget(days_title);
  week_bars_host_ = new QWidget(days_card);
  week_bars_layout_ = new QHBoxLayout(week_bars_host_);
  week_bars_layout_->setContentsMargins(0, 4, 0, 0);
  week_bars_layout_->setSpacing(10);
  week_bars_layout_->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
  days_lay->addWidget(week_bars_host_);
  lay->addWidget(days_card);

  // Recent sessions card
  auto* sess_card = new QFrame(page);
  sess_card->setObjectName("Card");
  auto* sess_lay = new QVBoxLayout(sess_card);
  sess_lay->setContentsMargins(24, 18, 24, 18);
  sess_lay->setSpacing(10);
  auto* sess_title = new QLabel(tr("RECENT"), sess_card);
  sess_title->setObjectName("CardTitle");
  sess_lay->addWidget(sess_title);
  sessions_list_ = new QPlainTextEdit(sess_card);
  sessions_list_->setObjectName("SessionsList");
  sessions_list_->setReadOnly(true);
  sessions_list_->setMaximumHeight(88);
  sessions_list_->setPlaceholderText(tr("No sessions"));
  sessions_list_->setFrameShape(QFrame::NoFrame);
  sess_lay->addWidget(sessions_list_);
  lay->addWidget(sess_card);

  date_range_hint_ = new QLabel(page);
  date_range_hint_->setObjectName("Subtitle");
  date_range_hint_->setVisible(false);
  lay->addWidget(date_range_hint_);
  lay->addStretch(1);
  return page;
}

void DashboardWindow::selectStatsWindow(StatsWindow w) {
  if (w == StatsWindow::Custom) {
    activateCustomDateRange();
    return;
  }
  stats_window_ = w;
  setDatePickerVisible(false);
  emit statsWindowChanged(w);
  emit refreshStatsRequested();
}

QWidget* DashboardWindow::buildSettings() {
  auto* page = new QWidget(stack_);
  auto* lay = pageLayout(page);
  lay->setSpacing(16);
  auto* title = new QLabel(tr("Settings"), page);
  title->setObjectName("Title");
  lay->addWidget(title);
  lay->addSpacing(8);

  auto* form = new QGridLayout();
  form->setHorizontalSpacing(20);
  form->setVerticalSpacing(14);
  int r = 0;
  auto addLabel = [&](const QString& t) {
    auto* l = new QLabel(t, page);
    l->setObjectName("CardTitle");
    form->addWidget(l, r, 0);
  };

  addLabel(tr("RESUME FOCUS"));
  set_resume_ = new QCheckBox(tr("Resume open session on launch"), page);
  form->addWidget(set_resume_, r++, 1);

  addLabel(tr("OPEN AT LOGIN"));
  set_open_at_login_ = new QCheckBox(tr("Start with macOS login"), page);
  form->addWidget(set_open_at_login_, r++, 1);

  addLabel(tr("PRIVACY"));
  set_privacy_ = new QCheckBox(tr("Redact URL queries"), page);
  form->addWidget(set_privacy_, r++, 1);

  addLabel(tr("SOUND"));
  set_alarm_sound_ = new QCheckBox(tr("Alarm sound"), page);
  form->addWidget(set_alarm_sound_, r++, 1);

  addLabel(tr("PRESET"));
  auto* sound_row = new QHBoxLayout();
  set_alarm_sound_name_ = new QComboBox(page);
  for (const char* name : {"default", "sosumi", "glass", "funk", "tink", "purr"}) {
    set_alarm_sound_name_->addItem(QString::fromUtf8(name));
  }
  auto* test_sound = new QPushButton(tr("Test"), page);
  test_sound->setObjectName("Secondary");
  connect(test_sound, &QPushButton::clicked, this, &DashboardWindow::testAlarmSoundRequested);
  sound_row->addWidget(set_alarm_sound_name_, 1);
  sound_row->addWidget(test_sound);
  form->addLayout(sound_row, r++, 1);

  addLabel(tr("PHONE (SEC)"));
  set_phone_threshold_ = new QSpinBox(page);
  set_phone_threshold_->setRange(5, 3600);
  set_phone_threshold_->setValue(60);
  form->addWidget(set_phone_threshold_, r++, 1);

  addLabel(tr("WINDOW (MIN)"));
  set_phone_window_min_ = new QSpinBox(page);
  set_phone_window_min_->setRange(1, 240);
  set_phone_window_min_->setValue(30);
  form->addWidget(set_phone_window_min_, r++, 1);

  addLabel(tr("CAMERA #"));
  set_camera_index_ = new QSpinBox(page);
  set_camera_index_->setRange(0, 16);
  form->addWidget(set_camera_index_, r++, 1);

  addLabel(tr("PORT"));
  set_bridge_port_ = new QSpinBox(page);
  set_bridge_port_->setRange(1024, 65535);
  set_bridge_port_->setValue(18765);
  form->addWidget(set_bridge_port_, r++, 1);

  addLabel(tr("TOKEN"));
  set_token_ = new QLineEdit(page);
  set_token_->setEchoMode(QLineEdit::Password);
  set_token_->setPlaceholderText(tr("Leave blank to keep"));
  form->addWidget(set_token_, r++, 1);
  lay->addLayout(form);

  auto* bl_lab = new QLabel(tr("BLOCKLIST"), page);
  bl_lab->setObjectName("CardTitle");
  lay->addWidget(bl_lab);
  set_blocklist_ = new QPlainTextEdit(page);
  set_blocklist_->setMaximumHeight(80);
  lay->addWidget(set_blocklist_);

  auto* al_lab = new QLabel(tr("ALLOWLIST"), page);
  al_lab->setObjectName("CardTitle");
  lay->addWidget(al_lab);
  set_allowlist_ = new QPlainTextEdit(page);
  set_allowlist_->setMaximumHeight(64);
  lay->addWidget(set_allowlist_);

  auto* actions = new QHBoxLayout();
  auto* save = new QPushButton(tr("Save"), page);
  save->setObjectName("Primary");
  connect(save, &QPushButton::clicked, this, &DashboardWindow::saveSettingsRequested);
  auto* reset = new QPushButton(tr("Reset"), page);
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
  focus_badge_->setText(focus_on ? tr("Focus on") : tr("Ready"));
  focus_btn_->setText(focus_on ? tr("Stop focus") : tr("Start focus"));
  focus_btn_->setProperty("focusOn", focus_on);
  focus_btn_->style()->unpolish(focus_btn_);
  focus_btn_->style()->polish(focus_btn_);

  bridge_value_->setText(bridge_ok ? tr("Online · %1").arg(bridge_port) : tr("Off"));
  camera_value_->setText(camera_on ? (camera_device_label.isEmpty() ? tr("On") : camera_device_label)
                                   : tr("Off"));
  if (!phone_detail.isEmpty()) {
    phone_value_->setText((phone_visible ? tr("In use") : tr("Idle")) + " · " + phone_detail);
  } else {
    phone_value_->setText(phone_visible ? tr("In use") : tr("Idle"));
  }
  if (alarm_active) {
    alarm_value_->setText(alarm_text.isEmpty() ? tr("Active") : alarm_text);
    alarm_value_->setStyleSheet("color: #F87171; font-size: 15px; font-weight: 700;");
  } else {
    alarm_value_->setText(tr("Clear"));
    alarm_value_->setStyleSheet("color: #34D399; font-size: 15px; font-weight: 600;");
  }
  session_line_->setText(last_session_line.isEmpty() ? tr("No sessions yet") : last_session_line);

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
  const auto vm = buildStatsViewModel(window, day_chart);

  if (window_label_) {
    window_label_->setText(QString::fromStdString(vm.window_title) +
                           (vm.session_count > 0
                                ? tr("  ·  %1 session(s)").arg(vm.session_count)
                                : QString()));
  }
  if (score_value_) score_value_->setText(QString::fromStdString(vm.score_text));
  if (duration_value_) duration_value_->setText(QString::fromStdString(vm.focus_text));

  auto setBar = [](QProgressBar* bar, QLabel* lbl, int pct, const std::string& text) {
    if (!bar) return;
    pct = std::clamp(pct, 0, 100);
    bar->setValue(pct == 0 ? 0 : pct);
    if (lbl) lbl->setText(QString::fromStdString(text));
  };
  setBar(bar_productive_, lbl_productive_, vm.productive_pct, vm.productive_text);
  setBar(bar_unproductive_, lbl_unproductive_, vm.unproductive_pct, vm.unproductive_text);
  setBar(bar_phone_, lbl_phone_, vm.phone_pct, vm.phone_text);

  if (week_bars_layout_) {
    while (QLayoutItem* it = week_bars_layout_->takeAt(0)) {
      if (it->widget()) it->widget()->deleteLater();
      delete it;
    }
    if (vm.days.empty()) {
      auto* empty = new QLabel(tr("No daily data"), week_bars_host_);
      empty->setObjectName("Subtitle");
      week_bars_layout_->addWidget(empty);
    }
    for (const auto& d : vm.days) {
      auto* col = new QWidget(week_bars_host_);
      auto* cl = new QVBoxLayout(col);
      cl->setContentsMargins(0, 0, 0, 0);
      cl->setSpacing(8);
      cl->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
      auto* bar = new QProgressBar(col);
      bar->setObjectName("DayBar");
      bar->setOrientation(Qt::Vertical);
      bar->setRange(0, 100);
      // Empty days stay flat; give a tiny floor only when there was focus.
      const int vis = d.focus_seconds > 0 ? std::max(8, d.score_0_100) : 0;
      bar->setValue(vis);
      bar->setFixedWidth(28);
      bar->setFixedHeight(88);
      bar->setTextVisible(false);
      bar->setToolTip(QString::fromStdString(d.day_label + " · score " +
                                             std::to_string(d.score_0_100) + " · " +
                                             formatFocusDuration(d.focus_seconds)));
      auto* day = new QLabel(QString::fromStdString(d.day_label), col);
      day->setObjectName("MetricLabel");
      day->setAlignment(Qt::AlignCenter);
      day->setMinimumWidth(36);
      cl->addStretch(1);
      cl->addWidget(bar, 0, Qt::AlignHCenter);
      cl->addWidget(day);
      week_bars_layout_->addWidget(col);
    }
    week_bars_layout_->addStretch(1);
  }

  if (sessions_list_) {
    QString body;
    for (const auto& line : vm.session_lines) {
      body += QString::fromStdString(line) + "\n";
    }
    sessions_list_->setPlainText(body.trimmed());
  }
}

void DashboardWindow::setDatePickerVisible(bool visible) {
  if (date_picker_panel_) date_picker_panel_->setVisible(visible);
}

void DashboardWindow::syncDateEditorsFromCustomRange() {
  suppress_date_sync_ = true;
  const QDate today = QDate::currentDate();
  if (custom_from_ > today) custom_from_ = today;
  if (custom_to_ > today) custom_to_ = today;
  if (custom_from_ > custom_to_) std::swap(custom_from_, custom_to_);
  if (date_from_) {
    const QSignalBlocker b(date_from_);
    date_from_->setMaximumDate(today);
    date_from_->setDate(custom_from_);
  }
  if (date_to_) {
    const QSignalBlocker b(date_to_);
    date_to_->setMaximumDate(today);
    date_to_->setDate(custom_to_);
  }
  suppress_date_sync_ = false;
}

void DashboardWindow::onFromDateEdited(QDate d) {
  if (suppress_date_sync_ || !d.isValid()) return;
  if (d > QDate::currentDate()) d = QDate::currentDate();
  custom_from_ = d;
  if (custom_to_ < custom_from_) {
    custom_to_ = custom_from_;
    if (date_to_) {
      const QSignalBlocker b(date_to_);
      date_to_->setDate(custom_to_);
    }
  }
}

void DashboardWindow::onToDateEdited(QDate d) {
  if (suppress_date_sync_ || !d.isValid()) return;
  if (d > QDate::currentDate()) d = QDate::currentDate();
  custom_to_ = d;
  if (custom_from_ > custom_to_) {
    custom_from_ = custom_to_;
    if (date_from_) {
      const QSignalBlocker b(date_from_);
      date_from_->setDate(custom_from_);
    }
  }
}

void DashboardWindow::openCalendarPopupFor(bool is_from) {
  QDialog dlg(this);
  dlg.setWindowTitle(is_from ? tr("Start date") : tr("End date"));
  dlg.setModal(true);
  dlg.setMinimumSize(300, 300);
  dlg.setStyleSheet(styleSheet());
  auto* lay = new QVBoxLayout(&dlg);
  auto* cal = new QCalendarWidget(&dlg);
  cal->setGridVisible(true);
  cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
  cal->setMinimumDate(QDate(2020, 1, 1));
  cal->setMaximumDate(QDate::currentDate());
  const QDate initial = is_from ? custom_from_ : custom_to_;
  cal->setSelectedDate(initial.isValid() ? initial : QDate::currentDate());
  lay->addWidget(cal);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  lay->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(cal, &QCalendarWidget::activated, &dlg, &QDialog::accept);
  if (dlg.exec() != QDialog::Accepted) return;
  const QDate picked = cal->selectedDate();
  if (!picked.isValid()) return;
  if (is_from) {
    onFromDateEdited(picked);
    if (date_from_) {
      const QSignalBlocker b(date_from_);
      date_from_->setDate(picked);
    }
  } else {
    onToDateEdited(picked);
    if (date_to_) {
      const QSignalBlocker b(date_to_);
      date_to_->setDate(picked);
    }
  }
}

void DashboardWindow::activateCustomDateRange() {
  stats_window_ = StatsWindow::Custom;
  if (stats_custom_chip_) {
    const QSignalBlocker block(stats_custom_chip_);
    stats_custom_chip_->setChecked(true);
  }
  if (!custom_from_.isValid()) custom_from_ = QDate::currentDate();
  if (!custom_to_.isValid()) custom_to_ = QDate::currentDate();
  syncDateEditorsFromCustomRange();
  setDatePickerVisible(true);
  emit statsWindowChanged(StatsWindow::Custom);
  emit refreshStatsRequested();
}

void DashboardWindow::applyCustomDateRangeFromEditors() {
  if (!date_from_ || !date_to_) return;
  QDate from = date_from_->date();
  QDate to = date_to_->date();
  if (!from.isValid() || !to.isValid()) return;
  if (from > to) std::swap(from, to);
  const QDate today = QDate::currentDate();
  if (to > today) to = today;
  if (from > to) from = to;
  custom_from_ = from;
  custom_to_ = to;
  syncDateEditorsFromCustomRange();
  stats_window_ = StatsWindow::Custom;
  if (stats_custom_chip_) {
    const QSignalBlocker block(stats_custom_chip_);
    stats_custom_chip_->setChecked(true);
  }
  setDatePickerVisible(true);
  if (date_range_hint_) {
    date_range_hint_->setVisible(true);
    date_range_hint_->setText(tr("%1 → %2")
                                  .arg(from.toString(QStringLiteral("MMM d")))
                                  .arg(to.toString(QStringLiteral("MMM d"))));
  }
  emit customDateRangeApplied();
  emit statsWindowChanged(StatsWindow::Custom);
  emit refreshStatsRequested();
}

bool DashboardWindow::selectedCustomRangeEpoch(EpochSeconds& range_start,
                                               EpochSeconds& range_end_exclusive) const {
  if (stats_window_ != StatsWindow::Custom) return false;
  if (!custom_from_.isValid() || !custom_to_.isValid()) return false;
  QDate from = custom_from_;
  QDate to = custom_to_;
  if (from > to) std::swap(from, to);
  range_start = ProductivityStats::localMidnightEpoch(
      from.toString(QStringLiteral("yyyy-MM-dd")).toStdString());
  range_end_exclusive = ProductivityStats::localMidnightEpoch(
      to.addDays(1).toString(QStringLiteral("yyyy-MM-dd")).toStdString());
  if (range_end_exclusive <= range_start) range_end_exclusive = range_start + 24 * 3600;
  return true;
}

QString DashboardWindow::customRangeLabel() const {
  if (!custom_from_.isValid() || !custom_to_.isValid()) return tr("Custom");
  return tr("%1 → %2")
      .arg(custom_from_.toString(QStringLiteral("yyyy-MM-dd")))
      .arg(custom_to_.toString(QStringLiteral("yyyy-MM-dd")));
}

void DashboardWindow::setCameraDevices(const std::vector<std::pair<int, QString>>& devices,
                                      int selected_index) {
  if (!camera_combo_) return;
  suppress_camera_device_signal_ = true;
  camera_combo_->clear();
  if (devices.empty()) {
    camera_combo_->addItem(tr("No camera"), selected_index);
  } else {
    for (const auto& d : devices) camera_combo_->addItem(d.second, d.first);
  }
  const int idx = camera_combo_->findData(selected_index);
  if (idx >= 0) camera_combo_->setCurrentIndex(idx);
  suppress_camera_device_signal_ = false;
}

void DashboardWindow::loadSettingsForm(const Settings& s) {
  set_resume_->setChecked(s.resume_focus_on_launch);
  if (set_open_at_login_) set_open_at_login_->setChecked(s.open_at_login);
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
  set_token_->setPlaceholderText(tr("Keep current token"));
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
  if (set_open_at_login_) s.open_at_login = set_open_at_login_->isChecked();
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
    return UrlClassifier::normalizeDomainList(out);
  };
  s.blocklist = parseLines(set_blocklist_->toPlainText());
  s.allowlist = parseLines(set_allowlist_->toPlainText());
  return s;
}

} // namespace focusgaze
