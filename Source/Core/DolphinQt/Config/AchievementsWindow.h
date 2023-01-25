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

class AchievementsWindow : public QDialog
{
  Q_OBJECT
public:
  explicit AchievementsWindow(QWidget* parent);

private:
  void CreateMainLayout();
  void ConnectWidgets();

  QDialogButtonBox* m_button_box;
  QTabWidget* m_tab_widget;
};
