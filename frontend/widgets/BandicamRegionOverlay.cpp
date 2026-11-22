/******************************************************************************
    Bandicam-style interactive screen region selector and floating recording
    frame overlay for OBS Recorder.
******************************************************************************/

#include "BandicamRegionOverlay.hpp"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

BandicamRegionSelector::BandicamRegionSelector(QWidget *parent)
	: QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
	setAttribute(Qt::WA_TranslucentBackground, true);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setMouseTracking(true);
	setCursor(Qt::CrossCursor);

	presetBar = new QWidget(this);
	presetBar->setObjectName(QStringLiteral("bandicamRegionPresetBar"));
	presetBar->setCursor(Qt::ArrowCursor);
	presetBar->setStyleSheet(QStringLiteral(
		"QWidget#bandicamRegionPresetBar { background: rgba(20, 22, 27, 238); border: 1px solid #3a404d; "
		"border-radius: 10px; }"
		"QLabel { color: #d6dbe4; font-size: 12px; font-weight: 600; padding: 0 6px; }"
		"QPushButton { background: #2c303a; color: #f2f4f8; border: 1px solid #454c5b; border-radius: 6px; "
		"padding: 5px 10px; font-size: 12px; font-weight: 600; }"
		"QPushButton:hover { background: #3b4252; border-color: #6382b8; }"
		"QPushButton#cancelBtn { background: #5a262c; border-color: #8c3d45; }"
		"QPushButton#cancelBtn:hover { background: #753038; }"));

	auto *layout = new QHBoxLayout(presetBar);
	layout->setContentsMargins(10, 7, 10, 7);
	layout->setSpacing(6);

	auto *hint = new QLabel(QStringLiteral("Выделите область экрана:"), presetBar);
	layout->addWidget(hint);

	auto addPreset = [this, layout](const QString &label, int w, int h) {
		auto *btn = new QPushButton(label, presetBar);
		connect(btn, &QPushButton::clicked, this, [this, w, h]() { ApplyPresetSize(w, h); });
		layout->addWidget(btn);
	};

	addPreset(QStringLiteral("1920×1080"), 1920, 1080);
	addPreset(QStringLiteral("1280×720"), 1280, 720);
	addPreset(QStringLiteral("854×480"), 854, 480);

	auto *fullBtn = new QPushButton(QStringLiteral("Весь экран"), presetBar);
	connect(fullBtn, &QPushButton::clicked, this, &BandicamRegionSelector::SelectFullCurrentScreen);
	layout->addWidget(fullBtn);

	auto *cancelBtn = new QPushButton(QStringLiteral("Отмена (Esc)"), presetBar);
	cancelBtn->setObjectName(QStringLiteral("cancelBtn"));
	connect(cancelBtn, &QPushButton::clicked, this, [this]() {
		hide();
		emit canceled();
	});
	layout->addWidget(cancelBtn);
}

void BandicamRegionSelector::BeginSelection()
{
	QRect combined;
	for (QScreen *screen : QGuiApplication::screens()) {
		combined = combined.united(screen->geometry());
	}
	if (combined.isEmpty()) {
		if (QScreen *primary = QGuiApplication::primaryScreen())
			combined = primary->geometry();
		else
			combined = QRect(0, 0, 1920, 1080);
	}

	virtualGeometry = combined;
	dragging = false;
	hasSelection = false;
	lockedSelection = QRect();

	setGeometry(virtualGeometry);

	QScreen *cursorScreen = QGuiApplication::screenAt(QCursor::pos());
	if (!cursorScreen)
		cursorScreen = QGuiApplication::primaryScreen();

	presetBar->adjustSize();
	if (cursorScreen) {
		const QRect localScreen = cursorScreen->geometry().translated(-virtualGeometry.topLeft());
		const int x = localScreen.left() + (localScreen.width() - presetBar->width()) / 2;
		const int y = localScreen.top() + 20;
		presetBar->move(std::max(8, x), std::max(8, y));
	}

	show();
	raise();
	activateWindow();
	setFocus();
	update();
}

QRect BandicamRegionSelector::CurrentSelectionRect() const
{
	if (dragging)
		return QRect(dragStart, dragCurrent).normalized();
	if (hasSelection)
		return lockedSelection;
	return QRect();
}

QScreen *BandicamRegionSelector::ScreenForLocalRect(const QRect &localRect) const
{
	const QPoint globalCenter = localRect.center() + virtualGeometry.topLeft();
	QScreen *screen = QGuiApplication::screenAt(globalCenter);
	if (!screen)
		screen = QGuiApplication::primaryScreen();
	return screen;
}

void BandicamRegionSelector::ApplyPresetSize(int width, int height)
{
	QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
	if (!screen)
		screen = QGuiApplication::primaryScreen();
	if (!screen)
		return;

	const qreal dpr = std::max<qreal>(1.0, screen->devicePixelRatio());
	const int logicalW = std::min(screen->geometry().width(), static_cast<int>(std::round(width / dpr)));
	const int logicalH = std::min(screen->geometry().height(), static_cast<int>(std::round(height / dpr)));
	const QRect sg = screen->geometry();
	const QRect globalRect(sg.left() + (sg.width() - logicalW) / 2, sg.top() + (sg.height() - logicalH) / 2,
			       logicalW, logicalH);

	hide();
	emit regionSelected(globalRect, screen);
}

