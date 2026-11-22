/******************************************************************************
    Bandicam-inspired Modern Fluent Recorder Workspace for OBS Studio.
    Provides a recorder-first experience (capture modes, interactive screen
    region selector + recording frame, live audio VU meters, instant
    screenshots, webcam PiP toggle, built-in media library, quick video/general
    settings, and auto-stop) layered cleanly over OBS's native engine.
******************************************************************************/

#pragma once

#include <obs.hpp>

#include <QElapsedTimer>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

#include <atomic>
#include <memory>

class BandicamRegionFrame;
class BandicamRegionSelector;
class OBSBasic;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScreen;
class QSpinBox;
class QStackedWidget;
class QTimer;

class BandicamAudioMeter : public QWidget {
	Q_OBJECT

public:
	explicit BandicamAudioMeter(QWidget *parent = nullptr);
	~BandicamAudioMeter() override;

	void AttachSource(obs_source_t *source);
	void DetachSource();
	void SetMuted(bool muted);
	void SetConfigured(bool configured);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	static void VolMeterCallback(void *data, const float magnitude[MAX_AUDIO_CHANNELS],
				     const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);

	obs_volmeter_t *volmeter = nullptr;
	std::atomic<float> peakNormalized{0.0f};
	float displayLevel = 0.0f;
	bool mutedState = false;
	bool configuredState = false;
	QTimer *decayTimer = nullptr;
};

class BandicamRecorderWidget : public QWidget {
	Q_OBJECT

public:
	enum class ViewMode { Bandicam, Mini, Studio };
	enum class CaptureMode { Area = 0, Screen = 1, Game = 2, Window = 3 };

	explicit BandicamRecorderWidget(OBSBasic *mainWindow, QWidget *parent = nullptr);
	~BandicamRecorderWidget() override;

	void InitializeAfterOBSReady();
	void PrepareShutdown();

	ViewMode CurrentViewMode() const { return currentViewMode; }
	bool IsStudioMode() const { return currentViewMode == ViewMode::Studio; }
	bool IsPreviewVisibleInRecorder() const { return previewVisibleInRecorder; }

	void SetViewMode(ViewMode mode, bool savePreference = true);
	void ApplyWindowTitle();
	void RefreshAfterSettingsChanged();
	void RefreshActiveSourceInfo();

	// Preserved Studio state so closing in Bandicam/Mini mode never corrupts OBS docks/geometry
	bool HasSavedStudioState() const { return studioStateSaved; }
	QByteArray SavedStudioDockState() const { return studioDockState; }
	QByteArray SavedStudioGeometry() const { return studioGeometry; }

public slots:
	void OnRecordingStarted();
	void OnRecordingStopped();
	void OnRecordingPaused();
	void OnRecordingUnpaused();
	void OnScreenshotSaved(const QString &path);

private:
	void BuildUi();
	QWidget *BuildTopBar();
	QWidget *BuildStudioStrip();
	QWidget *BuildMainBody();
	QWidget *BuildHomePage();
	QWidget *BuildVideoPage();
	QWidget *BuildRecordingsPage();
	QWidget *BuildGeneralPage();
	QWidget *BuildFooterBar();

	void SyncAudioControls();
	void ToggleAudioChannel(uint32_t channel, const QString &friendlyName);
	void ToggleWebcamOverlay();
	void ToggleCursorCapture();
	void UpdateWebcamButtonState();
	void UpdateCursorButtonState();

	void SelectCaptureMode(CaptureMode mode, bool configureIfNeeded);
	void StartRegionSelection();
	void ApplySelectedRegion(const QRect &globalLogicalRect, QScreen *screen);
	void ConfigureOrAddCaptureSource(CaptureMode mode, bool replaceExisting);
	void ConfigureCurrentPrimarySource();
	void RemovePrimaryCaptureSource();
	bool EnsureCaptureSourceBeforeRecording();
	OBSSceneItem FindPrimaryCaptureItem(OBSScene *outScene = nullptr) const;
	OBSSceneItem FindWebcamItem(OBSScene *outScene = nullptr) const;

	void UpdateRecordingUi();
	void UpdateFooterStatus();
	void CheckAutoStop(qint64 elapsedMs, uint64_t totalBytes);

	void LoadVideoPageFromConfig();
	void ApplyVideoSettingsToConfig();
	void LoadGeneralPageFromConfig();
	void RefreshRecordingsList();
	void OpenSelectedRecording();
	void RevealSelectedRecording();
	void RenameSelectedRecording();
	void DeleteSelectedRecording();
	void CopySelectedRecordingPath();

	void ShowToast(const QString &message, bool isError = false);
	void TogglePreviewVisibility(bool visible);
	void SavePreferences();
	QString ResolveOutputDirectory() const;

	OBSBasic *main = nullptr;
	bool initialized = false;
	bool shuttingDown = false;
	bool syncingUi = false;

	ViewMode currentViewMode = ViewMode::Bandicam;
	CaptureMode currentCaptureMode = CaptureMode::Screen;
	bool previewVisibleInRecorder = false;
	bool showRegionFrame = true;
	bool minimizeOnRecord = false;
	bool minimizedByRecording = false;
	bool autoStopEnabled = false;
	int autoStopType = 0; // 0 = minutes, 1 = megabytes
	int autoStopValue = 30;
	bool autoStopTriggered = false;

