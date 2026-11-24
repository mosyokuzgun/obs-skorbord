// "Skorbord" kaynağı: seçilen görseli çizer, üzerine takım adlarını, skorları ve periyodu yazar.

#include <obs-module.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "state.hpp"

enum Role { HOME_NAME = 0, HOME_SCORE, AWAY_SCORE, AWAY_NAME, PERIOD, ROLE_COUNT };

struct RoleInfo {
	const char *key;   // ayar anahtarı öneki
	const char *label; // ayarlar penceresindeki ad
	double x, y, w, h; // varsayılan: görsele oranla yüzde (merkez x/y, kutu genişlik/yükseklik)
	bool isName;
};

// Varsayılan yerler, Türkiye Basketbol 2. Ligi skorbord görseline göre ölçüldü
static const RoleInfo ROLES[ROLE_COUNT] = {
	{"home_name", "Ev sahibi adı", 26.07, 45.7, 13.9, 41.0, true},
	{"home_score", "Ev sahibi skoru", 37.63, 46.6, 9.7, 45.0, false},
	{"away_score", "Konuk skoru", 48.40, 46.6, 9.7, 45.0, false},
	{"away_name", "Konuk adı", 62.86, 45.7, 13.9, 41.0, true},
	{"period", "Periyot", 84.03, 47.0, 5.4, 27.0, false},
};

static const uint32_t DEFAULT_W = 1653;
static const uint32_t DEFAULT_H = 221;

struct SkorbordSource {
	obs_source_t *self = nullptr;
	obs_source_t *image = nullptr;
	obs_source_t *texts[ROLE_COUNT] = {};
	bool gdiplus = false;
	int listener = 0;

	// ayarlar
	std::string imagePath;
	std::string fontFace = "Arial";
	uint32_t nameColor = 0xFF111111;
	uint32_t scoreColor = 0xFFFFFFFF;
	uint32_t periodColor = 0xFFFFFFFF;
	std::string periodSuffix = ".Ç";
	bool showPeriod = true;
	double scoreSize = 0.95, nameSize = 0.36, periodSize = 0.70;
	double box[ROLE_COUNT][4] = {};
	uint32_t lastW = 0, lastH = 0;
};

static uint32_t base_w(SkorbordSource *s)
{
	uint32_t w = s->image ? obs_source_get_width(s->image) : 0;
	return w ? w : DEFAULT_W;
}
static uint32_t base_h(SkorbordSource *s)
{
	uint32_t h = s->image ? obs_source_get_height(s->image) : 0;
	return h ? h : DEFAULT_H;
}

// Kalın Arial için yaklaşık karakter genişliği (yazı boyutuna oranla)
static double est_width(const std::string &text, double font)
{
	// UTF-8 karakter sayısı
	size_t chars = 0;
	for (unsigned char c : text)
		if ((c & 0xC0) != 0x80)
			chars++;
	return chars * font * 0.62;
}

static void apply_text(SkorbordSource *s, int role, const std::string &text)
{
	obs_source_t *src = s->texts[role];
	if (!src)
		return;
	uint32_t W = base_w(s), H = base_h(s);
	double bw = s->box[role][2] / 100.0 * W;
	double bh = s->box[role][3] / 100.0 * H;

	bool isName = ROLES[role].isName;
	double font;
	bool wrap = false;
	uint32_t color;

	if (isName) {
		color = s->nameColor;
		// Önce tek satırda büyük yazmayı dene, sığmazsa iki satıra böl
		double big = bh * s->nameSize * 1.25;
		if (est_width(text, big) <= bw * 0.94) {
			font = big;
		} else {
			wrap = true;
			font = bh * s->nameSize;
			// En uzun satırı tahmin et (en ortadaki boşluktan bölünür)
			size_t mid = text.size() / 2, best = std::string::npos;
			for (size_t i = 0; i < text.size(); i++)
				if (text[i] == ' ' && (best == std::string::npos ||
						       std::llabs((long long)i - (long long)mid) <
							       std::llabs((long long)best - (long long)mid)))
					best = i;
			std::string longest = text;
			if (best != std::string::npos) {
				std::string a = text.substr(0, best), b = text.substr(best + 1);
				longest = est_width(a, 1) > est_width(b, 1) ? a : b;
			}
			double lw = est_width(longest, font);
			if (lw > bw * 0.94)
				font *= (bw * 0.94) / lw;
		}
	} else if (role == PERIOD) {
		color = s->periodColor;
		font = bh * s->periodSize;
		double tw = est_width(text, font);
		if (tw > bw * 0.98)
			font *= (bw * 0.98) / tw;
	} else {
		color = s->scoreColor;
		font = bh * s->scoreSize;
		double tw = est_width(text, font) * 0.9;
		if (tw > bw * 0.98)
			font *= (bw * 0.98) / tw;
	}

	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "text", text.c_str());

	obs_data_t *f = obs_data_create();
	obs_data_set_string(f, "face", s->fontFace.c_str());
	obs_data_set_int(f, "size", std::max(6, (int)std::lround(font)));
	obs_data_set_string(f, "style", isName || role == PERIOD ? "Bold" : "Regular");
	obs_data_set_int(f, "flags", isName || role == PERIOD ? 1 : 0); // 1 = kalın
	obs_data_set_obj(st, "font", f);
	obs_data_release(f);

	// Windows (GDI+) metin kaynağı
	obs_data_set_int(st, "color", color & 0xFFFFFF);
	obs_data_set_int(st, "opacity", 100);
	obs_data_set_string(st, "align", "center");
	obs_data_set_string(st, "valign", "center");
	obs_data_set_bool(st, "extents", true);
	obs_data_set_int(st, "extents_cx", (int)std::lround(bw));
	obs_data_set_int(st, "extents_cy", (int)std::lround(bh));
	obs_data_set_bool(st, "extents_wrap", wrap);
	// Linux/macOS (FreeType2) metin kaynağı
	obs_data_set_int(st, "color1", color | 0xFF000000);
	obs_data_set_int(st, "color2", color | 0xFF000000);
	obs_data_set_bool(st, "word_wrap", wrap);
	obs_data_set_int(st, "custom_width", wrap ? (int)std::lround(bw) : 0);

	obs_source_update(src, st);
	obs_data_release(st);
}

