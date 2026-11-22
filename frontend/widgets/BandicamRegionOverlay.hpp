/******************************************************************************
    Bandicam-style interactive screen region selector and floating recording
    frame overlay for OBS Recorder.
******************************************************************************/

#pragma once

#include <QElapsedTimer>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

class QPushButton;
class QScreen;

class BandicamRegionSelector : public QWidget {
	Q_OBJECT

public:
	explicit BandicamRegionSelector(QWidget *parent = nullptr);
	~BandicamRegionSelector() override = default;

	void BeginSelection();

signals:
	void regionSelected(const QRect &globalLogicalRect, QScreen *screen);
	void canceled();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	void ApplyPresetSize(int width, int height);
	void SelectFullCurrentScreen();
	void FinalizeSelection(const QRect &localRect);
	QRect CurrentSelectionRect() const;
	QScreen *ScreenForLocalRect(const QRect &localRect) const;

	QRect virtualGeometry;
	QPoint dragStart;
	QPoint dragCurrent;
	bool dragging = false;
	bool hasSelection = false;
	QRect lockedSelection;
	QWidget *presetBar = nullptr;
};

class BandicamRegionFrame : public QWidget {
	Q_OBJECT

public:
	enum class State { Ready, Recording, Paused };

	explicit BandicamRegionFrame(QWidget *parent = nullptr);
	~BandicamRegionFrame() override = default;

	void SetTargetRegion(const QRect &globalLogicalRect);
	QRect TargetRegion() const { return targetRect; }

	void SetState(State state, const QString &statusText = QString());
	void SetFrameEnabled(bool enabled);
	bool IsFrameEnabled() const { return frameEnabled; }

protected:
	void paintEvent(QPaintEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	void UpdateGeometryForTarget();
	void ExcludeFromCapture();

	static constexpr int kBorderThickness = 3;
	static constexpr int kTopBannerHeight = 26;
	static constexpr int kOuterMargin = 4;

	QRect targetRect;
	State currentState = State::Ready;
	QString bannerText;
	bool frameEnabled = true;
};
