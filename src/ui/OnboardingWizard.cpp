#include "ui/OnboardingWizard.hpp"

#include <algorithm>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace focusgaze {
namespace {

const char* kStyle = R"(
  QDialog {
    background: #0B0F14;
    color: #F1F5F9;
    font-family: Inter, "SF Pro Text", "Helvetica Neue", Arial, sans-serif;
  }
  QLabel#Title { color: #2DD4BF; font-size: 22px; font-weight: 700; }
  QLabel#Body { color: #94A3B8; font-size: 13px; }
  QLabel#Step { color: #64748B; font-size: 11px; font-weight: 600; letter-spacing: 0.08em; }
  QPushButton#Primary {
    background: #2DD4BF; color: #04352F; border: none; border-radius: 8px;
    padding: 10px 18px; font-weight: 700;
  }
  QPushButton#Primary:hover { background: #5EEAD4; }
  QPushButton#Secondary {
    background: transparent; color: #CBD5E1; border: 1px solid #243041;
    border-radius: 8px; padding: 10px 16px; font-weight: 600;
  }
  QFrame#Card {
    background: #141A22; border: 1px solid #243041; border-radius: 12px;
  }
)";

} // namespace

OnboardingWizard::OnboardingWizard(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Welcome to focusGaze"));
  setModal(true);
  setMinimumSize(640, 480);
  setStyleSheet(kStyle);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(28, 24, 28, 20);
  root->setSpacing(16);

  step_label_ = new QLabel(this);
  step_label_->setObjectName("Step");
  root->addWidget(step_label_);

  stack_ = new QStackedWidget(this);
  stack_->addWidget(buildWelcome());
  stack_->addWidget(buildPermissions());
  stack_->addWidget(buildBrowser());
  stack_->addWidget(buildConnect());
  root->addWidget(stack_, 1);

  auto* actions = new QHBoxLayout();
  auto* skip_btn = new QPushButton(tr("Skip for now"), this);
  skip_btn->setObjectName("Secondary");
  connect(skip_btn, &QPushButton::clicked, this, &OnboardingWizard::skip);
  back_btn_ = new QPushButton(tr("Back"), this);
  back_btn_->setObjectName("Secondary");
  connect(back_btn_, &QPushButton::clicked, this, &OnboardingWizard::back);
  next_btn_ = new QPushButton(tr("Get started"), this);
  next_btn_->setObjectName("Primary");
  connect(next_btn_, &QPushButton::clicked, this, &OnboardingWizard::next);
  actions->addWidget(skip_btn);
  actions->addStretch(1);
  actions->addWidget(back_btn_);
  actions->addWidget(next_btn_);
  root->addLayout(actions);

  setStep(0);
}

QWidget* OnboardingWizard::buildWelcome() {
  auto* w = new QWidget(this);
  auto* lay = new QVBoxLayout(w);
  lay->setSpacing(12);
  auto* title = new QLabel(tr("Welcome to focusGaze"), w);
  title->setObjectName("Title");
  auto* body = new QLabel(
      tr("Local productivity guardian — browser focus + optional phone detection.\n"
         "No cloud accounts. All processing stays on this Mac."),
      w);
  body->setObjectName("Body");
  body->setWordWrap(true);
  lay->addStretch(1);
  lay->addWidget(title);
  lay->addWidget(body);
  lay->addStretch(2);
  return w;
}

QWidget* OnboardingWizard::buildPermissions() {
  auto* w = new QWidget(this);
  auto* lay = new QVBoxLayout(w);
  auto* title = new QLabel(tr("Camera permission"), w);
  title->setObjectName("Title");
  auto* body = new QLabel(
      tr("Phone detection is optional. If you enable camera monitoring later, macOS will ask "
         "for Camera access.\n\n"
         "Tip: pick your Mac webcam (not Continuity/iPhone) in the dashboard if the wrong "
         "camera is selected."),
      w);
  body->setObjectName("Body");
  body->setWordWrap(true);
  auto* open_btn = new QPushButton(tr("Open System Settings (Privacy → Camera)"), w);
  open_btn->setObjectName("Secondary");
  connect(open_btn, &QPushButton::clicked, this, &OnboardingWizard::openSystemSettingsRequested);
  lay->addWidget(title);
  lay->addWidget(body);
  lay->addSpacing(8);
  lay->addWidget(open_btn, 0, Qt::AlignLeft);
  lay->addStretch(1);
  return w;
}