	// Saved Full OBS Studio state
	bool studioStateSaved = false;
	QByteArray studioDockState;
	QByteArray studioGeometry;
	QSize bandicamSavedSize;
	bool hasBandicamSavedSize = false;

	// Recording timer & clock
	QPointer<QTimer> statusTimer;
	QPointer<QTimer> audioSyncTimer;
	QPointer<QTimer> toastTimer;
	QElapsedTimer recordingClock;
	qint64 recordingElapsedMs = 0;

	// Region overlay & frame
	QPointer<BandicamRegionSelector> regionSelector;
	QPointer<BandicamRegionFrame> regionFrame;
	QRect lastRegionLogicalRect;

	// Top-level containers
	QWidget *topBarWidget = nullptr;
	QWidget *studioStripWidget = nullptr;
	QWidget *bodyWidget = nullptr;
	QWidget *footerWidget = nullptr;

	// Top bar controls
	QPushButton *modeAreaBtn = nullptr;
	QPushButton *modeScreenBtn = nullptr;
	QPushButton *modeGameBtn = nullptr;
	QPushButton *modeWindowBtn = nullptr;
	QPushButton *webcamBtn = nullptr;
	QPushButton *sysAudioBtn = nullptr;
	QPushButton *micAudioBtn = nullptr;
	BandicamAudioMeter *sysAudioMeter = nullptr;
	BandicamAudioMeter *micAudioMeter = nullptr;
	QPushButton *cursorBtn = nullptr;
	QPushButton *screenshotBtn = nullptr;
	QPushButton *pauseBtn = nullptr;
	QPushButton *recordBtn = nullptr;
	QLabel *timerPillLabel = nullptr;
	QPushButton *miniToggleBtn = nullptr;
	QPushButton *fullObsBtn = nullptr;

	// Full Studio strip controls
	QPushButton *studioReturnBtn = nullptr;
	QLabel *studioStatusLabel = nullptr;
	QPushButton *studioRecordBtn = nullptr;
	QPushButton *studioShotBtn = nullptr;

	// Left rail & stack
	QStackedWidget *pageStack = nullptr;
	QPushButton *navHomeBtn = nullptr;
	QPushButton *navVideoBtn = nullptr;
	QPushButton *navRecordingsBtn = nullptr;
	QPushButton *navGeneralBtn = nullptr;
	QPushButton *navPreviewBtn = nullptr;
	QPushButton *navFolderBtn = nullptr;

	// Home page controls
	QPushButton *cardAreaBtn = nullptr;
	QPushButton *cardScreenBtn = nullptr;
	QPushButton *cardGameBtn = nullptr;
	QPushButton *cardWindowBtn = nullptr;
	QLabel *activeSourceTitleLabel = nullptr;
	QLabel *activeSourceDetailLabel = nullptr;
	QPushButton *configureSourceBtn = nullptr;
	QPushButton *reselectRegionBtn = nullptr;
	QPushButton *clearSourceBtn = nullptr;
	QLabel *homeSummaryLabel = nullptr;

	// Video page controls
	QLabel *videoLockedBanner = nullptr;
	QWidget *videoControlsContainer = nullptr;
	QComboBox *formatCombo = nullptr;
	QComboBox *qualityCombo = nullptr;
	QComboBox *encoderCombo = nullptr;
	QComboBox *resolutionCombo = nullptr;
	QComboBox *fpsCombo = nullptr;
	QCheckBox *videoCursorCheck = nullptr;
	QCheckBox *autoRemuxCheck = nullptr;

	// Recordings page controls
	int recordingsFilterMode = 0; // 0 = Videos, 1 = Screenshots, 2 = All
	QPushButton *filterVideosBtn = nullptr;
	QPushButton *filterShotsBtn = nullptr;
	QPushButton *filterAllBtn = nullptr;
	QLabel *recordingsCountLabel = nullptr;
	QListWidget *recordingsList = nullptr;
	QLabel *recordingsEmptyLabel = nullptr;
	QPushButton *recOpenBtn = nullptr;
	QPushButton *recRevealBtn = nullptr;
	QPushButton *recRenameBtn = nullptr;
	QPushButton *recCopyPathBtn = nullptr;
	QPushButton *recDeleteBtn = nullptr;

	// General page controls
	QLineEdit *outputFolderEdit = nullptr;
	QCheckBox *hideFromCaptureCheck = nullptr;
	QCheckBox *alwaysOnTopCheck = nullptr;
	QCheckBox *minimizeOnRecordCheck = nullptr;
	QCheckBox *showRegionFrameCheck = nullptr;
	QCheckBox *startInBandicamCheck = nullptr;
	QCheckBox *autoStopCheck = nullptr;
	QComboBox *autoStopTypeCombo = nullptr;
	QSpinBox *autoStopValueSpin = nullptr;

	// Footer controls
	QLabel *footerSourceLabel = nullptr;
	QLabel *footerSpecLabel = nullptr;
	QLabel *footerDiskLabel = nullptr;
	QLabel *footerToastLabel = nullptr;

	// Audio source tracking for volmeters
	OBSWeakSource attachedSysSource;
	OBSWeakSource attachedMicSource;
};
