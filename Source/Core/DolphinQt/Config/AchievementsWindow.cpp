// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// 
// 15 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#include "DolphinQt/Config/AchievementsWindow.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "Core/AchievementManager.h"
#include "DolphinQt/Config/AchievementProgressWidget.h"
#include "DolphinQt/Config/AchievementSettingsWidget.h"
#include "DolphinQt/QtUtils/WrapInScrollArea.h"
#include <set>

AchievementsWindow::AchievementsWindow(QWidget* parent) : QDialog(parent)
{
  setWindowTitle(tr("Achievements"));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  CreateMainLayout();
  ConnectWidgets();
}

void AchievementsWindow::CreateGeneralBlock()
{
  std::set<unsigned int> hardcore_ids(
      Achievements::GetHardcoreGameProgress()->achievement_ids,
      Achievements::GetHardcoreGameProgress()->achievement_ids +
          Achievements::GetHardcoreGameProgress()->num_achievement_ids);
  std::set<unsigned int> softcore_ids(
      Achievements::GetHardcoreGameProgress()->achievement_ids,
      Achievements::GetHardcoreGameProgress()->achievement_ids +
          Achievements::GetHardcoreGameProgress()->num_achievement_ids);
  unsigned int hardcore_points = 0;
  unsigned int softcore_points = 0;
  unsigned int total_points = 0;
  for (unsigned int ix = 0; ix < Achievements::GetGameData()->num_achievements; ix++)
  {
    unsigned int id = Achievements::GetGameData()->achievements[ix].id;
    unsigned int points = Achievements::GetGameData()->achievements[ix].points;
    total_points += points;
    if (hardcore_ids.count(id) > 0)
      hardcore_points += points;
    if (softcore_ids.count(id) > 0)
      softcore_points += points;
  }

  std::string user_name(Achievements::GetUserStatus()->display_name);
  std::string user_points = std::format("{} points", Achievements::GetUserStatus()->score);
  std::string game_name(Achievements::GetGameData()->title);
  std::string game_points;
  if (softcore_points > 0)
  {
    game_points = std::format(
        "{} has unlocked {}/{} achievements ({} hardcore) worth {}/{} points ({} hardcore)",
      user_name,
      Achievements::GetHardcoreGameProgress()->num_achievement_ids + Achievements::GetSoftcoreGameProgress()->num_achievement_ids,
      Achievements::GetGameData()->num_achievements,
      Achievements::GetHardcoreGameProgress()->num_achievement_ids,
      hardcore_points + softcore_points,
      total_points,
      hardcore_points);
  }
  else
  {
    game_points = std::format(
        "{} has unlocked {}/{} achievements worth {}/{} points",
        user_name,
        Achievements::GetHardcoreGameProgress()->num_achievement_ids,
        Achievements::GetGameData()->num_achievements,
        hardcore_points, total_points);
  }

  QImage i_user_icon;
  i_user_icon.loadFromData(Achievements::GetUserIcon()->begin()._Ptr,
                           (int)Achievements::GetUserIcon()->size());
  QImage i_game_icon;
  i_game_icon.loadFromData(Achievements::GetGameIcon()->begin()._Ptr,
                           (int)Achievements::GetGameIcon()->size());

  m_user_icon = new QLabel();
  m_user_icon->setPixmap(QPixmap::fromImage(i_user_icon)
                             .scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  m_user_icon->adjustSize();
  m_user_name = new QLabel(QString::fromStdString(user_name));
  m_user_points = new QLabel(QString::fromStdString(user_points));
  m_game_icon = new QLabel();
  m_game_icon->setPixmap(QPixmap::fromImage(i_game_icon).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  m_game_icon->adjustSize();
  m_game_name = new QLabel(QString::fromStdString(game_name));
  m_game_points = new QLabel(QString::fromStdString(game_points));
  m_game_progress = new QProgressBar();

  QVBoxLayout* m_user_right_col = new QVBoxLayout();
  m_user_right_col->addWidget(m_user_name);
  m_user_right_col->addWidget(m_user_points);
  QHBoxLayout* m_user_block = new QHBoxLayout();
  m_user_block->addWidget(m_user_icon);
  m_user_block->addLayout(m_user_right_col);
  QVBoxLayout* m_game_right_col = new QVBoxLayout();
  m_game_right_col->addWidget(m_game_name);
  m_game_right_col->addWidget(m_game_points);
  m_game_right_col->addWidget(m_game_progress);
  QHBoxLayout* m_game_block = new QHBoxLayout();
  m_game_block->addWidget(m_game_icon);
  m_game_block->addLayout(m_game_right_col);
  QVBoxLayout* m_total_general = new QVBoxLayout();
  m_total_general->addLayout(m_user_block);
  m_total_general->addLayout(m_game_block);
  m_general_box = new QGroupBox();
  m_general_box->setLayout(m_total_general);
}

void AchievementsWindow::CreateMainLayout()
{
  auto* layout = new QVBoxLayout();

  CreateGeneralBlock();

  m_tab_widget = new QTabWidget();
  m_tab_widget->addTab(GetWrappedWidget(new AchievementSettingsWidget(m_tab_widget), this, 125, 100),
                       tr("Settings"));
  m_tab_widget->addTab(GetWrappedWidget(new AchievementProgressWidget(m_tab_widget), this, 125, 100),
                       tr("Progress"));

  m_button_box = new QDialogButtonBox(QDialogButtonBox::Close);

  layout->addWidget(m_general_box);
  layout->addWidget(m_tab_widget);
  layout->addStretch();
  layout->addWidget(m_button_box);

  WrapInScrollArea(this, layout);
}

void AchievementsWindow::ConnectWidgets()
{
  connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