static void apply_state(SkorbordSource *s, const ScoreState &st)
{
	apply_text(s, HOME_NAME, st.homeName);
	apply_text(s, AWAY_NAME, st.awayName);
	apply_text(s, HOME_SCORE, std::to_string(st.homeScore));
	apply_text(s, AWAY_SCORE, std::to_string(st.awayScore));
	apply_text(s, PERIOD, s->showPeriod ? std::to_string(st.period) + s->periodSuffix : std::string());
}

/* ------------------------------------------------------------------ */

static const char *sb_get_name(void *)
{
	return "Skorbord";
}

static void sb_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<SkorbordSource *>(data);

	s->fontFace = "Arial";
	obs_data_t *font = obs_data_get_obj(settings, "font");
	if (font) {
		const char *face = obs_data_get_string(font, "face");
		if (face && *face)
			s->fontFace = face;
		obs_data_release(font);
	}
	s->nameColor = (uint32_t)obs_data_get_int(settings, "name_color") | 0xFF000000;
	s->scoreColor = (uint32_t)obs_data_get_int(settings, "score_color") | 0xFF000000;
	s->periodColor = (uint32_t)obs_data_get_int(settings, "period_color") | 0xFF000000;
	s->periodSuffix = obs_data_get_string(settings, "period_suffix");
	s->showPeriod = obs_data_get_bool(settings, "show_period");
	s->scoreSize = obs_data_get_double(settings, "score_size") / 100.0;
	s->nameSize = obs_data_get_double(settings, "name_size") / 100.0;

	for (int r = 0; r < ROLE_COUNT; r++) {
		const char *k = ROLES[r].key;
		s->box[r][0] = obs_data_get_double(settings, (std::string(k) + "_x").c_str());
		s->box[r][1] = obs_data_get_double(settings, (std::string(k) + "_y").c_str());
		s->box[r][2] = obs_data_get_double(settings, (std::string(k) + "_w").c_str());
		s->box[r][3] = obs_data_get_double(settings, (std::string(k) + "_h").c_str());
	}

	const char *path = obs_data_get_string(settings, "image");
	std::string newPath = path ? path : "";
	if (newPath != s->imagePath || (!s->image && !newPath.empty())) {
		s->imagePath = newPath;
		if (s->image) {
			obs_source_remove_active_child(s->self, s->image);
			obs_source_release(s->image);
			s->image = nullptr;
		}
		if (!newPath.empty()) {
			obs_data_t *is = obs_data_create();
			obs_data_set_string(is, "file", newPath.c_str());
			s->image = obs_source_create_private("image_source", "skorbord-gorsel", is);
			obs_data_release(is);
			if (s->image)
				obs_source_add_active_child(s->self, s->image);
		}
	}

	apply_state(s, state_get());
}

// Yüklü kaynak türleri arasında öneki eşleşen en yeni sürümü bul (ör. text_gdiplus_v3)
static std::string find_input_type(const char *prefix)
{
	std::string best;
	const char *id = nullptr;
	size_t n = strlen(prefix);
	for (size_t i = 0; obs_enum_input_types(i, &id); i++) {
		if (!id || strncmp(id, prefix, n) != 0)
			continue;
		const char *rest = id + n;
		if (*rest != '\0' && strncmp(rest, "_v", 2) != 0)
			continue;
		if (obs_get_source_output_flags(id) & OBS_SOURCE_DEPRECATED)
			continue;
		if (best.empty() || strcmp(id, best.c_str()) > 0)
			best = id;
	}
	return best;
}

