// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef USE_RETRO_ACHIEVEMENTS
#include "DolphinQt/Achievements/AchievementLeaderboardWidget.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include <fmt/format.h>

#include <rcheevos/include/rc_api_runtime.h>
#include <rcheevos/include/rc_api_user.h>
#include <rcheevos/include/rc_runtime.h>

#include "Core/AchievementManager.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"

#include "DolphinQt/Config/ControllerInterface/ControllerInterfaceWindow.h"
#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/QtUtils/NonDefaultQPushButton.h"
#include "DolphinQt/QtUtils/SignalBlocking.h"
#include "DolphinQt/Settings.h"

AchievementLeaderboardWidget::AchievementLeaderboardWidget(QWidget* parent) : QWidget(parent)
{
  m_common_box = new QGroupBox();
  m_common_layout = new QGridLayout();

  UpdateData();

  m_common_box->setLayout(m_common_layout);

  auto* layout = new QVBoxLayout;
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setAlignment(Qt::AlignTop);
  layout->addWidget(m_common_box);
  setLayout(layout);
}

void AchievementLeaderboardWidget::UpdateData()
{
  auto leaderboards = AchievementManager::GetInstance()->GetLeaderboardsInfo();
  int row = 0;
  for (auto board_row : leaderboards)
  {
    AchievementManager::LeaderboardStatus board = board_row.second;
    QLabel* a_title = new QLabel(QString::fromStdString(board.name));
    QLabel* a_description = new QLabel(QString::fromStdString(board.description));
    QVBoxLayout* a_col_left = new QVBoxLayout();
    a_col_left->addWidget(a_title);
    a_col_left->addWidget(a_description);
    m_common_layout->addLayout(a_col_left, row, 0);
    // Each leaderboard entry is displayed with four values. These are *generally* intended to be,
    // in order, the first place entry, the entry one above the player, the player's entry, and
    // the entry one below the player.
    // Edge cases:
    // * If there are fewer than four entries in the leaderboard, all entries will be shown in
    //   order and the remainder of the list will be padded with empty values.
    // * If the player does not currently have a score in the leaderboard, or is in the top 3,
    //   the four slots will be the top four players in order.
    // * If the player is last place, the player will be in the fourth slot, and the second and
    //   third slots will be the two players above them. The first slot will always be first place.
    Rank to_display[4]{};
    for (size_t ix = 0; ix < 4; ix++)
      to_display[ix] = (board.entries.size() > ix) ? static_cast<Rank>(ix) : 0;
    if (board.player_rank > 3)
    {
      // If the rank one below than the player is found, offset = 1.
      Rank offset = static_cast<Rank>(board.entries.count(board.player_rank + 1));
      // Example: player is 10th place but not last
      // to_display = {1, 10-3+1+1, 10-3+1+2, 10-3+1+3} = {1, 9, 10, 11}
      // Example: player is 15th place and is last
      // to_display = {1, 15-3+0+1, 15-3+0+2, 15-3+0+3} = {1, 13, 14, 15}
      for (size_t ix = 1; ix < 4; ix++)
        to_display[ix] = board.player_rank - 3 + offset + static_cast<Rank>(ix);
    }
    for (size_t ix = 0; ix < 4; ix++)
    {
      Rank rank = to_display[ix];
      QLabel* a_rank = new QLabel(tr("---"));
      QLabel* a_username = new QLabel(tr("---"));
      QLabel* a_score = new QLabel(tr("---"));
      if (board.entries.count(rank) > 0)
      {
        a_rank->setText(tr("Rank %1").arg(rank));
        a_username->setText(QString::fromStdString(board.entries[rank].username));
        a_score->setText(QString::fromLocal8Bit(board.entries[rank].score.data()));
      }
      QVBoxLayout* a_col = new QVBoxLayout();
      a_col->addWidget(a_rank);
      a_col->addWidget(a_username);
      a_col->addWidget(a_score);
      m_common_layout->addLayout(a_col, row, static_cast<int>(ix) + 1);
    }
    row++;
  }
}

#endif  // USE_RETRO_ACHIEVEMENTS
