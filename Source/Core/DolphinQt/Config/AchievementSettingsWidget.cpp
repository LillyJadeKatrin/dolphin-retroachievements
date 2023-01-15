// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// 15 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#include "DolphinQt/Config/AchievementSettingsWidget.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/AchievementManager.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"

#include "DolphinQt/Config/ControllerInterface/ControllerInterfaceWindow.h"
#include "DolphinQt/QtUtils/NonDefaultQPushButton.h"
#include "DolphinQt/QtUtils/SignalBlocking.h"
#include "DolphinQt/Settings.h"

AchievementSettingsWidget::AchievementSettingsWidget(QWidget* parent) : QWidget(parent)
{
  CreateLayout();
  LoadSettings();
  ConnectWidgets();

  connect(&Settings::Instance(), &Settings::ConfigChanged, this,
          &AchievementSettingsWidget::LoadSettings);
}

void AchievementSettingsWidget::CreateLayout()
{
  m_common_box = new QGroupBox(tr("Common"));
  m_common_layout = new QVBoxLayout();
  m_common_integration_enabled_input = new QCheckBox(tr("Enable RetroAchievements Integration"));
  m_common_achievements_enabled_input = new QCheckBox(tr("Enable Achievements"));
  m_common_leaderboards_enabled_input = new QCheckBox(tr("Enable Leaderboards"));
  m_common_rich_presence_enabled_input = new QCheckBox(tr("Enable Rich Presence"));
  m_common_hardcore_enabled_input = new QCheckBox(tr("Enable Hardcore Mode"));
  m_common_badge_icons_enabled_input = new QCheckBox(tr("Enable Badge Icons"));
  m_common_test_mode_enabled_input = new QCheckBox(tr("Enable Test Mode"));
  m_common_unofficial_enabled_input = new QCheckBox(tr("Enable Unofficial Achievements"));

  m_common_layout->addWidget(m_common_integration_enabled_input);
  m_common_layout->addWidget(m_common_achievements_enabled_input);
  m_common_layout->addWidget(m_common_leaderboards_enabled_input);
  m_common_layout->addWidget(m_common_rich_presence_enabled_input);
  m_common_layout->addWidget(m_common_hardcore_enabled_input);
  m_common_layout->addWidget(m_common_badge_icons_enabled_input);
  m_common_layout->addWidget(m_common_test_mode_enabled_input);
  m_common_layout->addWidget(m_common_unofficial_enabled_input);

  m_common_box->setLayout(m_common_layout);

  auto* layout = new QVBoxLayout;
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setAlignment(Qt::AlignTop);
  layout->addWidget(m_common_box);
  setLayout(layout);
}

void AchievementSettingsWidget::ConnectWidgets()
{
  connect(m_common_integration_enabled_input, &QCheckBox::toggled, this,
          &AchievementSettingsWidget::SaveSettings);
  connect(m_common_achievements_enabled_input, &QCheckBox::toggled, this,
          &AchievementSettingsWidget::SaveSettings);
  connect(m_common_leaderboards_enabled_input, &QCheckBox::toggled, this,
          &AchievementSettingsWidget::SaveSettings);
  connect(m_common_rich_presence_enabled_input, &QCheckBox::toggled, this,
          &AchievementSettingsWidget::SaveSettings);
  connect(m_common_hardcore_enabled_input, &QCheckBox::toggled, this,
          &AchievementSettingsWidget::SaveSettings);
  connect(m_common_badge_icons_enabled_input, &QCheckBox::toggled, this,
          &AchievementSettingsWidget::SaveSettings);
  connect(m_common_test_mode_enabled_input, &QCheckBox::toggled, this,
          &AchievementSettingsWidget::SaveSettings);
  connect(m_common_unofficial_enabled_input, &QCheckBox::toggled, this,
          &AchievementSettingsWidget::SaveSettings);
}

void AchievementSettingsWidget::OnControllerInterfaceConfigure()
{
  ControllerInterfaceWindow* window = new ControllerInterfaceWindow(this);
  window->setAttribute(Qt::WA_DeleteOnClose, true);
  window->setWindowModality(Qt::WindowModality::WindowModal);
  window->show();
}

void AchievementSettingsWidget::LoadSettings()
{
  SignalBlocking(m_common_integration_enabled_input)
      ->setChecked(Achievements::sett_integration_enabled);
  SignalBlocking(m_common_achievements_enabled_input)
      ->setChecked(Achievements::sett_achievements_enabled);
  SignalBlocking(m_common_leaderboards_enabled_input)
      ->setChecked(Achievements::sett_leaderboards_enabled);
  SignalBlocking(m_common_rich_presence_enabled_input)
      ->setChecked(Achievements::sett_rich_presence_enabled);
  SignalBlocking(m_common_hardcore_enabled_input)
      ->setChecked(Achievements::sett_hardcore_enabled);
  SignalBlocking(m_common_badge_icons_enabled_input)
      ->setChecked(Achievements::sett_badge_icons_enabled);
  SignalBlocking(m_common_test_mode_enabled_input)
      ->setChecked(Achievements::sett_test_mode_enabled);
  SignalBlocking(m_common_unofficial_enabled_input)
      ->setChecked(Achievements::sett_unofficial_enabled);
}

void AchievementSettingsWidget::SaveSettings()
{
  Achievements::sett_integration_enabled = m_common_integration_enabled_input->isChecked();
  Achievements::sett_achievements_enabled = m_common_achievements_enabled_input->isChecked();
  Achievements::sett_leaderboards_enabled = m_common_leaderboards_enabled_input->isChecked();
  Achievements::sett_rich_presence_enabled = m_common_rich_presence_enabled_input->isChecked();
  Achievements::sett_hardcore_enabled = m_common_hardcore_enabled_input->isChecked();
  Achievements::sett_badge_icons_enabled = m_common_badge_icons_enabled_input->isChecked();
  Achievements::sett_test_mode_enabled = m_common_test_mode_enabled_input->isChecked();
  Achievements::sett_unofficial_enabled = m_common_unofficial_enabled_input->isChecked();
}