static void *sb_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new SkorbordSource();
	s->self = source;

	std::string textId = find_input_type("text_gdiplus");
	s->gdiplus = !textId.empty();
	if (textId.empty())
		textId = find_input_type("text_ft2_source");
	if (textId.empty())
		textId = "text_ft2_source";

	for (int r = 0; r < ROLE_COUNT; r++) {
		obs_data_t *ts = obs_data_create();
		s->texts[r] = obs_source_create_private(textId.c_str(), ROLES[r].key, ts);
		obs_data_release(ts);
		if (s->texts[r])
			obs_source_add_active_child(source, s->texts[r]);
	}

	sb_update(s, settings);

	s->listener = state_add_listener([s](const ScoreState &st) { apply_state(s, st); });
	return s;
}

static void sb_destroy(void *data)
{
	auto *s = static_cast<SkorbordSource *>(data);
	state_remove_listener(s->listener);
	if (s->image) {
		obs_source_remove_active_child(s->self, s->image);
		obs_source_release(s->image);
	}
	for (auto &t : s->texts) {
		if (t) {
			obs_source_remove_active_child(s->self, t);
			obs_source_release(t);
		}
	}
	delete s;
}

static uint32_t sb_width(void *data)
{
	return base_w(static_cast<SkorbordSource *>(data));
}
static uint32_t sb_height(void *data)
{
	return base_h(static_cast<SkorbordSource *>(data));
}

static void draw_rect(gs_effect_t *solid, gs_eparam_t *color, float x, float y, float w, float h, uint32_t rgba)
{
	struct vec4 c;
	vec4_from_rgba(&c, rgba);
	gs_effect_set_vec4(color, &c);
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
	gs_matrix_pop();
}

static void sb_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<SkorbordSource *>(data);
	uint32_t W = base_w(s), H = base_h(s);

	if (s->image && obs_source_get_width(s->image) > 0) {
		obs_source_video_render(s->image);
	} else {
		// Görsel seçilmemişse basit bir tasarım çiz (ABGR renkler)
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
		draw_rect(solid, color, W * 0.04f, H * 0.28f, W * 0.92f, H * 0.44f, 0xF0302018);
		for (int r : {HOME_NAME, AWAY_NAME, PERIOD}) {
			float bw = (float)(s->box[r][2] / 100.0 * W), bh = (float)(s->box[r][3] / 100.0 * H);
			float x = (float)(s->box[r][0] / 100.0 * W) - bw / 2, y = (float)(s->box[r][1] / 100.0 * H) - bh / 2;
			draw_rect(solid, color, x, y, bw, bh, r == PERIOD ? 0xFF1E12A3 : 0xFFEEEEEE);
		}
	}

	for (int r = 0; r < ROLE_COUNT; r++) {
		obs_source_t *t = s->texts[r];
		if (!t)
			continue;
		double bw = s->box[r][2] / 100.0 * W, bh = s->box[r][3] / 100.0 * H;
		double cx = s->box[r][0] / 100.0 * W, cy = s->box[r][1] / 100.0 * H;
		// Metni kutunun ortasına yerleştir (GDI+ kutu boyutunda çizer, FreeType kendi boyutunda)
		double tw = obs_source_get_width(t), th = obs_source_get_height(t);
		(void)bw;
		(void)bh;
		if (tw <= 0 || th <= 0)
			continue;
		gs_matrix_push();
		gs_matrix_translate3f((float)std::floor(cx - tw / 2), (float)std::floor(cy - th / 2), 0.0f);
		obs_source_video_render(t);
		gs_matrix_pop();
	}
}

static void sb_tick(void *data, float)
{
	// Görselin boyutu değişirse (yeni görsel yüklendi) yazı boyutlarını yeniden hesapla
	auto *s = static_cast<SkorbordSource *>(data);
	uint32_t w = base_w(s), h = base_h(s);
	if (w != s->lastW || h != s->lastH) {
		s->lastW = w;
		s->lastH = h;
		apply_state(s, state_get());
	}
}

static void sb_enum_active(void *data, obs_source_enum_proc_t cb, void *param)
{
	auto *s = static_cast<SkorbordSource *>(data);
	if (s->image)
		cb(s->self, s->image, param);
	for (auto &t : s->texts)
		if (t)
			cb(s->self, t, param);
}

