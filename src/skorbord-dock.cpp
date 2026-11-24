// OBS içindeki "Skorbord" paneli: takım adları, skorlar ve periyot.

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QDockWidget>
#include <QFrame>
#include <QMainWindow>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "state.hpp"

namespace {

QPushButton *make_button(const QString &text, const QString &bg, int h, int fontPx)
{
	auto *b = new QPushButton(text);
	b->setMinimumHeight(h);
	b->setCursor(Qt::PointingHandCursor);
	b->setFocusPolicy(Qt::NoFocus);
	b->setStyleSheet(QString("QPushButton{background:%1;color:white;border:none;border-radius:6px;"
				 "font-size:%2px;font-weight:bold;padding:0 6px;}"
				 "QPushButton:hover{background:%1;border:1px solid rgba(255,255,255,0.5);}"
				 "QPushButton:pressed{padding-top:2px;}")
				 .arg(bg)
				 .arg(fontPx));
	return b;
}

struct TeamWidgets {
	QLineEdit *name = nullptr;
	QLabel *score = nullptr;
};

class SkorbordDock : public QWidget {
public:
	SkorbordDock()
	{
		setMinimumWidth(250);
		auto *outer = new QVBoxLayout(this);
		outer->setContentsMargins(0, 0, 0, 0);
		auto *scroll = new QScrollArea();
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		outer->addWidget(scroll);

		auto *content = new QWidget();
		auto *v = new QVBoxLayout(content);
		v->setContentsMargins(6, 6, 6, 6);
		v->setSpacing(8);
		scroll->setWidget(content);

		home = add_team(v, "EV SAHİBİ", true);
		away = add_team(v, "KONUK", false);

		// Periyot
		auto *perBox = new QFrame();
		perBox->setObjectName("sbcard");
		auto *ph = new QHBoxLayout(perBox);
		ph->setContentsMargins(8, 6, 8, 6);
		auto *pl = new QLabel("PERİYOT");
		pl->setStyleSheet("color:#9aa0ab;font-size:11px;font-weight:bold;");
		period = new QLabel("1");
		period->setAlignment(Qt::AlignCenter);
		period->setStyleSheet("font-size:24px;font-weight:bold;");
		auto *pm = make_button("−", "#8b1d1d", 36, 22);
		auto *pp = make_button("+", "#15803d", 36, 22);
		pm->setFixedWidth(46);
		pp->setFixedWidth(46);
		ph->addWidget(pl);
		ph->addStretch();
		ph->addWidget(pm);
		ph->addWidget(period, 1);
		ph->addWidget(pp);
		v->addWidget(perBox);
		connect(pm, &QPushButton::clicked, [] { state_modify([](ScoreState &s) { s.period -= 1; }); });
		connect(pp, &QPushButton::clicked, [] { state_modify([](ScoreState &s) { s.period += 1; }); });

		// Alt butonlar
		auto *bottom = new QHBoxLayout();
		auto *swap = make_button("Takımları değiştir", "#3b3e45", 30, 12);
		auto *reset = make_button("Skorları sıfırla", "#3b3e45", 30, 12);
		bottom->addWidget(swap);
		bottom->addWidget(reset);
		v->addLayout(bottom);
		v->addStretch();

		connect(swap, &QPushButton::clicked, [] {
			state_modify([](ScoreState &s) {
				std::swap(s.homeName, s.awayName);
				std::swap(s.homeScore, s.awayScore);
			});
		});
		connect(reset, &QPushButton::clicked, [this] {
			if (QMessageBox::question(this, "Skorbord", "Skorlar ve periyot sıfırlansın mı?") ==
			    QMessageBox::Yes)
				state_modify([](ScoreState &s) {
					s.homeScore = 0;
					s.awayScore = 0;
					s.period = 1;
				});
		});

		setStyleSheet("QFrame#sbcard{background:rgba(127,127,127,0.12);border-radius:8px;}"
			      "QLineEdit{font-size:15px;font-weight:bold;padding:5px;}");

		refresh(state_get());
		listener = state_add_listener([this](const ScoreState &st) { refresh(st); });
	}

