// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// 15 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#pragma once
#include <QDialog>

class QTabWidget;
class QDialogButtonBox;
class QGroupBox;
class QLabel;
class QProgressBar;

class AchievementsWindow : public QDialog
{
  Q_OBJECT
public:
  explicit AchievementsWindow(QWidget* parent);

private:
  void CreateMainLayout();
  void showEvent(QShowEvent* event);
  void CreateGeneralBlock();
  void ConnectWidgets();

  QGroupBox* m_general_box;
  QDialogButtonBox* m_button_box;
  QTabWidget* m_tab_widget;

  QLabel* m_user_icon;
  QLabel* m_user_name;
  QLabel* m_user_points;
  QLabel* m_game_icon;
  QLabel* m_game_name;
  QLabel* m_game_points;
  QProgressBar* m_game_progress_hard;
  QProgressBar* m_game_progress_soft;
  QLabel* m_rich_presence;
};