static void sb_defaults(obs_data_t *settings)
{
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_int(font, "size", 48);
	obs_data_set_string(font, "style", "Regular");
	obs_data_set_int(font, "flags", 0);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);

	obs_data_set_default_int(settings, "name_color", 0xFF111111);
	obs_data_set_default_int(settings, "score_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "period_color", 0xFFFFFFFF);
	obs_data_set_default_string(settings, "period_suffix", ".Ç");
	obs_data_set_default_bool(settings, "show_period", true);
	obs_data_set_default_double(settings, "score_size", 95.0);
	obs_data_set_default_double(settings, "name_size", 36.0);

	for (int r = 0; r < ROLE_COUNT; r++) {
		std::string k = ROLES[r].key;
		obs_data_set_default_double(settings, (k + "_x").c_str(), ROLES[r].x);
		obs_data_set_default_double(settings, (k + "_y").c_str(), ROLES[r].y);
		obs_data_set_default_double(settings, (k + "_w").c_str(), ROLES[r].w);
		obs_data_set_default_double(settings, (k + "_h").c_str(), ROLES[r].h);
	}
}

static bool reset_layout(obs_properties_t *, obs_property_t *, void *data)
{
	auto *s = static_cast<SkorbordSource *>(data);
	obs_data_t *settings = obs_source_get_settings(s->self);
	for (int r = 0; r < ROLE_COUNT; r++) {
		std::string k = ROLES[r].key;
		for (const char *suf : {"_x", "_y", "_w", "_h"})
			obs_data_unset_user_value(settings, (k + suf).c_str());
	}
	obs_source_update(s->self, settings);
	obs_data_release(settings);
	return true;
}

static obs_properties_t *sb_properties(void *)
{
	obs_properties_t *p = obs_properties_create();

	obs_properties_add_text(p, "info",
				"Takım adlarını, skorları ve periyodu OBS'teki \"Skorbord\" panelinden değiştirin "
				"(Paneller menüsü → Skorbord).",
				OBS_TEXT_INFO);

	obs_properties_add_path(p, "image", "Skorbord görseli", OBS_PATH_FILE,
				"Görseller (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;Tüm dosyalar (*.*)", nullptr);

	obs_properties_add_font(p, "font", "Yazı tipi (sadece yazı tipi adı kullanılır)");
	obs_properties_add_color(p, "name_color", "Takım adı rengi");
	obs_properties_add_color(p, "score_color", "Skor rengi");
	obs_properties_add_color(p, "period_color", "Periyot rengi");
	obs_properties_add_float_slider(p, "score_size", "Skor yazı boyutu (%)", 30, 130, 1);
	obs_properties_add_float_slider(p, "name_size", "Takım adı yazı boyutu (%)", 15, 70, 1);

	obs_property_t *suf = obs_properties_add_list(p, "period_suffix", "Periyot yazısı", OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(suf, "1.Ç (çeyrek)", ".Ç");
	obs_property_list_add_string(suf, "1.Y (yarı)", ".Y");
	obs_property_list_add_string(suf, "1.P (periyot)", ".P");
	obs_property_list_add_string(suf, "1. SET", ". SET");
	obs_property_list_add_string(suf, "Sadece sayı", "");
	obs_properties_add_bool(p, "show_period", "Periyodu göster");

	obs_properties_t *lay = obs_properties_create();
	obs_properties_add_text(lay, "lay_info",
				"Başka bir görsel kullanıyorsanız yazıların yerini buradan ayarlayın. Değerler görselin "
				"genişlik/yüksekliğine göre yüzdedir (X/Y = kutunun ortası).",
				OBS_TEXT_INFO);
	for (int r = 0; r < ROLE_COUNT; r++) {
		std::string k = ROLES[r].key, l = ROLES[r].label;
		obs_properties_add_float_slider(lay, (k + "_x").c_str(), (l + " – X").c_str(), 0, 100, 0.1);
		obs_properties_add_float_slider(lay, (k + "_y").c_str(), (l + " – Y").c_str(), 0, 100, 0.1);
		obs_properties_add_float_slider(lay, (k + "_w").c_str(), (l + " – genişlik").c_str(), 1, 100, 0.1);
		obs_properties_add_float_slider(lay, (k + "_h").c_str(), (l + " – yükseklik").c_str(), 1, 100, 0.1);
	}
	obs_properties_add_button(lay, "reset_layout", "Yerleşimi varsayılana döndür", reset_layout);
	obs_properties_add_group(p, "layout", "Yerleşim (gelişmiş)", OBS_GROUP_NORMAL, lay);

	return p;
}

void skorbord_source_register()
{
	struct obs_source_info info = {};
	info.id = "skorbord_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = sb_get_name;
	info.create = sb_create;
	info.destroy = sb_destroy;
	info.update = sb_update;
	info.get_defaults = sb_defaults;
	info.get_properties = sb_properties;
	info.get_width = sb_width;
	info.get_height = sb_height;
	info.video_render = sb_render;
	info.video_tick = sb_tick;
	info.enum_active_sources = sb_enum_active;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}
