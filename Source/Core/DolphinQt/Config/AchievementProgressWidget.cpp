// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// 25 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#include "DolphinQt/Config/AchievementProgressWidget.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include "rcheevos/include/rc_runtime.h"
#include <rcheevos/include/rc_api_runtime.h>
#include <rcheevos/include/rc_api_user.h>

#include "Core/AchievementManager.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"

#include <Core/Config/AchievementSettings.h>
#include <ModalMessageBox.h>
#include "DolphinQt/Config/ControllerInterface/ControllerInterfaceWindow.h"
#include "DolphinQt/QtUtils/NonDefaultQPushButton.h"
#include "DolphinQt/QtUtils/SignalBlocking.h"
#include "DolphinQt/Settings.h"
#include <QProgressBar>

AchievementProgressWidget::AchievementProgressWidget(QWidget* parent) : QWidget(parent)
{
  m_common_box = new QGroupBox(tr("Common"));
  m_common_layout = new QVBoxLayout();

  for (unsigned int ix = 0; ix < 3; ix++)
  //  for (unsigned int ix = 0; ix < Achievements::GetGameData()->num_achievements; ix++)
  {
    m_common_layout->addWidget(CreateAchievementBox(Achievements::GetGameData()->achievements + ix));
  }

  m_common_box->setLayout(m_common_layout);

  auto* layout = new QVBoxLayout;
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setAlignment(Qt::AlignTop);
  layout->addWidget(m_common_box);
  setLayout(layout);
}

QGroupBox* AchievementProgressWidget::CreateAchievementBox(const rc_api_achievement_definition_t* achievement)
{
  QImage i_icon;
  i_icon.loadFromData(Achievements::GetAchievementBadge(achievement->id, false)->begin()._Ptr,
                      (int)Achievements::GetAchievementBadge(achievement->id, false)->size());
  QLabel* a_icon = new QLabel();
  a_icon->setPixmap(QPixmap::fromImage(i_icon));
  a_icon->adjustSize();
  QLabel* a_title =
      new QLabel(QString::fromLocal8Bit(achievement->title, strlen(achievement->title)));
  QLabel* a_description =
      new QLabel(QString::fromLocal8Bit(achievement->description, strlen(achievement->description)));
  QLabel* a_points = new QLabel(QString::fromStdString(std::format("{} points", achievement->points)));
  QProgressBar* a_progress_bar = new QProgressBar();

  QVBoxLayout* a_col_right = new QVBoxLayout();
  a_col_right->addWidget(a_title);
  a_col_right->addWidget(a_description);
  a_col_right->addWidget(a_points);
  a_col_right->addWidget(a_progress_bar);
  QHBoxLayout* a_total = new QHBoxLayout();
  a_total->addWidget(a_icon);
  a_total->addLayout(a_col_right);
  QGroupBox* a_group_box = new QGroupBox();
  a_group_box->setLayout(a_total);
  return a_group_box;
}