	~SkorbordDock() override { state_remove_listener(listener); }

private:
	TeamWidgets home, away;
	QLabel *period = nullptr;
	int listener = 0;

	TeamWidgets add_team(QVBoxLayout *v, const QString &title, bool isHome)
	{
		TeamWidgets w;
		auto *card = new QFrame();
		card->setObjectName("sbcard");
		auto *cv = new QVBoxLayout(card);
		cv->setContentsMargins(8, 6, 8, 8);
		cv->setSpacing(6);

		auto *lbl = new QLabel(title);
		lbl->setStyleSheet("color:#9aa0ab;font-size:11px;font-weight:bold;");
		cv->addWidget(lbl);

		w.name = new QLineEdit();
		w.name->setPlaceholderText(isHome ? "Ev sahibi adı" : "Konuk adı");
		cv->addWidget(w.name);

		auto *row = new QHBoxLayout();
		auto *minus = make_button("−", "#8b1d1d", 52, 30);
		auto *plus = make_button("+", "#15803d", 52, 30);
		minus->setFixedWidth(60);
		plus->setFixedWidth(60);
		w.score = new QLabel("0");
		w.score->setAlignment(Qt::AlignCenter);
		w.score->setStyleSheet("font-size:40px;font-weight:bold;");
		row->addWidget(minus);
		row->addWidget(w.score, 1);
		row->addWidget(plus);
		cv->addLayout(row);

		auto *quick = new QHBoxLayout();
		auto *p2 = make_button("+2", "#14532d", 30, 14);
		auto *p3 = make_button("+3", "#14532d", 30, 14);
		quick->addWidget(p2);
		quick->addWidget(p3);
		cv->addLayout(quick);
		v->addWidget(card);

		auto add = [isHome](int n) {
			state_modify([isHome, n](ScoreState &s) { (isHome ? s.homeScore : s.awayScore) += n; });
		};
		connect(minus, &QPushButton::clicked, [add] { add(-1); });
		connect(plus, &QPushButton::clicked, [add] { add(1); });
		connect(p2, &QPushButton::clicked, [add] { add(2); });
		connect(p3, &QPushButton::clicked, [add] { add(3); });
		QLineEdit *edit = w.name;
		connect(edit, &QLineEdit::textEdited, [isHome](const QString &t) {
			std::string val = t.toUtf8().constData();
			state_modify([isHome, val](ScoreState &s) { (isHome ? s.homeName : s.awayName) = val; });
		});
		return w;
	}

	void refresh(const ScoreState &st)
	{
		set_name(home.name, st.homeName);
		set_name(away.name, st.awayName);
		home.score->setText(QString::number(st.homeScore));
		away.score->setText(QString::number(st.awayScore));
		period->setText(QString::number(st.period));
	}

	static void set_name(QLineEdit *e, const std::string &v)
	{
		QString q = QString::fromUtf8(v.c_str());
		if (e->text() != q && !e->hasFocus())
			e->setText(q);
	}
};

} // namespace

static QWidget *g_dock = nullptr;

void skorbord_dock_create()
{
	auto *dock = new SkorbordDock();
	if (!obs_frontend_add_dock_by_id("skorbord_dock", "Skorbord", dock)) {
		delete dock;
		return;
	}
	g_dock = dock;
}

// İlk kurulumda paneli ana pencerenin sağına yerleştirip göster
void skorbord_dock_show_first_time()
{
	if (!g_dock)
		return;
	auto *dw = qobject_cast<QDockWidget *>(g_dock->parentWidget());
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!dw || !main)
		return;
	dw->setFloating(false);
	main->addDockWidget(Qt::RightDockWidgetArea, dw);
	dw->setVisible(true);
}
