// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// 
// 15 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#include "DolphinQt/Config/AchievementsWindow.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "DolphinQt/Config/AchievementProgressWidget.h"
#include "DolphinQt/Config/AchievementSettingsWidget.h"
#include "DolphinQt/QtUtils/WrapInScrollArea.h"

AchievementsWindow::AchievementsWindow(QWidget* parent) : QDialog(parent)
{
  setWindowTitle(tr("Achievements"));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  CreateMainLayout();
  ConnectWidgets();
}

void AchievementsWindow::CreateMainLayout()
{
  auto* layout = new QVBoxLayout();
  m_tab_widget = new QTabWidget();

  m_tab_widget->addTab(GetWrappedWidget(new AchievementSettingsWidget(m_tab_widget), this, 125, 100),
                       tr("Settings"));
  m_tab_widget->addTab(GetWrappedWidget(new AchievementProgressWidget(m_tab_widget), this, 125, 100),
                       tr("Progress"));

  m_button_box = new QDialogButtonBox(QDialogButtonBox::Close);

  layout->addWidget(m_tab_widget);
  layout->addStretch();
  layout->addWidget(m_button_box);

  WrapInScrollArea(this, layout);
}

void AchievementsWindow::ConnectWidgets()
{
  connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
