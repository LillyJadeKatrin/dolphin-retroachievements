// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// 15 JAN 2013 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#pragma once
#include <QDialog>

class CommonControllersWidget;
class GamecubeControllersWidget;
class QDialogButtonBox;
class WiimoteControllersWidget;

class AchievementsWindow : public QDialog
{
  Q_OBJECT
public:
  explicit AchievementsWindow(QWidget* parent);

private:
  void CreateMainLayout();
  void ConnectWidgets();

  QDialogButtonBox* m_button_box;
  GamecubeControllersWidget* m_gamecube_controllers;
  WiimoteControllersWidget* m_wiimote_controllers;
  CommonControllersWidget* m_common;
};