void BandicamRegionSelector::SelectFullCurrentScreen()
{
	QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
	if (!screen)
		screen = QGuiApplication::primaryScreen();
	if (!screen)
		return;

	hide();
	emit regionSelected(screen->geometry(), screen);
}

void BandicamRegionSelector::FinalizeSelection(const QRect &localRect)
{
	if (localRect.width() < 24 || localRect.height() < 24)
		return;

	QScreen *screen = ScreenForLocalRect(localRect);
	if (!screen)
		return;

	QRect globalRect = localRect.translated(virtualGeometry.topLeft());
	globalRect = globalRect.intersected(screen->geometry());
	if (globalRect.width() < 24 || globalRect.height() < 24)
		return;

	hide();
	emit regionSelected(globalRect, screen);
}

void BandicamRegionSelector::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	const QRect sel = CurrentSelectionRect();

	QPainterPath dimPath;
	dimPath.addRect(rect());
	if (!sel.isEmpty()) {
		QPainterPath cutout;
		cutout.addRect(sel);
		dimPath = dimPath.subtracted(cutout);
	}
	p.fillPath(dimPath, QColor(12, 14, 18, 145));

	if (sel.isEmpty()) {
		const QPoint cursorLocal = mapFromGlobal(QCursor::pos());
		p.setPen(QPen(QColor(59, 130, 246, 140), 1, Qt::DashLine));
		p.drawLine(cursorLocal.x(), 0, cursorLocal.x(), height());
		p.drawLine(0, cursorLocal.y(), width(), cursorLocal.y());
		return;
	}

	// Crisp Bandicam-style green selection border + corner L-brackets
	p.setPen(QPen(QColor("#22c55e"), 2));
	p.setBrush(Qt::NoBrush);
	p.drawRect(sel.adjusted(0, 0, -1, -1));

	const int cornerLen = std::min({18, sel.width() / 3, sel.height() / 3});
	p.setPen(QPen(QColor("#4ade80"), 4, Qt::SolidLine, Qt::SquareCap));
	// Top-left
	p.drawLine(sel.left(), sel.top(), sel.left() + cornerLen, sel.top());
	p.drawLine(sel.left(), sel.top(), sel.left(), sel.top() + cornerLen);
	// Top-right
	p.drawLine(sel.right() - cornerLen, sel.top(), sel.right(), sel.top());
	p.drawLine(sel.right(), sel.top(), sel.right(), sel.top() + cornerLen);
	// Bottom-left
	p.drawLine(sel.left(), sel.bottom(), sel.left() + cornerLen, sel.bottom());
	p.drawLine(sel.left(), sel.bottom() - cornerLen, sel.left(), sel.bottom());
	// Bottom-right
	p.drawLine(sel.right() - cornerLen, sel.bottom(), sel.right(), sel.bottom());
	p.drawLine(sel.right(), sel.bottom() - cornerLen, sel.right(), sel.bottom());

	// Physical pixel readout pill
	QScreen *screen = ScreenForLocalRect(sel);
	const qreal dpr = screen ? std::max<qreal>(1.0, screen->devicePixelRatio()) : 1.0;
	const int physW = static_cast<int>(std::round(sel.width() * dpr));
	const int physH = static_cast<int>(std::round(sel.height() * dpr));
	const QString dimText = QStringLiteral("%1 × %2 px").arg(physW).arg(physH);

	QFont f = font();
	f.setBold(true);
	f.setPointSize(10);
	p.setFont(f);
	const QFontMetrics fm(f);
	const QSize textSize = fm.size(Qt::TextSingleLine, dimText);
	const int pillW = textSize.width() + 20;
	const int pillH = textSize.height() + 10;

	int pillX = sel.left();
	int pillY = sel.top() - pillH - 8;
	if (pillY < 8)
		pillY = sel.top() + 8;
	if (pillX + pillW > width() - 8)
		pillX = width() - pillW - 8;

	const QRect pillRect(pillX, pillY, pillW, pillH);
	p.setPen(QPen(QColor("#22c55e"), 1));
	p.setBrush(QColor(20, 22, 27, 235));
	p.drawRoundedRect(pillRect, 6, 6);
	p.setPen(QColor("#f8fafc"));
	p.drawText(pillRect, Qt::AlignCenter, dimText);
}

void BandicamRegionSelector::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton) {
		hide();
		emit canceled();
		return;
	}
	if (event->button() != Qt::LeftButton)
		return;

	if (presetBar && presetBar->geometry().contains(event->pos()))
		return;

	dragging = true;
	hasSelection = false;
	dragStart = event->pos();
	dragCurrent = event->pos();
	update();
}

void BandicamRegionSelector::mouseMoveEvent(QMouseEvent *event)
{
	if (dragging)
		dragCurrent = event->pos();
	update();
}

void BandicamRegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !dragging)
		return;

	dragging = false;
	dragCurrent = event->pos();
	const QRect sel = QRect(dragStart, dragCurrent).normalized();
	if (sel.width() >= 24 && sel.height() >= 24) {
		lockedSelection = sel;
		hasSelection = true;
		FinalizeSelection(sel);
	} else {
		update();
	}
}

void BandicamRegionSelector::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape) {
		hide();
		emit canceled();
		return;
	}
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
		const QRect sel = CurrentSelectionRect();
		if (sel.width() >= 24 && sel.height() >= 24)
			FinalizeSelection(sel);
		return;
	}
	QWidget::keyPressEvent(event);
}

// ============================================================================
// BandicamRegionFrame: Click-through floating border around the capture area
// ============================================================================

BandicamRegionFrame::BandicamRegionFrame(QWidget *parent)
	: QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool |
				  Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
{
	setAttribute(Qt::WA_TranslucentBackground, true);
	setAttribute(Qt::WA_TransparentForMouseEvents, true);
	setAttribute(Qt::WA_ShowWithoutActivating, true);
}

void BandicamRegionFrame::SetTargetRegion(const QRect &globalLogicalRect)
{
	targetRect = globalLogicalRect;
	if (!targetRect.isValid() || targetRect.width() < 24 || targetRect.height() < 24) {
		hide();
		return;
	}
	UpdateGeometryForTarget();
	if (frameEnabled) {
		show();
		ExcludeFromCapture();
		update();
	}
}

void BandicamRegionFrame::SetState(State state, const QString &statusText)
{
	currentState = state;
	bannerText = statusText;
	if (isVisible())
		update();
}

void BandicamRegionFrame::SetFrameEnabled(bool enabled)
{
	frameEnabled = enabled;
	if (!frameEnabled) {
		hide();
	} else if (targetRect.isValid() && targetRect.width() >= 24 && targetRect.height() >= 24) {
		UpdateGeometryForTarget();
		show();
		ExcludeFromCapture();
		update();
	}
}

void BandicamRegionFrame::UpdateGeometryForTarget()
{
	const int pad = kBorderThickness + kOuterMargin;
	const QRect outer(targetRect.left() - pad, targetRect.top() - pad - kTopBannerHeight,
			  targetRect.width() + pad * 2, targetRect.height() + pad * 2 + kTopBannerHeight);
	setGeometry(outer);
}

void BandicamRegionFrame::ExcludeFromCapture()
{
#ifdef _WIN32
	HWND hwnd = reinterpret_cast<HWND>(winId());
	if (hwnd) {
		SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
		LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE);
	}
#endif
}

void BandicamRegionFrame::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	ExcludeFromCapture();
}

void BandicamRegionFrame::paintEvent(QPaintEvent *)
{
	if (!targetRect.isValid())
		return;

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	QColor accent("#22c55e");
	QString label = bannerText;
	if (currentState == State::Recording) {
		accent = QColor("#ef4444");
		if (label.isEmpty())
			label = QStringLiteral("● REC");
	} else if (currentState == State::Paused) {
		accent = QColor("#f59e0b");
		if (label.isEmpty())
			label = QStringLiteral("Ⅱ ПАУЗА");
	} else if (label.isEmpty()) {
		QScreen *screen = QGuiApplication::screenAt(targetRect.center());
		const qreal dpr = screen ? std::max<qreal>(1.0, screen->devicePixelRatio()) : 1.0;
		label = QStringLiteral("Область %1×%2")
				.arg(static_cast<int>(std::round(targetRect.width() * dpr)))
				.arg(static_cast<int>(std::round(targetRect.height() * dpr)));
	}

	const int pad = kBorderThickness + kOuterMargin;
	const QRect frameRect(pad - kBorderThickness, kTopBannerHeight + pad - kBorderThickness,
			      targetRect.width() + kBorderThickness * 2, targetRect.height() + kBorderThickness * 2);

	// Outer dark hairline for visibility on bright backgrounds
	p.setPen(QPen(QColor(0, 0, 0, 180), kBorderThickness + 2));
	p.setBrush(Qt::NoBrush);
	p.drawRect(frameRect);

	// Crisp colored frame
	p.setPen(QPen(accent, kBorderThickness));
	p.drawRect(frameRect);

	// Top status pill outside the recorded rectangle
	QFont f = font();
	f.setBold(true);
	f.setPointSize(9);
	p.setFont(f);
	const QFontMetrics fm(f);
	const int pillW = std::min(frameRect.width(), fm.horizontalAdvance(label) + 20);
	const QRect pillRect(frameRect.left(), 2, std::max(90, pillW), kTopBannerHeight - 2);
	p.setPen(QPen(accent, 1));
	p.setBrush(QColor(20, 22, 27, 232));
	p.drawRoundedRect(pillRect, 6, 6);
	p.setPen(QColor("#f8fafc"));
	p.drawText(pillRect, Qt::AlignCenter, label);
}
