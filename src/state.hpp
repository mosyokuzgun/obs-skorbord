#pragma once

#include <functional>
#include <string>

// Skorbordun ortak durumu: tüm Skorbord kaynakları ve panel aynı durumu paylaşır.
struct ScoreState {
	std::string homeName = "EV SAHİBİ";
	std::string awayName = "KONUK";
	int homeScore = 0;
	int awayScore = 0;
	int period = 1;
};

// Durumu oku (kopya döner)
ScoreState state_get();

// Durumu değiştir: değişikliği uygular, diske kaydeder, kaynakları ve paneli günceller.
// Sadece Qt arayüz (ana) iş parçacığından çağrılmalı.
void state_modify(const std::function<void(ScoreState &)> &fn);

// Ana iş parçacığında çalıştır (kısayol tuşları başka iş parçacığından gelir)
void run_on_ui(std::function<void()> fn);

void state_load();
void state_save();

// Durum değişince çağrılacak dinleyiciler (panel, kaynaklar)
using StateListener = std::function<void(const ScoreState &)>;
int state_add_listener(StateListener fn);
void state_remove_listener(int id);

// Modül parçaları
void skorbord_source_register();
void skorbord_dock_create();
void skorbord_dock_show_first_time();
void skorbord_hotkeys_register();
void skorbord_hotkeys_save();
