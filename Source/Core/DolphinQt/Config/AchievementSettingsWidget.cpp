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
#include <Core/Config/AchievementSettings.h>

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
  m_common_encore_enabled_input = new QCheckBox(tr("Enable Encore Achievements"));

  m_common_layout->addWidget(m_common_integration_enabled_input);
  m_common_layout->addWidget(m_common_achievements_enabled_input);
  m_common_layout->addWidget(m_common_leaderboards_enabled_input);
  m_common_layout->addWidget(m_common_rich_presence_enabled_input);
  m_common_layout->addWidget(m_common_hardcore_enabled_input);
  m_common_layout->addWidget(m_common_badge_icons_enabled_input);
  m_common_layout->addWidget(m_common_test_mode_enabled_input);
  m_common_layout->addWidget(m_common_unofficial_enabled_input);
  m_common_layout->addWidget(m_common_encore_enabled_input);

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
  connect(m_common_encore_enabled_input, &QCheckBox::toggled, this,
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
      ->setChecked(Config::Get(Config::RA_INTEGRATION_ENABLED));
  SignalBlocking(m_common_achievements_enabled_input)
      ->setChecked(Config::Get(Config::RA_ACHIEVEMENTS_ENABLED));
  SignalBlocking(m_common_leaderboards_enabled_input)
      ->setChecked(Config::Get(Config::RA_LEADERBOARDS_ENABLED));
  SignalBlocking(m_common_rich_presence_enabled_input)
      ->setChecked(Config::Get(Config::RA_RICH_PRESENCE_ENABLED));
  SignalBlocking(m_common_hardcore_enabled_input)
      ->setChecked(Config::Get(Config::RA_HARDCORE_ENABLED));
  SignalBlocking(m_common_badge_icons_enabled_input)
      ->setChecked(Config::Get(Config::RA_BADGE_ICONS_ENABLED));
  SignalBlocking(m_common_test_mode_enabled_input)
      ->setChecked(Config::Get(Config::RA_TEST_MODE_ENABLED));
  SignalBlocking(m_common_unofficial_enabled_input)
      ->setChecked(Config::Get(Config::RA_UNOFFICIAL_ENABLED));
  SignalBlocking(m_common_encore_enabled_input)
      ->setChecked(Config::Get(Config::RA_ENCORE_ENABLED));
}

void AchievementSettingsWidget::SaveSettings()
{
  if (m_common_integration_enabled_input)
    Config::Ach::EnableRAIntegration();
  else
    Config::Ach::DisableRAIntegration();
  if (m_common_achievements_enabled_input)
    Config::Ach::EnableAchievements();
  else
    Config::Ach::DisableAchievements();
  if (m_common_leaderboards_enabled_input)
    Config::Ach::EnableLeaderboards();
  else
    Config::Ach::DisableLeaderboards();
  if (m_common_rich_presence_enabled_input)
    Config::Ach::EnableRichPresence();
  else
    Config::Ach::DisableRichPresence();
  if (m_common_hardcore_enabled_input)
    Config::Ach::EnableHardcore();
  else
    Config::Ach::DisableHardcore();
  if (m_common_badge_icons_enabled_input)
    Config::Ach::EnableBadgeIcons();
  else
    Config::Ach::DisableBadgeIcons();
  if (m_common_test_mode_enabled_input)
    Config::Ach::EnableTestMode();
  else
    Config::Ach::DisableTestMode();
  if (m_common_unofficial_enabled_input)
    Config::Ach::EnableUnofficial();
  else
    Config::Ach::DisableUnofficial();
  if (m_common_encore_enabled_input)
    Config::Ach::EnableEncore();
  else
    Config::Ach::DisableEncore();
  Config::Save();
}