QWidget* OnboardingWizard::buildBrowser() {
  auto* w = new QWidget(this);
  auto* lay = new QVBoxLayout(w);
  auto* title = new QLabel(tr("Install the Chrome extension"), w);
  title->setObjectName("Title");
  auto* body = new QLabel(
      tr("focusGaze uses a small Chrome extension to see active tab URLs while Focus is on.\n\n"
         "• Production: install from the Chrome Web Store (button below).\n"
         "• Development: chrome://extensions → Developer mode → Load unpacked → extension/chrome."),
      w);
  body->setObjectName("Body");
  body->setWordWrap(true);
  auto* store = new QPushButton(tr("Get Chrome extension…"), w);
  store->setObjectName("Primary");
  connect(store, &QPushButton::clicked, this, &OnboardingWizard::openExtensionStoreRequested);
  lay->addWidget(title);
  lay->addWidget(body);
  lay->addSpacing(8);
  lay->addWidget(store, 0, Qt::AlignLeft);
  lay->addStretch(1);
  return w;
}

QWidget* OnboardingWizard::buildConnect() {
  auto* w = new QWidget(this);
  auto* lay = new QVBoxLayout(w);
  auto* title = new QLabel(tr("Connect browser"), w);
  title->setObjectName("Title");
  auto* body = new QLabel(
      tr("Link this app to the extension with one click. A short-lived page opens in "
         "Google Chrome and stores the bridge token automatically — no copy/paste.\n\n"
         "Finishing also registers a local Chrome Native Messaging host (optional helper) "
         "so the extension can discover this app on this Mac."),
      w);
  body->setObjectName("Body");
  body->setWordWrap(true);
  auto* pair_btn = new QPushButton(tr("Connect browser now"), w);
  pair_btn->setObjectName("Primary");
  QObject::connect(pair_btn, &QPushButton::clicked, this,
                   &OnboardingWizard::connectBrowserRequested);
  open_at_login_check_ = new QCheckBox(tr("Open focusGaze when I log in to this Mac"), w);
  open_at_login_check_->setChecked(false);
  lay->addWidget(title);
  lay->addWidget(body);
  lay->addSpacing(8);
  lay->addWidget(pair_btn, 0, Qt::AlignLeft);
  lay->addSpacing(12);
  lay->addWidget(open_at_login_check_);
  lay->addStretch(1);
  return w;
}

bool OnboardingWizard::openAtLoginRequested() const {
  return open_at_login_check_ && open_at_login_check_->isChecked();
}

void OnboardingWizard::setStep(int step) {
  step_ = std::clamp(step, 0, 3);
  stack_->setCurrentIndex(step_);
  static const char* names[] = {"Welcome", "Permissions", "Browser", "Connect"};
  step_label_->setText(tr("STEP %1 OF 4  ·  %2").arg(step_ + 1).arg(tr(names[step_])));
  back_btn_->setEnabled(step_ > 0);
  if (step_ == 0) next_btn_->setText(tr("Get started"));
  else if (step_ == 3) next_btn_->setText(tr("Finish"));
  else next_btn_->setText(tr("Continue"));
}

void OnboardingWizard::next() {
  if (step_ >= 3) {
    finish();
    return;
  }
  setStep(step_ + 1);
}

void OnboardingWizard::back() {
  if (step_ > 0) setStep(step_ - 1);
}

void OnboardingWizard::skip() { finish(); }

void OnboardingWizard::finish() {
  completed_ = true;
  accept();
}

} // namespace focusgaze
