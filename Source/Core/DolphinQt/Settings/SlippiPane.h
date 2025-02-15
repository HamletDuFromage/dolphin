#pragma once

#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

class QLineEdit;

namespace Core
{
enum class State;
}

class SlippiPane final : public QWidget
{
  Q_OBJECT
public:
  explicit SlippiPane(QWidget* parent = nullptr);

private:
  void SetSaveReplays(bool checked);
  void BrowseReplayFolder();
  void ToggleJukebox(bool checked);
  void SetForceNetplayPort(bool checked);
  void OnMusicVolumeUpdate(int volume);
  void CreateLayout();
  void LoadConfig();
  void ConnectLayout();
  void OnSaveConfig();
  void OnCharacterBanlistClick();
  void CharacterClicked(QListWidgetItem *item);
  void OnPlayerBlocklistClick();
  void PlayerCodeAdded();
  void PlayerCodeDeleted();
  void SavePlayerBlockList();

  QVBoxLayout* m_main_layout;

  // Replay Settings
  QCheckBox* m_save_replays;
  QCheckBox* m_monthly_replay_folders;
  QLineEdit* m_replay_folder_edit;
  QPushButton* m_replay_folder_open;

  // Online Settings
  QSpinBox* m_delay_spin;
  QComboBox* m_netplay_quick_chat_combo;
  QCheckBox* m_force_netplay_port;
  QSpinBox* m_netplay_port;

  // Jukebox Settings
  QCheckBox* m_enable_jukebox;
  QSlider* m_music_volume_slider;
  QLabel* m_music_volume_percent;

  // Banlist Settings
  QPushButton* m_character_banlist;
  QListWidget* m_character_checkboxes;
  QPushButton* m_player_blocklist;
  QTableWidget* m_player_block_table;
  QLineEdit* m_player_code;
};
