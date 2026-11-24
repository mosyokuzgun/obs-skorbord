/*
OBS Skorbord
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QApplication>
#include <QMetaObject>

#include <map>
#include <mutex>

#include "state.hpp"

extern "C" {
#include <plugin-support.h>
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Skorbord: kendi gorseliniz uzerinde skor, takim adi ve periyot; OBS paneli ve kisayol tuslari.";
}

/* ------------------------------------------------------------------ */
/* Durum                                                               */
/* ------------------------------------------------------------------ */

static std::mutex g_mutex;
static ScoreState g_state;
static std::map<int, StateListener> g_listeners;
static int g_next_listener = 1;

ScoreState state_get()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_state;
}

void state_modify(const std::function<void(ScoreState &)> &fn)
{
	ScoreState copy;
	std::map<int, StateListener> listeners;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		fn(g_state);
		if (g_state.homeScore < 0)
			g_state.homeScore = 0;
		if (g_state.awayScore < 0)
			g_state.awayScore = 0;
		if (g_state.period < 1)
			g_state.period = 1;
		copy = g_state;
		listeners = g_listeners;
	}
	state_save();
	for (auto &l : listeners)
		l.second(copy);
}

int state_add_listener(StateListener fn)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	int id = g_next_listener++;
	g_listeners[id] = std::move(fn);
	return id;
}

void state_remove_listener(int id)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_listeners.erase(id);
}

void run_on_ui(std::function<void()> fn)
{
	if (qApp)
		QMetaObject::invokeMethod(qApp, [fn]() { fn(); }, Qt::QueuedConnection);
	else
		fn();
}

static std::string config_file(const char *name)
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *path = obs_module_config_path(name);
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

void state_load()
{
	std::string path = config_file("state.json");
	obs_data_t *data = obs_data_create_from_json_file(path.c_str());
	if (!data)
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	if (obs_data_has_user_value(data, "homeName"))
		g_state.homeName = obs_data_get_string(data, "homeName");
	if (obs_data_has_user_value(data, "awayName"))
		g_state.awayName = obs_data_get_string(data, "awayName");
	g_state.homeScore = (int)obs_data_get_int(data, "homeScore");
	g_state.awayScore = (int)obs_data_get_int(data, "awayScore");
	g_state.period = (int)obs_data_get_int(data, "period");
	if (g_state.period < 1)
		g_state.period = 1;
	obs_data_release(data);
}

void state_save()
{
	ScoreState s = state_get();
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "homeName", s.homeName.c_str());
	obs_data_set_string(data, "awayName", s.awayName.c_str());
	obs_data_set_int(data, "homeScore", s.homeScore);
	obs_data_set_int(data, "awayScore", s.awayScore);
	obs_data_set_int(data, "period", s.period);
	std::string path = config_file("state.json");
	obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
	obs_data_release(data);
}

/* ------------------------------------------------------------------ */
/* Kısayol tuşları                                                     */
/* ------------------------------------------------------------------ */

struct HotkeyDef {
	const char *name;
	const char *desc;
	std::function<void(ScoreState &)> action;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

static std::vector<HotkeyDef> g_hotkeys;

static void hotkey_cb(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *def = static_cast<HotkeyDef *>(data);
	auto action = def->action;
	run_on_ui([action]() { state_modify(action); });
}

void skorbord_hotkeys_register()
{
	g_hotkeys = {
		{"skorbord.home_plus1", "Skorbord: Ev sahibi +1", [](ScoreState &s) { s.homeScore += 1; }},
		{"skorbord.home_plus2", "Skorbord: Ev sahibi +2", [](ScoreState &s) { s.homeScore += 2; }},
		{"skorbord.home_plus3", "Skorbord: Ev sahibi +3", [](ScoreState &s) { s.homeScore += 3; }},
		{"skorbord.home_minus1", "Skorbord: Ev sahibi -1", [](ScoreState &s) { s.homeScore -= 1; }},
		{"skorbord.away_plus1", "Skorbord: Konuk +1", [](ScoreState &s) { s.awayScore += 1; }},
		{"skorbord.away_plus2", "Skorbord: Konuk +2", [](ScoreState &s) { s.awayScore += 2; }},
		{"skorbord.away_plus3", "Skorbord: Konuk +3", [](ScoreState &s) { s.awayScore += 3; }},
		{"skorbord.away_minus1", "Skorbord: Konuk -1", [](ScoreState &s) { s.awayScore -= 1; }},
		{"skorbord.period_plus", "Skorbord: Periyot +1", [](ScoreState &s) { s.period += 1; }},
		{"skorbord.period_minus", "Skorbord: Periyot -1", [](ScoreState &s) { s.period -= 1; }},
	};

	std::string path = config_file("hotkeys.json");
	obs_data_t *saved = obs_data_create_from_json_file(path.c_str());

	for (auto &h : g_hotkeys) {
		h.id = obs_hotkey_register_frontend(h.name, h.desc, hotkey_cb, &h);
		if (saved) {
			obs_data_array_t *arr = obs_data_get_array(saved, h.name);
			if (arr) {
				obs_hotkey_load(h.id, arr);
				obs_data_array_release(arr);
			}
		}
	}
	if (saved)
		obs_data_release(saved);
}

void skorbord_hotkeys_save()
{
	obs_data_t *data = obs_data_create();
	for (auto &h : g_hotkeys) {
		if (h.id == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *arr = obs_hotkey_save(h.id);
		obs_data_set_array(data, h.name, arr);
		obs_data_array_release(arr);
	}
	std::string path = config_file("hotkeys.json");
	obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
	obs_data_release(data);
}

static void frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		// Panel ilk kurulumda bir kez otomatik açılır; sonra kullanıcının tercihi korunur
		std::string flag = config_file("first_run_done");
		if (!os_file_exists(flag.c_str())) {
			skorbord_dock_show_first_time();
			os_quick_write_utf8_file(flag.c_str(), "1", 1, false);
		}
	}
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		skorbord_hotkeys_save();
		state_save();
	}
}

/* ------------------------------------------------------------------ */
/* Modül                                                               */
/* ------------------------------------------------------------------ */

bool obs_module_load(void)
{
	state_load();
	skorbord_source_register();
	skorbord_hotkeys_register();
	obs_frontend_add_event_callback(frontend_event, nullptr);
	obs_log(LOG_INFO, "Skorbord yuklendi (surum %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	skorbord_dock_create();
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	for (auto &h : g_hotkeys) {
		if (h.id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(h.id);
	}
	g_hotkeys.clear();
}
