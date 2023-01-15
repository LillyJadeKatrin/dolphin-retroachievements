// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// 
// 15 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#include "DolphinQt/Config/AchievementsWindow.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "DolphinQt/Config/CommonControllersWidget.h"
#include "DolphinQt/Config/GamecubeControllersWidget.h"
#include "DolphinQt/Config/WiimoteControllersWidget.h"
#include "DolphinQt/QtUtils/WrapInScrollArea.h"

AchievementsWindow::AchievementsWindow(QWidget* parent) : QDialog(parent)
{
  setWindowTitle(tr("Controller Settings"));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  m_gamecube_controllers = new GamecubeControllersWidget(this);
  m_wiimote_controllers = new WiimoteControllersWidget(this);
  m_common = new CommonControllersWidget(this);
  CreateMainLayout();
  ConnectWidgets();
}

void AchievementsWindow::CreateMainLayout()
{
  auto* layout = new QVBoxLayout();
  m_button_box = new QDialogButtonBox(QDialogButtonBox::Close);

  layout->addWidget(m_gamecube_controllers);
  layout->addWidget(m_wiimote_controllers);
  layout->addWidget(m_common);
  layout->addStretch();
  layout->addWidget(m_button_box);

  WrapInScrollArea(this, layout);
}

void AchievementsWindow::ConnectWidgets()
{
  connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
