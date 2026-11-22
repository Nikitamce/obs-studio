/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"

#include "ui-config.h"

#include "ColorSelect.hpp"
#include "OBSBasicControls.hpp"
#include "OBSBasicStats.hpp"
#include "plugin-manager/PluginManager.hpp"

#include <obs-module.h>

#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QImage>
#include <QIcon>
#include <QListView>
#include <QPixmap>
#include <QThreadPool>
#include <QRunnable>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <QMessageBox>
#include <QTimer>

#ifdef YOUTUBE_ENABLED
#include <docks/YouTubeAppDock.hpp>
#endif
#include <dialogs/NameDialog.hpp>
#include <dialogs/OBSAbout.hpp>
#include <dialogs/OBSBasicAdvAudio.hpp>
#include <dialogs/OBSBasicFilters.hpp>
#include <dialogs/OBSBasicInteraction.hpp>
#include <dialogs/OBSBasicProperties.hpp>
#include <dialogs/OBSBasicTransform.hpp>
#include <properties-view.hpp>
#include <models/SceneCollection.hpp>
#include <settings/OBSBasicSettings.hpp>
#include <utility/QuickTransition.hpp>
#include <utility/SceneRenameDelegate.hpp>
#include <utility/ScreenshotObj.hpp>
#if defined(_WIN32) || defined(WHATSNEW_ENABLED)
#include <utility/WhatsNewInfoThread.hpp>
#endif
#include <widgets/AudioMixer.hpp>
#include <widgets/OBSProjector.hpp>

#include <OBSStudioAPI.hpp>
#ifdef BROWSER_AVAILABLE
#include <browser-panel.hpp>
#endif
#ifdef ENABLE_WAYLAND
#include <obs-nix-platform.h>
#endif
#include <qt-wrappers.hpp>

#include <QActionGroup>
#include <QThread>
#include <QWidgetAction>

#include <mutex>
#include <sstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#include <objbase.h>
#include <ShObjIdl.h>
#endif

#include "moc_OBSBasic.cpp"

using namespace std;

extern bool portable_mode;
extern bool disable_3p_plugins;
extern bool opt_studio_mode;
extern bool opt_always_on_top;
extern bool opt_minimize_tray;
extern std::string opt_starting_profile;
extern std::string opt_starting_collection;

extern bool safe_mode;
extern bool opt_start_recording;
extern bool opt_start_replaybuffer;
extern bool opt_start_virtualcam;

extern volatile long insideEventLoop;
extern bool restart;

extern bool EncoderAvailable(const char *encoder);

extern void RegisterTwitchAuth();
extern void RegisterRestreamAuth();
#ifdef YOUTUBE_ENABLED
extern void RegisterYoutubeAuth();
#endif

struct QCef;

extern QCef *cef;
extern bool cef_js_avail;

extern void DestroyPanelCookieManager();
extern void CheckExistingCookieId();

namespace {
std::once_flag saveOnceFlag;

#ifdef _WIN32
QImage LoadShellVideoThumbnail(const QString &filePath)
{
	QImage image;
	const HRESULT initResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE)
		return image;
	const bool uninitializeCom = SUCCEEDED(initResult);

	IShellItemImageFactory *imageFactory = nullptr;
	const std::wstring path = filePath.toStdWString();
	HRESULT result = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&imageFactory));
	if (SUCCEEDED(result) && imageFactory) {
		HBITMAP thumbnail = nullptr;
		const SIZE targetSize = {320, 180};
		result = imageFactory->GetImage(targetSize, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &thumbnail);
		if (SUCCEEDED(result) && thumbnail) {
			BITMAP bitmap = {};
			if (GetObjectW(thumbnail, sizeof(bitmap), &bitmap) == sizeof(bitmap) && bitmap.bmWidth > 0 &&
			    bitmap.bmHeight > 0) {
				BITMAPINFO info = {};
				info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				info.bmiHeader.biWidth = bitmap.bmWidth;
				info.bmiHeader.biHeight = -bitmap.bmHeight;
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				info.bmiHeader.biCompression = BI_RGB;

				image = QImage(bitmap.bmWidth, bitmap.bmHeight, QImage::Format_RGB32);
				HDC deviceContext = GetDC(nullptr);
				if (!deviceContext ||
				    GetDIBits(deviceContext, thumbnail, 0, static_cast<UINT>(bitmap.bmHeight), image.bits(), &info,
					      DIB_RGB_COLORS) != bitmap.bmHeight) {
					image = QImage();
				}
				if (deviceContext)
					ReleaseDC(nullptr, deviceContext);
			}
			DeleteObject(thumbnail);
		}
		imageFactory->Release();
	}

	if (uninitializeCom)
		CoUninitialize();
	return image;
}
#endif
}

extern void setupDockAction(QDockWidget *dock);

OBSBasic::OBSBasic(QWidget *parent) : OBSMainWindow(parent), undo_s(ui), ui(new Ui::OBSBasic)
{
	collections = {};

	setAttribute(Qt::WA_NativeWindow);

#ifdef TWITCH_ENABLED
	RegisterTwitchAuth();
#endif
#ifdef RESTREAM_ENABLED
	RegisterRestreamAuth();
#endif
#ifdef YOUTUBE_ENABLED
	RegisterYoutubeAuth();
#endif

	setAcceptDrops(true);

	setContextMenuPolicy(Qt::CustomContextMenu);

	QEvent::registerEventType(QEvent::User + QEvent::Close);

	api = InitializeAPIInterface(this);

	ui->setupUi(this);
	ui->previewDisabledWidget->setVisible(false);
	SetupSimpleRecorderUi();

	/* Set up streaming connections */
	connect(
		this, &OBSBasic::StreamingStarting, this, [this] { this->streamingStarting = true; },
		Qt::DirectConnection);
	connect(
		this, &OBSBasic::StreamingStarted, this, [this] { this->streamingStarting = false; },
		Qt::DirectConnection);
	connect(
		this, &OBSBasic::StreamingStopped, this, [this] { this->streamingStarting = false; },
		Qt::DirectConnection);

	/* Set up recording connections */
	connect(
		this, &OBSBasic::RecordingStarted, this,
		[this]() {
			this->recordingStarted = true;
			this->recordingPaused = false;
		},
		Qt::DirectConnection);
	connect(
		this, &OBSBasic::RecordingPaused, this, [this]() { this->recordingPaused = true; },
		Qt::DirectConnection);
	connect(
		this, &OBSBasic::RecordingUnpaused, this, [this]() { this->recordingPaused = false; },
		Qt::DirectConnection);
	connect(
		this, &OBSBasic::RecordingStopped, this,
		[this]() {
			this->recordingStarted = false;
			this->recordingPaused = false;
		},
		Qt::DirectConnection);

	/* Add controls dock */
	OBSBasicControls *controls = new OBSBasicControls(this);
	controlsDock = new OBSDock(this);
	controlsDock->setObjectName(QString::fromUtf8("controlsDock"));
	controlsDock->setWindowTitle(QTStr("Basic.Main.Controls"));
	/* Parenting is done there so controls will be deleted alongside controlsDock */
	controlsDock->setWidget(controls);

	connect(controls, &OBSBasicControls::StreamButtonClicked, this, &OBSBasic::StreamActionTriggered);

	connect(controls, &OBSBasicControls::StartStreamMenuActionClicked, this, &OBSBasic::StartStreaming);
	connect(controls, &OBSBasicControls::StopStreamMenuActionClicked, this, &OBSBasic::StopStreaming);
	connect(controls, &OBSBasicControls::ForceStopStreamMenuActionClicked, this, &OBSBasic::ForceStopStreaming);

	connect(controls, &OBSBasicControls::BroadcastButtonClicked, this, &OBSBasic::BroadcastButtonClicked);

	connect(controls, &OBSBasicControls::RecordButtonClicked, this, &OBSBasic::RecordActionTriggered);
	connect(controls, &OBSBasicControls::PauseRecordButtonClicked, this, &OBSBasic::RecordPauseToggled);

	connect(controls, &OBSBasicControls::ReplayBufferButtonClicked, this, &OBSBasic::ReplayBufferActionTriggered);
	connect(controls, &OBSBasicControls::SaveReplayBufferButtonClicked, this, &OBSBasic::ReplayBufferSave);

	connect(controls, &OBSBasicControls::VirtualCamButtonClicked, this, &OBSBasic::VirtualCamActionTriggered);
	connect(controls, &OBSBasicControls::VirtualCamConfigButtonClicked, this, &OBSBasic::OpenVirtualCamConfig);

	connect(controls, &OBSBasicControls::StudioModeButtonClicked, this, &OBSBasic::TogglePreviewProgramMode);

	connect(controls, &OBSBasicControls::SettingsButtonClicked, this, &OBSBasic::on_action_Settings_triggered);

	/* Set up transitions combobox connections */
	connect(this, &OBSBasic::TransitionAdded, this, [this](const QString &name, const QString &uuid) {
		QSignalBlocker sb(ui->transitions);
		ui->transitions->addItem(name, uuid);
	});
	connect(this, &OBSBasic::TransitionRenamed, this, [this](const QString &uuid, const QString &newName) {
		QSignalBlocker sb(ui->transitions);
		ui->transitions->setItemText(ui->transitions->findData(uuid), newName);
	});
	connect(this, &OBSBasic::TransitionRemoved, this, [this](const QString &uuid) {
		QSignalBlocker sb(ui->transitions);
		ui->transitions->removeItem(ui->transitions->findData(uuid));
	});
	connect(this, &OBSBasic::TransitionsCleared, this, [this]() {
		QSignalBlocker sb(ui->transitions);
		ui->transitions->clear();
	});

	connect(this, &OBSBasic::CurrentTransitionChanged, this, [this](const QString &uuid) {
		QSignalBlocker sb(ui->transitions);
		ui->transitions->setCurrentIndex(ui->transitions->findData(uuid));
	});

	connect(ui->transitions, &QComboBox::currentIndexChanged, this,
		[this]() { SetCurrentTransition(ui->transitions->currentData().toString()); });

	connect(this, &OBSBasic::TransitionDurationChanged, this, [this](int duration) {
		QSignalBlocker sb(ui->transitionDuration);
		ui->transitionDuration->setValue(duration);
	});

	connect(ui->transitionDuration, &QSpinBox::valueChanged, this,
		[this](int value) { SetTransitionDuration(value); });

	/* Main window default layout */
	setDockCornersVertical(true);

	/* Scenes and Sources dock on left
	 * This specific arrangement can't be set up in Qt Designer */
	addDockWidget(Qt::LeftDockWidgetArea, ui->scenesDock);
	splitDockWidget(ui->scenesDock, ui->sourcesDock, Qt::Vertical);
	int sideDockWidth = std::min(width() * 30 / 100, 320);
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {sideDockWidth, sideDockWidth}, Qt::Horizontal);
	addDockWidget(Qt::BottomDockWidgetArea, controlsDock);

	startingDockLayout = saveState();

	statsDock = new OBSDock();
	statsDock->setObjectName(QStringLiteral("statsDock"));
	statsDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			       QDockWidget::DockWidgetFloatable);
	statsDock->setWindowTitle(QTStr("Basic.Stats"));
	addDockWidget(Qt::BottomDockWidgetArea, statsDock);
	statsDock->setVisible(false);
	statsDock->setFloating(true);
	statsDock->resize(700, 200);

	copyActionsDynamicProperties();

	qRegisterMetaType<int64_t>("int64_t");
	qRegisterMetaType<uint32_t>("uint32_t");
	qRegisterMetaType<OBSScene>("OBSScene");
	qRegisterMetaType<OBSSceneItem>("OBSSceneItem");
	qRegisterMetaType<OBSSource>("OBSSource");
	qRegisterMetaType<obs_hotkey_id>("obs_hotkey_id");
	qRegisterMetaType<SavedProjectorInfo *>("SavedProjectorInfo *");

	ui->scenes->setAttribute(Qt::WA_MacShowFocusRect, false);
	ui->sources->setAttribute(Qt::WA_MacShowFocusRect, false);

	bool sceneGrid = config_get_bool(App()->GetUserConfig(), "BasicWindow", "gridMode");
	ui->scenes->SetGridMode(sceneGrid);

	if (sceneGrid) {
		ui->actionSceneGridMode->setChecked(true);
	} else {
		ui->actionSceneListMode->setChecked(true);
	}

	ui->scenes->setItemDelegate(new SceneRenameDelegate(ui->scenes));

	auto displayResize = [this]() {
		struct obs_video_info ovi;

		if (obs_get_video_info(&ovi)) {
			ResizePreview(ovi.base_width, ovi.base_height);
		}

		UpdateContextBarVisibility();
		UpdatePreviewControls();
		dpi = devicePixelRatioF();
	};
	dpi = devicePixelRatioF();

	connect(windowHandle(), &QWindow::screenChanged, this, displayResize);
	connect(ui->preview, &OBSQTDisplay::DisplayResized, this, displayResize);

	/* TODO: Move these into window-basic-preview */
	/* Preview Scaling label */
	connect(ui->preview, &OBSBasicPreview::scalingChanged, ui->previewScalePercent,
		&OBSPreviewScalingLabel::PreviewScaleChanged);

	/* Preview Scaling dropdown */
	connect(ui->preview, &OBSBasicPreview::scalingChanged, ui->previewScalingMode,
		&OBSPreviewScalingComboBox::PreviewScaleChanged);

	connect(ui->preview, &OBSBasicPreview::fixedScalingChanged, ui->previewScalingMode,
		&OBSPreviewScalingComboBox::PreviewFixedScalingChanged);

	connect(ui->previewScalingMode, &OBSPreviewScalingComboBox::currentIndexChanged, this,
		&OBSBasic::PreviewScalingModeChanged);

	/* Preview Controls */
	connect(ui->previewXScrollBar, &QScrollBar::sliderMoved, ui->preview, &OBSBasicPreview::xScrollBarChanged);
	connect(ui->previewYScrollBar, &QScrollBar::valueChanged, ui->preview, &OBSBasicPreview::yScrollBarChanged);

	connect(ui->previewZoomInButton, &QPushButton::clicked, ui->preview, &OBSBasicPreview::increaseScalingLevel);
	connect(ui->previewZoomOutButton, &QPushButton::clicked, ui->preview, &OBSBasicPreview::decreaseScalingLevel);

	/* Preview Actions */
	connect(ui->actionScaleWindow, &QAction::triggered, this, &OBSBasic::setPreviewScalingWindow);
	connect(ui->actionScaleCanvas, &QAction::triggered, this, &OBSBasic::setPreviewScalingCanvas);
	connect(ui->actionScaleOutput, &QAction::triggered, this, &OBSBasic::setPreviewScalingOutput);

	connect(ui->actionPreviewZoomIn, &QAction::triggered, ui->preview, &OBSBasicPreview::increaseScalingLevel);
	connect(ui->actionPreviewZoomOut, &QAction::triggered, ui->preview, &OBSBasicPreview::decreaseScalingLevel);
	connect(ui->actionPreviewResetZoom, &QAction::triggered, ui->preview, &OBSBasicPreview::resetScalingLevel);

	connect(this, &OBSBasic::CanvasResized, ui->previewScalingMode, &OBSPreviewScalingComboBox::CanvasResized);
	connect(this, &OBSBasic::OutputResized, ui->previewScalingMode, &OBSPreviewScalingComboBox::OutputResized);

	delete shortcutFilter;
	shortcutFilter = CreateShortcutFilter();
	installEventFilter(shortcutFilter);

	stringstream name;
	name << "OBS " << App()->GetVersionString();
	blog(LOG_INFO, "%s", name.str().c_str());
	blog(LOG_INFO, "---------------------------------");

	UpdateTitleBar();

	connect(ui->scenes->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, &OBSBasic::SceneNameEdited);

	cpuUsageInfo = os_cpu_usage_info_start();
	cpuUsageTimer = new QTimer(this);
	connect(cpuUsageTimer.data(), &QTimer::timeout, ui->statusbar, &OBSBasicStatusBar::UpdateCPUUsage);
	cpuUsageTimer->start(3000);

	diskFullTimer = new QTimer(this);
	connect(diskFullTimer, &QTimer::timeout, this, &OBSBasic::CheckDiskSpaceRemaining);

	renameScene = new QAction(QTStr("Rename"), ui->scenesDock);
	renameScene->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(renameScene, &QAction::triggered, this, &OBSBasic::EditSceneName);
	ui->scenesDock->addAction(renameScene);

	renameSource = new QAction(QTStr("Rename"), ui->sourcesDock);
	renameSource->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(renameSource, &QAction::triggered, this, &OBSBasic::EditSceneItemName);
	ui->sourcesDock->addAction(renameSource);

#ifdef __APPLE__
	renameScene->setShortcut({Qt::Key_Return});
	renameSource->setShortcut({Qt::Key_Return});

	ui->actionRemoveSource->setShortcuts({Qt::Key_Backspace, Qt::Key_Delete});
	ui->actionRemoveScene->setShortcuts({Qt::Key_Backspace, Qt::Key_Delete});

	ui->actionCheckForUpdates->setMenuRole(QAction::AboutQtRole);
	ui->action_Settings->setMenuRole(QAction::PreferencesRole);
	ui->actionShowMacPermissions->setMenuRole(QAction::ApplicationSpecificRole);
	delete ui->actionE_xit;
#else
	renameScene->setShortcut({Qt::Key_F2});
	renameSource->setShortcut({Qt::Key_F2});
#endif

#ifdef __linux__
	ui->actionE_xit->setShortcut(Qt::CTRL | Qt::Key_Q);
#endif

#ifndef ENABLE_IDIAN_PLAYGROUND
	ui->idianPlayground->setVisible(false);
#endif

	auto addNudge = [this](const QKeySequence &seq, MoveDir direction, int distance) {
		QAction *nudge = new QAction(ui->preview);
		nudge->setShortcut(seq);
		nudge->setShortcutContext(Qt::WidgetShortcut);
		ui->preview->addAction(nudge);
		connect(nudge, &QAction::triggered, this,
			[this, distance, direction]() { Nudge(distance, direction); });
	};

	addNudge(Qt::Key_Up, MoveDir::Up, 1);
	addNudge(Qt::Key_Down, MoveDir::Down, 1);
	addNudge(Qt::Key_Left, MoveDir::Left, 1);
	addNudge(Qt::Key_Right, MoveDir::Right, 1);
	addNudge(Qt::SHIFT | Qt::Key_Up, MoveDir::Up, 10);
	addNudge(Qt::SHIFT | Qt::Key_Down, MoveDir::Down, 10);
	addNudge(Qt::SHIFT | Qt::Key_Left, MoveDir::Left, 10);
	addNudge(Qt::SHIFT | Qt::Key_Right, MoveDir::Right, 10);

	/* Setup dock toggle action
	 * And hide all docks before restoring parent geometry */
#define SETUP_DOCK(dock)                                    \
	setupDockAction(dock);                              \
	ui->menuDocks->addAction(dock->toggleViewAction()); \
	dock->setVisible(false);

	SETUP_DOCK(ui->scenesDock);
	SETUP_DOCK(ui->sourcesDock);
	SETUP_DOCK(ui->mixerDock);
	SETUP_DOCK(ui->transitionsDock);
	SETUP_DOCK(controlsDock);
	SETUP_DOCK(statsDock);
#undef SETUP_DOCK

	// Register shortcuts for Undo/Redo
	ui->actionMainUndo->setShortcut(Qt::CTRL | Qt::Key_Z);
	QList<QKeySequence> shrt;
	shrt << QKeySequence((Qt::CTRL | Qt::SHIFT) | Qt::Key_Z) << QKeySequence(Qt::CTRL | Qt::Key_Y);
	ui->actionMainRedo->setShortcuts(shrt);

	ui->actionMainUndo->setShortcutContext(Qt::ApplicationShortcut);
	ui->actionMainRedo->setShortcutContext(Qt::ApplicationShortcut);

	QPoint curPos;

	//restore parent window geometry
	const char *geometry = config_get_string(App()->GetUserConfig(), "BasicWindow", "geometry");
	if (geometry != NULL) {
		QByteArray byteArray = QByteArray::fromBase64(QByteArray(geometry));
		restoreGeometry(byteArray);

		QRect windowGeometry = normalGeometry();
		if (!WindowPositionValid(windowGeometry)) {
			QRect rect = QGuiApplication::primaryScreen()->geometry();
			setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), rect));
		}

		curPos = pos();
	} else {
		QRect desktopRect = QGuiApplication::primaryScreen()->geometry();
		QSize adjSize = desktopRect.size() / 2 - size() / 2;
		curPos = QPoint(adjSize.width(), adjSize.height());
	}

	QPoint curSize(width(), height());

	QPoint statsDockSize(statsDock->width(), statsDock->height());
	QPoint statsDockPos = curSize / 2 - statsDockSize / 2;
	QPoint newPos = curPos + statsDockPos;
	statsDock->move(newPos);

	ui->actionReleaseNotes->setVisible(true);

	ui->previewDisabledWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui->enablePreviewButton, &QPushButton::clicked, this, &OBSBasic::TogglePreview);

	connect(ui->scenes, &SceneTree::scenesReordered, ui->scenes,
		[]() { OBSProjector::UpdateMultiviewProjectors(); });

	connect(App(), &OBSApp::StyleChanged, this, [this]() { OnEvent(OBS_FRONTEND_EVENT_THEME_CHANGED); });
#ifndef __APPLE__
	connect(App(), &OBSApp::aboutToQuit, this, &OBSBasic::closeWindow);
#endif

	QActionGroup *actionGroup = new QActionGroup(this);
	actionGroup->addAction(ui->actionSceneListMode);
	actionGroup->addAction(ui->actionSceneGridMode);

	UpdatePreviewSafeAreas();
	UpdatePreviewSpacingHelpers();
	UpdatePreviewOverflowSettings();
}

static const double scaled_vals[] = {1.0, 1.25, (1.0 / 0.75), 1.5, (1.0 / 0.6), 1.75, 2.0, 2.25, 2.5, 2.75, 3.0, 0.0};

#ifdef __APPLE__ // macOS
#define DEFAULT_CONTAINER "hybrid_mov"
#else // Windows/Linux
#define DEFAULT_CONTAINER "hybrid_mp4"
#endif

bool OBSBasic::InitBasicConfigDefaults()
{
	QList<QScreen *> screens = QGuiApplication::screens();

	if (!screens.size()) {
		OBSErrorBox(NULL, "There appears to be no monitors.  Er, this "
				  "technically shouldn't be possible.");
		return false;
	}

	QScreen *primaryScreen = QGuiApplication::primaryScreen();

	uint32_t cx = primaryScreen->size().width();
	uint32_t cy = primaryScreen->size().height();

	cx *= devicePixelRatioF();
	cy *= devicePixelRatioF();

	bool oldResolutionDefaults = config_get_bool(App()->GetUserConfig(), "General", "Pre19Defaults");

	/* use 1920x1080 for new default base res if main monitor is above
	 * 1920x1080, but don't apply for people from older builds -- only to
	 * new users */
	if (!oldResolutionDefaults && (cx * cy) > (1920 * 1080)) {
		cx = 1920;
		cy = 1080;
	}

	bool changed = false;

	/* ----------------------------------------------------- */
	/* move over old FFmpeg track settings                   */
	if (config_has_user_value(activeConfiguration, "AdvOut", "FFAudioTrack") &&
	    !config_has_user_value(activeConfiguration, "AdvOut", "Pre22.1Settings")) {

		int track = (int)config_get_int(activeConfiguration, "AdvOut", "FFAudioTrack");
		config_set_int(activeConfiguration, "AdvOut", "FFAudioMixes", 1LL << (track - 1));
		config_set_bool(activeConfiguration, "AdvOut", "Pre22.1Settings", true);
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* move over mixer values in advanced if older config */
	if (config_has_user_value(activeConfiguration, "AdvOut", "RecTrackIndex") &&
	    !config_has_user_value(activeConfiguration, "AdvOut", "RecTracks")) {

		uint64_t track = config_get_uint(activeConfiguration, "AdvOut", "RecTrackIndex");
		track = 1ULL << (track - 1);
		config_set_uint(activeConfiguration, "AdvOut", "RecTracks", track);
		config_remove_value(activeConfiguration, "AdvOut", "RecTrackIndex");
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* set twitch chat extensions to "both" if prev version  */
	/* is under 24.1                                         */
	if (config_get_bool(App()->GetUserConfig(), "General", "Pre24.1Defaults") &&
	    !config_has_user_value(activeConfiguration, "Twitch", "AddonChoice")) {
		config_set_int(activeConfiguration, "Twitch", "AddonChoice", 3);
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* move bitrate enforcement setting to new value         */
	if (config_has_user_value(activeConfiguration, "SimpleOutput", "EnforceBitrate") &&
	    !config_has_user_value(activeConfiguration, "Stream1", "IgnoreRecommended") &&
	    !config_has_user_value(activeConfiguration, "Stream1", "MovedOldEnforce")) {
		bool enforce = config_get_bool(activeConfiguration, "SimpleOutput", "EnforceBitrate");
		config_set_bool(activeConfiguration, "Stream1", "IgnoreRecommended", !enforce);
		config_set_bool(activeConfiguration, "Stream1", "MovedOldEnforce", true);
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* enforce minimum retry delay of 1 second prior to 27.1 */
	if (config_has_user_value(activeConfiguration, "Output", "RetryDelay")) {
		int retryDelay = config_get_uint(activeConfiguration, "Output", "RetryDelay");
		if (retryDelay < 1) {
			config_set_uint(activeConfiguration, "Output", "RetryDelay", 1);
			changed = true;
		}
	}

	/* ----------------------------------------------------- */
	/* Migrate old container selection (if any) to new key.  */

	auto MigrateFormat = [&](const char *section) {
		bool has_old_key = config_has_user_value(activeConfiguration, section, "RecFormat");
		bool has_new_key = config_has_user_value(activeConfiguration, section, "RecFormat2");
		if (!has_new_key && !has_old_key) {
			return;
		}

		string old_format =
			config_get_string(activeConfiguration, section, has_new_key ? "RecFormat2" : "RecFormat");
		string new_format = old_format;
		if (old_format == "ts") {
			new_format = "mpegts";
		} else if (old_format == "m3u8") {
			new_format = "hls";
		} else if (old_format == "fmp4") {
			new_format = "fragmented_mp4";
		} else if (old_format == "fmov") {
			new_format = "fragmented_mov";
		}

		if (new_format != old_format || !has_new_key) {
			config_set_string(activeConfiguration, section, "RecFormat2", new_format.c_str());
			changed = true;
		}
	};

	MigrateFormat("AdvOut");
	MigrateFormat("SimpleOutput");

	/* ----------------------------------------------------- */
	/* Migrate output scale setting to GPU scaling options.  */

	if (config_get_bool(activeConfiguration, "AdvOut", "Rescale") &&
	    !config_has_user_value(activeConfiguration, "AdvOut", "RescaleFilter")) {
		config_set_int(activeConfiguration, "AdvOut", "RescaleFilter", OBS_SCALE_BILINEAR);
	}

	if (config_get_bool(activeConfiguration, "AdvOut", "RecRescale") &&
	    !config_has_user_value(activeConfiguration, "AdvOut", "RecRescaleFilter")) {
		config_set_int(activeConfiguration, "AdvOut", "RecRescaleFilter", OBS_SCALE_BILINEAR);
	}

	/* ----------------------------------------------------- */

	if (changed) {
		activeConfiguration.SaveSafe("tmp");
	}

	/* ----------------------------------------------------- */

	config_set_default_string(activeConfiguration, "Output", "Mode", "Simple");

	config_set_default_bool(activeConfiguration, "Stream1", "IgnoreRecommended", false);
	config_set_default_bool(activeConfiguration, "Stream1", "EnableMultitrackVideo", false);
	config_set_default_bool(activeConfiguration, "Stream1", "MultitrackVideoMaximumAggregateBitrateAuto", true);
	config_set_default_bool(activeConfiguration, "Stream1", "MultitrackVideoMaximumVideoTracksAuto", true);

	config_set_default_string(activeConfiguration, "SimpleOutput", "FilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(activeConfiguration, "SimpleOutput", "RecFormat2", DEFAULT_CONTAINER);
	config_set_default_uint(activeConfiguration, "SimpleOutput", "VBitrate", 6000);
	config_set_default_uint(activeConfiguration, "SimpleOutput", "ABitrate", 160);
	config_set_default_bool(activeConfiguration, "SimpleOutput", "UseAdvanced", false);
	config_set_default_string(activeConfiguration, "SimpleOutput", "Preset", "veryfast");
	config_set_default_string(activeConfiguration, "SimpleOutput", "NVENCPreset2", "p5");
	config_set_default_string(activeConfiguration, "SimpleOutput", "RecQuality", "Stream");
	config_set_default_bool(activeConfiguration, "SimpleOutput", "RecRB", false);
	config_set_default_int(activeConfiguration, "SimpleOutput", "RecRBTime", 20);
	config_set_default_int(activeConfiguration, "SimpleOutput", "RecRBSize", 512);
	config_set_default_string(activeConfiguration, "SimpleOutput", "RecRBPrefix", "Replay");
	config_set_default_string(activeConfiguration, "SimpleOutput", "StreamAudioEncoder", "aac");
	config_set_default_string(activeConfiguration, "SimpleOutput", "RecAudioEncoder", "aac");
	config_set_default_uint(activeConfiguration, "SimpleOutput", "RecTracks", (1 << 0));

	config_set_default_bool(activeConfiguration, "AdvOut", "ApplyServiceSettings", true);
	config_set_default_bool(activeConfiguration, "AdvOut", "UseRescale", false);
	config_set_default_uint(activeConfiguration, "AdvOut", "TrackIndex", 1);
	config_set_default_uint(activeConfiguration, "AdvOut", "VodTrackIndex", 2);
	config_set_default_string(activeConfiguration, "AdvOut", "Encoder", "obs_x264");

	config_set_default_string(activeConfiguration, "AdvOut", "RecType", "Standard");

	config_set_default_string(activeConfiguration, "AdvOut", "RecFilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(activeConfiguration, "AdvOut", "RecFormat2", DEFAULT_CONTAINER);
	config_set_default_bool(activeConfiguration, "AdvOut", "RecUseRescale", false);
	config_set_default_uint(activeConfiguration, "AdvOut", "RecTracks", (1 << 0));
	config_set_default_string(activeConfiguration, "AdvOut", "RecEncoder", "none");
	config_set_default_uint(activeConfiguration, "AdvOut", "FLVTrack", 1);
	config_set_default_uint(activeConfiguration, "AdvOut", "StreamMultiTrackAudioMixes", 1);
	config_set_default_bool(activeConfiguration, "AdvOut", "FFOutputToFile", true);
	config_set_default_string(activeConfiguration, "AdvOut", "FFFilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(activeConfiguration, "AdvOut", "FFExtension", "mp4");
	config_set_default_uint(activeConfiguration, "AdvOut", "FFVBitrate", 6000);
	config_set_default_uint(activeConfiguration, "AdvOut", "FFVGOPSize", 250);
	config_set_default_bool(activeConfiguration, "AdvOut", "FFUseRescale", false);
	config_set_default_bool(activeConfiguration, "AdvOut", "FFIgnoreCompat", false);
	config_set_default_uint(activeConfiguration, "AdvOut", "FFABitrate", 160);
	config_set_default_uint(activeConfiguration, "AdvOut", "FFAudioMixes", 1);

	config_set_default_uint(activeConfiguration, "AdvOut", "Track1Bitrate", 160);
	config_set_default_uint(activeConfiguration, "AdvOut", "Track2Bitrate", 160);
	config_set_default_uint(activeConfiguration, "AdvOut", "Track3Bitrate", 160);
	config_set_default_uint(activeConfiguration, "AdvOut", "Track4Bitrate", 160);
	config_set_default_uint(activeConfiguration, "AdvOut", "Track5Bitrate", 160);
	config_set_default_uint(activeConfiguration, "AdvOut", "Track6Bitrate", 160);

	config_set_default_uint(activeConfiguration, "AdvOut", "RecSplitFileTime", 15);
	config_set_default_uint(activeConfiguration, "AdvOut", "RecSplitFileSize", 2048);

	config_set_default_bool(activeConfiguration, "AdvOut", "RecRB", false);
	config_set_default_uint(activeConfiguration, "AdvOut", "RecRBTime", 20);
	config_set_default_int(activeConfiguration, "AdvOut", "RecRBSize", 512);

	config_set_default_uint(activeConfiguration, "Video", "BaseCX", cx);
	config_set_default_uint(activeConfiguration, "Video", "BaseCY", cy);

	/* don't allow BaseCX/BaseCY to be susceptible to defaults changing */
	if (!config_has_user_value(activeConfiguration, "Video", "BaseCX") ||
	    !config_has_user_value(activeConfiguration, "Video", "BaseCY")) {
		config_set_uint(activeConfiguration, "Video", "BaseCX", cx);
		config_set_uint(activeConfiguration, "Video", "BaseCY", cy);
		config_save_safe(activeConfiguration, "tmp", nullptr);
	}

	config_set_default_string(activeConfiguration, "Output", "FilenameFormatting", "%CCYY-%MM-%DD %hh-%mm-%ss");

	config_set_default_bool(activeConfiguration, "Output", "DelayEnable", false);
	config_set_default_uint(activeConfiguration, "Output", "DelaySec", 20);
	config_set_default_bool(activeConfiguration, "Output", "DelayPreserve", true);

	config_set_default_bool(activeConfiguration, "Output", "Reconnect", true);
	config_set_default_uint(activeConfiguration, "Output", "RetryDelay", 2);
	config_set_default_uint(activeConfiguration, "Output", "MaxRetries", 25);

	config_set_default_string(activeConfiguration, "Output", "BindIP", "default");
	config_set_default_string(activeConfiguration, "Output", "IPFamily", "IPv4+IPv6");
	config_set_default_bool(activeConfiguration, "Output", "NewSocketLoopEnable", false);
	config_set_default_bool(activeConfiguration, "Output", "LowLatencyEnable", false);

	int i = 0;
	uint32_t scale_cx = cx;
	uint32_t scale_cy = cy;

	/* use a default scaled resolution that has a pixel count no higher
	 * than 1280x720 */
	while (((scale_cx * scale_cy) > (1280 * 720)) && scaled_vals[i] > 0.0) {
		double scale = scaled_vals[i++];
		scale_cx = uint32_t(double(cx) / scale);
		scale_cy = uint32_t(double(cy) / scale);
	}

	config_set_default_uint(activeConfiguration, "Video", "OutputCX", scale_cx);
	config_set_default_uint(activeConfiguration, "Video", "OutputCY", scale_cy);

	/* don't allow OutputCX/OutputCY to be susceptible to defaults
	 * changing */
	if (!config_has_user_value(activeConfiguration, "Video", "OutputCX") ||
	    !config_has_user_value(activeConfiguration, "Video", "OutputCY")) {
		config_set_uint(activeConfiguration, "Video", "OutputCX", scale_cx);
		config_set_uint(activeConfiguration, "Video", "OutputCY", scale_cy);
		config_save_safe(activeConfiguration, "tmp", nullptr);
	}

	config_set_default_uint(activeConfiguration, "Video", "FPSType", 0);
	config_set_default_string(activeConfiguration, "Video", "FPSCommon", "30");
	config_set_default_uint(activeConfiguration, "Video", "FPSInt", 30);
	config_set_default_uint(activeConfiguration, "Video", "FPSNum", 30);
	config_set_default_uint(activeConfiguration, "Video", "FPSDen", 1);
	config_set_default_string(activeConfiguration, "Video", "ScaleType", "bicubic");
	config_set_default_string(activeConfiguration, "Video", "ColorFormat", "NV12");
	config_set_default_string(activeConfiguration, "Video", "ColorSpace", "709");
	config_set_default_string(activeConfiguration, "Video", "ColorRange", "Partial");
	config_set_default_uint(activeConfiguration, "Video", "SdrWhiteLevel", 300);
	config_set_default_uint(activeConfiguration, "Video", "HdrNominalPeakLevel", 1000);

	config_set_default_string(activeConfiguration, "Audio", "MonitoringDeviceId", "default");
	config_set_default_string(activeConfiguration, "Audio", "MonitoringDeviceName",
				  Str("Basic.Settings.Advanced.Audio.MonitoringDevice"
				      ".Default"));
	config_set_default_uint(activeConfiguration, "Audio", "SampleRate", 48000);
	config_set_default_string(activeConfiguration, "Audio", "ChannelSetup", "Stereo");
	config_set_default_double(activeConfiguration, "Audio", "MeterDecayRate", VOLUME_METER_DECAY_FAST);
	config_set_default_uint(activeConfiguration, "Audio", "PeakMeterType", 0);

	CheckExistingCookieId();

	return true;
}

void OBSBasic::InitBasicConfigDefaults2()
{
	bool oldEncDefaults = config_get_bool(App()->GetUserConfig(), "General", "Pre23Defaults");
	bool useNV = EncoderAvailable("ffmpeg_nvenc") && !oldEncDefaults;

	config_set_default_string(activeConfiguration, "SimpleOutput", "StreamEncoder",
				  useNV ? SIMPLE_ENCODER_NVENC : SIMPLE_ENCODER_X264);
	config_set_default_string(activeConfiguration, "SimpleOutput", "RecEncoder",
				  useNV ? SIMPLE_ENCODER_NVENC : SIMPLE_ENCODER_X264);

	const char *aac_default = "ffmpeg_aac";
	if (EncoderAvailable("CoreAudio_AAC")) {
		aac_default = "CoreAudio_AAC";
	} else if (EncoderAvailable("libfdk_aac")) {
		aac_default = "libfdk_aac";
	}

	config_set_default_string(activeConfiguration, "AdvOut", "AudioEncoder", aac_default);
	config_set_default_string(activeConfiguration, "AdvOut", "RecAudioEncoder", aac_default);
}

bool OBSBasic::InitBasicConfig()
{
	ProfileScope("OBSBasic::InitBasicConfig");

	RefreshProfiles(true);

	const std::string currentProfileName{config_get_string(App()->GetUserConfig(), "Basic", "Profile")};
	const std::optional<OBSProfile> currentProfile = GetProfileByName(currentProfileName);
	const std::optional<OBSProfile> foundProfile = GetProfileByName(opt_starting_profile);

	try {
		if (foundProfile) {
			ActivateProfile(foundProfile.value());
		} else if (currentProfile) {
			ActivateProfile(currentProfile.value());
		} else {
			const OBSProfile &newProfile = CreateProfile(currentProfileName);
			ActivateProfile(newProfile);
		}
	} catch (const std::logic_error &) {
		OBSErrorBox(NULL, "Failed to open basic.ini: %d", -1);
		return false;
	}

	return true;
}

void OBSBasic::InitOBSCallbacks()
{
	ProfileScope("OBSBasic::InitOBSCallbacks");

	signalHandlers.reserve(signalHandlers.size() + 6);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_create", OBSBasic::SourceCreated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_remove", OBSBasic::SourceRemoved, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_rename", OBSBasic::SourceRenamed, this);
	signalHandlers.emplace_back(
		obs_get_signal_handler(), "source_filter_add",
		[](void *data, calldata_t *) {
			QMetaObject::invokeMethod(static_cast<OBSBasic *>(data), &OBSBasic::UpdateEditMenu,
						  Qt::QueuedConnection);
		},
		this);
	signalHandlers.emplace_back(
		obs_get_signal_handler(), "source_filter_remove",
		[](void *data, calldata_t *) {
			QMetaObject::invokeMethod(static_cast<OBSBasic *>(data), &OBSBasic::UpdateEditMenu,
						  Qt::QueuedConnection);
		},
		this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "canvas_remove", OBSBasic::CanvasRemoved, this);
}

#define STARTUP_SEPARATOR "==== Startup complete ==============================================="
#define SHUTDOWN_SEPARATOR "==== Shutting down =================================================="

#define UNSUPPORTED_ERROR                                                     \
	"Failed to initialize video:\n\nRequired graphics API functionality " \
	"not found.  Your GPU may not be supported."

#define UNKNOWN_ERROR                                                  \
	"Failed to initialize video.  Your GPU may not be supported, " \
	"or your graphics drivers may need to be updated."

static inline void LogEncoders()
{
	constexpr uint32_t hide_flags = OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL;

	auto list_encoders = [](obs_encoder_type type) {
		size_t idx = 0;
		const char *encoder_type;

		while (obs_enum_encoder_types(idx++, &encoder_type)) {
			if (obs_get_encoder_caps(encoder_type) & hide_flags ||
			    obs_get_encoder_type(encoder_type) != type) {
				continue;
			}

			blog(LOG_INFO, "\t- %s (%s)", encoder_type, obs_encoder_get_display_name(encoder_type));
		}
	};

	blog(LOG_INFO, "---------------------------------");
	blog(LOG_INFO, "Available Encoders:");
	blog(LOG_INFO, "  Video Encoders:");
	list_encoders(OBS_ENCODER_VIDEO);
	blog(LOG_INFO, "  Audio Encoders:");
	list_encoders(OBS_ENCODER_AUDIO);
}

void OBSBasic::OBSInit()
{
	ProfileScope("OBSBasic::OBSInit");

	if (!InitBasicConfig()) {
		throw "Failed to load basic.ini";
	}
	if (!ResetAudio()) {
		throw "Failed to initialize audio";
	}

	int ret = 0;

	ret = ResetVideo();

	switch (ret) {
	case OBS_VIDEO_MODULE_NOT_FOUND:
		throw "Failed to initialize video:  Graphics module not found";
	case OBS_VIDEO_NOT_SUPPORTED:
		throw UNSUPPORTED_ERROR;
	case OBS_VIDEO_INVALID_PARAM:
		throw "Failed to initialize video:  Invalid parameters";
	default:
		if (ret != OBS_VIDEO_SUCCESS) {
			throw UNKNOWN_ERROR;
		}
	}

	/* load audio monitoring */
	if (obs_audio_monitoring_available()) {
		const char *device_name = config_get_string(activeConfiguration, "Audio", "MonitoringDeviceName");
		const char *device_id = config_get_string(activeConfiguration, "Audio", "MonitoringDeviceId");

		obs_set_audio_monitoring_device(device_name, device_id);

		blog(LOG_INFO, "Audio monitoring device:\n\tname: %s\n\tid: %s", device_name, device_id);
	}

	InitOBSCallbacks();
	InitHotkeys();
	ui->preview->Init();

	/* hack to prevent elgato from loading its own QtNetwork that it tries
	 * to ship with */
#if defined(_WIN32) && !defined(_DEBUG)
	LoadLibraryW(L"Qt6Network");
#endif
	// Modules can access frontend information (i.e., profile and scene collection data) during their initialization,
	// and some modules (e.g., obs-websockets) are known to use the filesystem location of the current profile in their
	// own code.
	//
	// Thus, the profile and scene collection discovery needs to happen before any access to that information happens,
	// but after initializing global settings, to ensure legacy code gets valid path information.
	RefreshSceneCollections(true);

	App()->loadAppModules();

#ifdef BROWSER_AVAILABLE
	cef = obs_browser_init_panel();
	cef_js_avail = cef && obs_browser_qcef_version() >= 3;
#endif

	vcamEnabled = (obs_get_output_flags(VIRTUAL_CAM_ID) & OBS_OUTPUT_VIDEO) != 0;
	if (vcamEnabled) {
		emit VirtualCamEnabled();
	}

	UpdateProfileEncoders();

	LogEncoders();

	blog(LOG_INFO, STARTUP_SEPARATOR);

	if (!InitService()) {
		throw "Failed to initialize service";
	}

	ResetOutputs();
	CreateHotkeys();

	InitPrimitives();

	sceneDuplicationMode = config_get_bool(App()->GetUserConfig(), "BasicWindow", "SceneDuplicationMode");
	swapScenesMode = config_get_bool(App()->GetUserConfig(), "BasicWindow", "SwapScenesMode");
	editPropertiesMode = config_get_bool(App()->GetUserConfig(), "BasicWindow", "EditPropertiesMode");

	if (!opt_studio_mode) {
		SetPreviewProgramMode(config_get_bool(App()->GetUserConfig(), "BasicWindow", "PreviewProgramMode"));
	} else {
		SetPreviewProgramMode(true);
		opt_studio_mode = false;
	}

#define SET_VISIBILITY(name, control)                                                                \
	do {                                                                                         \
		if (config_has_user_value(App()->GetUserConfig(), "BasicWindow", name)) {            \
			bool visible = config_get_bool(App()->GetUserConfig(), "BasicWindow", name); \
			ui->control->setChecked(visible);                                            \
		}                                                                                    \
	} while (false)

	SET_VISIBILITY("ShowListboxToolbars", toggleListboxToolbars);
	SET_VISIBILITY("ShowStatusBar", toggleStatusBar);
#undef SET_VISIBILITY

	bool sourceIconsVisible = config_get_bool(App()->GetUserConfig(), "BasicWindow", "ShowSourceIcons");
	ui->toggleSourceIcons->setChecked(sourceIconsVisible);

	bool contextVisible = config_get_bool(App()->GetUserConfig(), "BasicWindow", "ShowContextToolbars");
	ui->toggleContextBar->setChecked(contextVisible);
	ui->contextContainer->setVisible(contextVisible);
	if (contextVisible) {
		UpdateContextBar(true);
	}
	UpdateEditMenu();

	{
		ProfileScope("OBSBasic::Load");
		const std::string sceneCollectionName{
			config_get_string(App()->GetUserConfig(), "Basic", "SceneCollection")};
		std::optional<OBS::SceneCollection> configuredCollection =
			GetSceneCollectionByName(sceneCollectionName);
		std::optional<OBS::SceneCollection> foundCollection = GetSceneCollectionByName(opt_starting_collection);

		if (foundCollection) {
			ActivateSceneCollection(foundCollection.value());
		} else if (configuredCollection) {
			ActivateSceneCollection(configuredCollection.value());
		} else {
			disableSaving--;
			SetupNewSceneCollection(sceneCollectionName);
			disableSaving++;
		}

		if (foundCollection || configuredCollection) {
			disableSaving--;
			OnEvent(OBS_FRONTEND_EVENT_SCENE_COLLECTION_LIST_CHANGED);
			OnEvent(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED);
			OnEvent(OBS_FRONTEND_EVENT_SCENE_CHANGED);
			OnEvent(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
			disableSaving++;
		}
	}

	loaded = true;

	previewEnabled = config_get_bool(App()->GetUserConfig(), "BasicWindow", "PreviewEnabled");

	if (!previewEnabled && !IsPreviewProgramMode()) {
		QMetaObject::invokeMethod(this, &OBSBasic::EnablePreviewDisplay, Qt::QueuedConnection, previewEnabled);
	} else if (!previewEnabled && IsPreviewProgramMode()) {
		QMetaObject::invokeMethod(this, &OBSBasic::EnablePreviewDisplay, Qt::QueuedConnection, true);
	}

	disableSaving--;

	auto addDisplay = [this](OBSQTDisplay *window) {
		obs_display_add_draw_callback(window->GetDisplay(), OBSBasic::RenderMain, this);

		struct obs_video_info ovi;
		if (obs_get_video_info(&ovi)) {
			ResizePreview(ovi.base_width, ovi.base_height);
		}
	};

	connect(ui->preview, &OBSQTDisplay::DisplayCreated, this, addDisplay);

	/* Show the main window, unless the tray icon isn't available
	 * or neither the setting nor flag for starting minimized is set. */
	bool sysTrayEnabled = config_get_bool(App()->GetUserConfig(), "BasicWindow", "SysTrayEnabled");
	bool sysTrayWhenStarted = config_get_bool(App()->GetUserConfig(), "BasicWindow", "SysTrayWhenStarted");
	bool hideWindowOnStart = QSystemTrayIcon::isSystemTrayAvailable() && sysTrayEnabled &&
				 (opt_minimize_tray || sysTrayWhenStarted);

#ifdef _WIN32
	SetWin32DropStyle(this);

	if (!hideWindowOnStart) {
		show();
	}
#endif

	bool alwaysOnTop = config_get_bool(App()->GetUserConfig(), "BasicWindow", "AlwaysOnTop");

#ifdef ENABLE_WAYLAND
	bool isWayland = obs_get_nix_platform() == OBS_NIX_PLATFORM_WAYLAND;
#else
	bool isWayland = false;
#endif

	if (!isWayland && (alwaysOnTop || opt_always_on_top)) {
		SetAlwaysOnTop(this, true);
		ui->actionAlwaysOnTop->setChecked(true);
	} else if (isWayland) {
		if (opt_always_on_top) {
			blog(LOG_INFO, "Always On Top not available on Wayland, ignoring.");
		}
		ui->actionAlwaysOnTop->setEnabled(false);
		ui->actionAlwaysOnTop->setVisible(false);
	}

#ifndef _WIN32
	if (!hideWindowOnStart) {
		show();
	}
#endif

	// Set up Audio Mixer dock
	AudioMixer *audioMixer = new AudioMixer(this);
	ui->mixerDock->setWidget(audioMixer);
	ui->mixerDock->setContextMenuPolicy(Qt::CustomContextMenu);

	connect(ui->mixerDock, &QDockWidget::customContextMenuRequested, audioMixer, &AudioMixer::showMixerContextMenu);

	/* setup stats dock */
	OBSBasicStats *statsDlg = new OBSBasicStats(statsDock, false);
	statsDock->setWidget(statsDlg);

	/* ----------------------------- */
	/* add custom browser docks      */
#if defined(BROWSER_AVAILABLE) && defined(YOUTUBE_ENABLED)
	YouTubeAppDock::CleanupYouTubeUrls();
#endif

#ifdef BROWSER_AVAILABLE
	if (cef) {
		QAction *action = new QAction(QTStr("Basic.MainMenu.Docks."
						    "CustomBrowserDocks"),
					      this);
		ui->menuDocks->insertAction(ui->scenesDock->toggleViewAction(), action);
		connect(action, &QAction::triggered, this, &OBSBasic::ManageExtraBrowserDocks);
		ui->menuDocks->insertSeparator(ui->scenesDock->toggleViewAction());

		LoadExtraBrowserDocks();
	}
#endif

#ifdef YOUTUBE_ENABLED
	/* setup YouTube app dock */
	if (YouTubeAppDock::IsYTServiceSelected()) {
		NewYouTubeAppDock();
	}
#endif

	const char *dockStateStr = config_get_string(App()->GetUserConfig(), "BasicWindow", "DockState");

	if (!dockStateStr) {
		on_resetDocks_triggered(true);
	} else {
		QByteArray dockState = QByteArray::fromBase64(QByteArray(dockStateStr));
		if (!restoreState(dockState)) {
			on_resetDocks_triggered(true);
		}
	}

	bool pre23Defaults = config_get_bool(App()->GetUserConfig(), "General", "Pre23Defaults");
	if (pre23Defaults) {
		bool resetDockLock23 = config_get_bool(App()->GetUserConfig(), "General", "ResetDockLock23");
		if (!resetDockLock23) {
			config_set_bool(App()->GetUserConfig(), "General", "ResetDockLock23", true);
			config_remove_value(App()->GetUserConfig(), "BasicWindow", "DocksLocked");
			config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
		}
	}

	bool docksLocked = config_get_bool(App()->GetUserConfig(), "BasicWindow", "DocksLocked");
	on_lockDocks_toggled(docksLocked);
	ui->lockDocks->blockSignals(true);
	ui->lockDocks->setChecked(docksLocked);
	ui->lockDocks->blockSignals(false);

	bool sideDocks = config_get_bool(App()->GetUserConfig(), "BasicWindow", "SideDocks");
	ui->sideDocks->setChecked(sideDocks);
	setDockCornersVertical(sideDocks);

	SystemTray(true);

	TaskbarOverlayInit();

#ifdef __APPLE__
	disableColorSpaceConversion(this);
#endif

	bool has_last_version = config_has_user_value(App()->GetAppConfig(), "General", "LastVersion");
	bool first_run = config_get_bool(App()->GetUserConfig(), "General", "FirstRun");

	if (!first_run) {
		config_set_bool(App()->GetUserConfig(), "General", "FirstRun", true);
		config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
	}

	if (!first_run && !has_last_version && !Active()) {
		QMetaObject::invokeMethod(this, &OBSBasic::on_autoConfigure_triggered, Qt::QueuedConnection);
	}

#if (defined(_WIN32) || defined(__APPLE__)) && (OBS_RELEASE_CANDIDATE > 0 || OBS_BETA > 0)
	/* Automatically set branch to "beta" the first time a pre-release build is run. */
	if (!config_get_bool(App()->GetAppConfig(), "General", "AutoBetaOptIn")) {
		config_set_string(App()->GetAppConfig(), "General", "UpdateBranch", "beta");
		config_set_bool(App()->GetAppConfig(), "General", "AutoBetaOptIn", true);
		config_save_safe(App()->GetAppConfig(), "tmp", nullptr);
	}
#endif
	TimedCheckForUpdates();

	emit userSettingChanged("BasicWindow", "VerticalVolumeControl");

	if (config_get_bool(activeConfiguration, "General", "OpenStatsOnStartup")) {
		on_stats_triggered();
	}

	OBSBasicStats::InitializeValues();

	/* ----------------------- */
	/* Add multiview menu      */

	ui->viewMenu->addSeparator();

	connect(ui->viewMenu->menuAction(), &QAction::hovered, this, &OBSBasic::updateMultiviewProjectorMenu);
	OBSBasic::updateMultiviewProjectorMenu();

	ui->sources->UpdateIcons();

#if !defined(_WIN32)
	delete ui->actionRepair;
	ui->actionRepair = nullptr;
#if !defined(__APPLE__)
	delete ui->actionShowCrashLogs;
	delete ui->actionUploadLastCrashLog;
	delete ui->menuCrashLogs;
	delete ui->actionCheckForUpdates;
	ui->actionShowCrashLogs = nullptr;
	ui->actionUploadLastCrashLog = nullptr;
	ui->menuCrashLogs = nullptr;
	ui->actionCheckForUpdates = nullptr;
#endif
#endif

#ifdef __APPLE__
	/* Remove OBS' Fullscreen Interface menu in favor of the one macOS adds by default */
	delete ui->actionFullscreenInterface;
	ui->actionFullscreenInterface = nullptr;
#else
	/* Don't show menu to raise macOS-only permissions dialog */
	delete ui->actionShowMacPermissions;
	ui->actionShowMacPermissions = nullptr;
#endif

#if defined(_WIN32) || defined(__APPLE__)
	if (App()->IsUpdaterDisabled()) {
		ui->actionCheckForUpdates->setEnabled(false);
#if defined(_WIN32)
		ui->actionRepair->setEnabled(false);
#endif
	}
#endif

#ifndef WHATSNEW_ENABLED
	delete ui->actionShowWhatsNew;
	ui->actionShowWhatsNew = nullptr;
#endif

	if (safe_mode) {
		ui->actionRestartSafe->setText(QTStr("Basic.MainMenu.Help.RestartNormal"));
	}

	UpdatePreviewProgramIndicators();
	SetSimpleRecorderMode(ui->simpleStudioModeButton->isChecked());
	OnFirstLoad();

	if (!hideWindowOnStart) {
		activateWindow();
	}

	App()->handlePluginLoadState();
}

void OBSBasic::OnFirstLoad()
{
	OnEvent(OBS_FRONTEND_EVENT_FINISHED_LOADING);

#ifdef WHATSNEW_ENABLED
	/* Attempt to load init screen if available */
	if (cef) {
		WhatsNewInfoThread *wnit = new WhatsNewInfoThread();
		connect(wnit, &WhatsNewInfoThread::Result, this, &OBSBasic::ReceivedIntroJson, Qt::QueuedConnection);

		introCheckThread.reset(wnit);
		introCheckThread->start();
	}
#endif

	Auth::Load();

	bool showLogViewerOnStartup = config_get_bool(App()->GetUserConfig(), "LogViewer", "ShowLogStartup");

	if (showLogViewerOnStartup) {
		on_actionViewCurrentLog_triggered();
	}
}

OBSBasic::~OBSBasic() {}

void OBSBasic::applicationShutdown() noexcept
{
	/* clear out UI event queue */
	QApplication::sendPostedEvents(nullptr);
#ifndef __APPLE__
	QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
#endif

	if (updateCheckThread && updateCheckThread->isRunning()) {
		updateCheckThread->wait();
	}

	if (patronJsonThread && patronJsonThread->isRunning()) {
		patronJsonThread->wait();
	}

	delete screenshotData;
	delete previewProjectorSource;
	delete previewProjectorMain;
	delete sourceProjector;
	delete sceneProjectorMenu;
	delete scaleFilteringMenu;
	delete blendingModeMenu;
	delete colorMenu;
	delete colorWidgetAction;
	delete colorSelect;
	delete deinterlaceMenu;
	delete perSceneTransitionMenu;
	delete shortcutFilter;
	delete programOptions;
	delete program;

	/* XXX: any obs data must be released before calling obs_shutdown.
	 * currently, we can't automate this with C++ RAII because of the
	 * delicate nature of obs_shutdown needing to be freed before the UI
	 * can be freed, and we have no control over the destruction order of
	 * the Qt UI stuff, so we have to manually clear any references to
	 * libobs. */
	delete cpuUsageTimer;
	os_cpu_usage_info_destroy(cpuUsageInfo);

	obs_hotkey_set_callback_routing_func(nullptr, nullptr);
	ClearHotkeys();

	service = nullptr;
	outputHandler.reset();

	delete interaction;
	delete properties;
	delete filters;
	delete transformWindow;
	delete advAudioWindow;
	delete about;
	delete remux;

	obs_display_remove_draw_callback(ui->preview->GetDisplay(), OBSBasic::RenderMain, this);

	obs_enter_graphics();
	gs_vertexbuffer_destroy(box);
	gs_vertexbuffer_destroy(boxLeft);
	gs_vertexbuffer_destroy(boxTop);
	gs_vertexbuffer_destroy(boxRight);
	gs_vertexbuffer_destroy(boxBottom);
	gs_vertexbuffer_destroy(circle);
	gs_vertexbuffer_destroy(actionSafeMargin);
	gs_vertexbuffer_destroy(graphicsSafeMargin);
	gs_vertexbuffer_destroy(fourByThreeSafeMargin);
	gs_vertexbuffer_destroy(leftLine);
	gs_vertexbuffer_destroy(topLine);
	gs_vertexbuffer_destroy(rightLine);
	obs_leave_graphics();

	/* When shutting down, sometimes source references can get in to the
	 * event queue, and if we don't forcibly process those events they
	 * won't get processed until after obs_shutdown has been called.  I
	 * really wish there were a more elegant way to deal with this via C++,
	 * but Qt doesn't use C++ in a normal way, so you can't really rely on
	 * normal C++ behavior for your data to be freed in the order that you
	 * expect or want it to. */
	QApplication::sendPostedEvents(nullptr);

#ifdef BROWSER_AVAILABLE
	DestroyPanelCookieManager();
	delete cef;
	cef = nullptr;
#endif

	handledShutdown = true;
}

void OBSBasic::toggleMixerLayout()
{
	bool vertical = config_get_bool(App()->GetUserConfig(), "BasicWindow", "VerticalVolumeControl");
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "VerticalVolumeControl", !vertical);

	emit userSettingChanged("BasicWindow", "VerticalVolumeControl");
}

static inline int AttemptToResetVideo(struct obs_video_info *ovi)
{
	return obs_reset_video(ovi);
}

static inline enum obs_scale_type GetScaleType(ConfigFile &activeConfiguration)
{
	const char *scaleTypeStr = config_get_string(activeConfiguration, "Video", "ScaleType");

	if (astrcmpi(scaleTypeStr, "bilinear") == 0) {
		return OBS_SCALE_BILINEAR;
	} else if (astrcmpi(scaleTypeStr, "lanczos") == 0) {
		return OBS_SCALE_LANCZOS;
	} else if (astrcmpi(scaleTypeStr, "area") == 0) {
		return OBS_SCALE_AREA;
	} else {
		return OBS_SCALE_BICUBIC;
	}
}

static inline enum video_format GetVideoFormatFromName(const char *name)
{
	if (astrcmpi(name, "I420") == 0) {
		return VIDEO_FORMAT_I420;
	} else if (astrcmpi(name, "NV12") == 0) {
		return VIDEO_FORMAT_NV12;
	} else if (astrcmpi(name, "I444") == 0) {
		return VIDEO_FORMAT_I444;
	} else if (astrcmpi(name, "I010") == 0) {
		return VIDEO_FORMAT_I010;
	} else if (astrcmpi(name, "P010") == 0) {
		return VIDEO_FORMAT_P010;
	} else if (astrcmpi(name, "P216") == 0) {
		return VIDEO_FORMAT_P216;
	} else if (astrcmpi(name, "P416") == 0) {
		return VIDEO_FORMAT_P416;
	}
#if 0 //currently unsupported
	else if (astrcmpi(name, "YVYU") == 0)
		return VIDEO_FORMAT_YVYU;
	else if (astrcmpi(name, "YUY2") == 0)
		return VIDEO_FORMAT_YUY2;
	else if (astrcmpi(name, "UYVY") == 0)
		return VIDEO_FORMAT_UYVY;
#endif
	else {
		return VIDEO_FORMAT_BGRA;
	}
}

static inline enum video_colorspace GetVideoColorSpaceFromName(const char *name)
{
	enum video_colorspace colorspace = VIDEO_CS_SRGB;
	if (strcmp(name, "601") == 0) {
		colorspace = VIDEO_CS_601;
	} else if (strcmp(name, "709") == 0) {
		colorspace = VIDEO_CS_709;
	} else if (strcmp(name, "2100PQ") == 0) {
		colorspace = VIDEO_CS_2100_PQ;
	} else if (strcmp(name, "2100HLG") == 0) {
		colorspace = VIDEO_CS_2100_HLG;
	}

	return colorspace;
}

int OBSBasic::ResetVideo()
{
	if (outputHandler && outputHandler->Active()) {
		return OBS_VIDEO_CURRENTLY_ACTIVE;
	}

	ProfileScope("OBSBasic::ResetVideo");

	struct obs_video_info ovi;
	int ret;

	GetConfigFPS(ovi.fps_num, ovi.fps_den);

	const char *colorFormat = config_get_string(activeConfiguration, "Video", "ColorFormat");
	const char *colorSpace = config_get_string(activeConfiguration, "Video", "ColorSpace");
	const char *colorRange = config_get_string(activeConfiguration, "Video", "ColorRange");

	ovi.graphics_module = App()->GetRenderModule();
	ovi.base_width = (uint32_t)config_get_uint(activeConfiguration, "Video", "BaseCX");
	ovi.base_height = (uint32_t)config_get_uint(activeConfiguration, "Video", "BaseCY");
	ovi.output_width = (uint32_t)config_get_uint(activeConfiguration, "Video", "OutputCX");
	ovi.output_height = (uint32_t)config_get_uint(activeConfiguration, "Video", "OutputCY");
	ovi.output_format = GetVideoFormatFromName(colorFormat);
	ovi.colorspace = GetVideoColorSpaceFromName(colorSpace);
	ovi.range = astrcmpi(colorRange, "Full") == 0 ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;
	ovi.adapter = config_get_uint(App()->GetUserConfig(), "Video", "AdapterIdx");
	ovi.gpu_conversion = true;
	ovi.scale_type = GetScaleType(activeConfiguration);

	if (ovi.base_width < 32 || ovi.base_height < 32) {
		ovi.base_width = 1920;
		ovi.base_height = 1080;
		config_set_uint(activeConfiguration, "Video", "BaseCX", 1920);
		config_set_uint(activeConfiguration, "Video", "BaseCY", 1080);
	}

	if (ovi.output_width < 32 || ovi.output_height < 32) {
		ovi.output_width = ovi.base_width;
		ovi.output_height = ovi.base_height;
		config_set_uint(activeConfiguration, "Video", "OutputCX", ovi.base_width);
		config_set_uint(activeConfiguration, "Video", "OutputCY", ovi.base_height);
	}

	ret = AttemptToResetVideo(&ovi);
	if (ret == OBS_VIDEO_CURRENTLY_ACTIVE) {
		blog(LOG_WARNING, "Tried to reset when already active");
		return ret;
	}

	if (ret == OBS_VIDEO_SUCCESS) {
		ResizePreview(ovi.base_width, ovi.base_height);
		if (program) {
			ResizeProgram(ovi.base_width, ovi.base_height);
		}

		const float sdr_white_level = (float)config_get_uint(activeConfiguration, "Video", "SdrWhiteLevel");
		const float hdr_nominal_peak_level =
			(float)config_get_uint(activeConfiguration, "Video", "HdrNominalPeakLevel");
		obs_set_video_levels(sdr_white_level, hdr_nominal_peak_level);
		OBSBasicStats::InitializeValues();
		OBSProjector::UpdateMultiviewProjectors();

		if (!collections.empty()) {
			const OBS::SceneCollection currentSceneCollection = OBSBasic::GetCurrentSceneCollection();

			bool usingAbsoluteCoordinates = currentSceneCollection.getCoordinateMode() ==
							OBS::SceneCoordinateMode::Absolute;
			OBS::Rect migrationResolution = currentSceneCollection.getMigrationResolution();

			OBS::Rect videoResolution = OBS::Rect(ovi.base_width, ovi.base_height);

			bool canMigrate = usingAbsoluteCoordinates ||
					  (!migrationResolution.isZero() && migrationResolution != videoResolution);

			ui->actionRemigrateSceneCollection->setEnabled(canMigrate);
		} else {
			ui->actionRemigrateSceneCollection->setEnabled(false);
		}

		emit CanvasResized(ovi.base_width, ovi.base_height);
		emit OutputResized(ovi.output_width, ovi.output_height);
	}

	return ret;
}

bool OBSBasic::ResetAudio()
{
	ProfileScope("OBSBasic::ResetAudio");

	struct obs_audio_info2 ai = {};
	ai.samples_per_sec = config_get_uint(activeConfiguration, "Audio", "SampleRate");

	const char *channelSetupStr = config_get_string(activeConfiguration, "Audio", "ChannelSetup");

	if (strcmp(channelSetupStr, "Mono") == 0) {
		ai.speakers = SPEAKERS_MONO;
	} else if (strcmp(channelSetupStr, "2.1") == 0) {
		ai.speakers = SPEAKERS_2POINT1;
	} else if (strcmp(channelSetupStr, "4.0") == 0) {
		ai.speakers = SPEAKERS_4POINT0;
	} else if (strcmp(channelSetupStr, "4.1") == 0) {
		ai.speakers = SPEAKERS_4POINT1;
	} else if (strcmp(channelSetupStr, "5.1") == 0) {
		ai.speakers = SPEAKERS_5POINT1;
	} else if (strcmp(channelSetupStr, "7.1") == 0) {
		ai.speakers = SPEAKERS_7POINT1;
	} else {
		ai.speakers = SPEAKERS_STEREO;
	}

	bool lowLatencyAudioBuffering = config_get_bool(App()->GetUserConfig(), "Audio", "LowLatencyAudioBuffering");
	if (lowLatencyAudioBuffering) {
		ai.max_buffering_ms = 20;
		ai.fixed_buffering = true;
	}

	return obs_reset_audio2(&ai);
}

void OBSBasic::close()
{
	if (isClosePromptOpen() || isClosing()) {
		return;
	}

	OBSMainWindow::close();
}

void OBSBasic::closeEvent(QCloseEvent *event)
{
	if (isClosePromptOpen() || isClosing()) {
		return;
	}

	if (isVisible()) {
		config_set_string(App()->GetUserConfig(), "BasicWindow", "geometry",
				  saveGeometry().toBase64().constData());
	}

	if (!isReadyToClose()) {
		event->ignore();

		QTimer::singleShot(1000, this, &OBSBasic::close);
		return;
	}

	if (remux && !remux->close()) {
		event->ignore();
		restart = false;

		return;
	}

	if (shouldPromptForClose()) {
		event->ignore();
		restart = false;

		if (!isClosePromptOpen()) {
			bool shouldClose = promptToClose();

			if (shouldClose) {
				closeWindow();
			}
		}

		return;
	}

	QWidget::closeEvent(event);
	if (!event->isAccepted()) {
		return;
	}

	closeWindow();
}

bool OBSBasic::nativeEvent(const QByteArray &, void *message, qintptr *)
{
#ifdef _WIN32
	const MSG &msg = *static_cast<MSG *>(message);
	switch (msg.message) {
	case WM_MOVE:
		for (OBSQTDisplay *const display : findChildren<OBSQTDisplay *>()) {
			display->OnMove();
		}
		break;
	case WM_DISPLAYCHANGE:
		for (OBSQTDisplay *const display : findChildren<OBSQTDisplay *>()) {
			display->OnDisplayChange();
		}
	}
#else
	UNUSED_PARAMETER(message);
#endif

	return false;
}

void OBSBasic::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::WindowStateChange) {
		QWindowStateChangeEvent *stateEvent = (QWindowStateChangeEvent *)event;

		if (isMinimized()) {
			if (trayIcon && trayIcon->isVisible() && sysTrayMinimizeToTray()) {
				ToggleShowHide();
				return;
			}

			if (previewEnabled) {
				EnablePreviewDisplay(false);
			}
		} else if (stateEvent->oldState() & Qt::WindowMinimized && isVisible()) {
			if (previewEnabled) {
				EnablePreviewDisplay(true);
			}
		}
	}
}

void OBSBasic::GetFPSCommon(uint32_t &num, uint32_t &den) const
{
	const char *val = config_get_string(activeConfiguration, "Video", "FPSCommon");

	if (strcmp(val, "10") == 0) {
		num = 10;
		den = 1;
	} else if (strcmp(val, "20") == 0) {
		num = 20;
		den = 1;
	} else if (strcmp(val, "24 NTSC") == 0) {
		num = 24000;
		den = 1001;
	} else if (strcmp(val, "25 PAL") == 0) {
		num = 25;
		den = 1;
	} else if (strcmp(val, "29.97") == 0) {
		num = 30000;
		den = 1001;
	} else if (strcmp(val, "48") == 0) {
		num = 48;
		den = 1;
	} else if (strcmp(val, "50 PAL") == 0) {
		num = 50;
		den = 1;
	} else if (strcmp(val, "59.94") == 0) {
		num = 60000;
		den = 1001;
	} else if (strcmp(val, "60") == 0) {
		num = 60;
		den = 1;
	} else {
		num = 30;
		den = 1;
	}
}

void OBSBasic::GetFPSInteger(uint32_t &num, uint32_t &den) const
{
	num = (uint32_t)config_get_uint(activeConfiguration, "Video", "FPSInt");
	den = 1;
}

void OBSBasic::GetFPSFraction(uint32_t &num, uint32_t &den) const
{
	num = (uint32_t)config_get_uint(activeConfiguration, "Video", "FPSNum");
	den = (uint32_t)config_get_uint(activeConfiguration, "Video", "FPSDen");
}

void OBSBasic::GetFPSNanoseconds(uint32_t &num, uint32_t &den) const
{
	num = 1000000000;
	den = (uint32_t)config_get_uint(activeConfiguration, "Video", "FPSNS");
}

void OBSBasic::GetConfigFPS(uint32_t &num, uint32_t &den) const
{
	uint32_t type = config_get_uint(activeConfiguration, "Video", "FPSType");

	if (type == 1) { //"Integer"
		GetFPSInteger(num, den);
	} else if (type == 2) { //"Fraction"
		GetFPSFraction(num, den);
	}
	/*
	 * 	else if (false) //"Nanoseconds", currently not implemented
	 *		GetFPSNanoseconds(num, den);
	 */
	else {
		GetFPSCommon(num, den);
	}
}

config_t *OBSBasic::Config() const
{
	return activeConfiguration;
}

void OBSBasic::saveAll()
{
	if (isVisible()) {
		config_set_string(App()->GetUserConfig(), "BasicWindow", "geometry",
				  saveGeometry().toBase64().constData());
	}

	std::call_once(saveOnceFlag, [this]() {
		Auth::Save();
		SaveProjectNow();

		config_set_string(App()->GetUserConfig(), "BasicWindow", "DockState",
				  saveState().toBase64().constData());

#ifdef BROWSER_AVAILABLE
		if (cef) {
			SaveExtraBrowserDocks();
		}
#endif
	});

	config_set_int(App()->GetAppConfig(), "General", "LastVersion", LIBOBS_API_VER);
	config_save_safe(App()->GetAppConfig(), "tmp", nullptr);

	config_set_bool(App()->GetUserConfig(), "BasicWindow", "PreviewEnabled", previewEnabled);
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "AlwaysOnTop", ui->actionAlwaysOnTop->isChecked());
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SceneDuplicationMode", sceneDuplicationMode);
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SwapScenesMode", swapScenesMode);
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "EditPropertiesMode", editPropertiesMode);
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "PreviewProgramMode", IsPreviewProgramMode());
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "DocksLocked", ui->lockDocks->isChecked());
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SideDocks", ui->sideDocks->isChecked());
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
}

bool OBSBasic::isReadyToClose()
{
	/* Wait for multitrack video stream to start/finish processing in the background */
	if (setupStreamingGuard.valid() &&
	    setupStreamingGuard.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
		return false;
	}

	/* Do not close window if inside of a temporary event loop because we
	 * could be inside of an Auth::LoadUI call.  Keep trying once per
	 * second until we've exit any known sub-loops. */
	if (os_atomic_load_long(&insideEventLoop) != 0) {
		return false;
	}

#ifdef YOUTUBE_ENABLED
	/* Also don't close the window if the youtube stream check is active */
	if (youtubeStreamCheckThread) {
		return false;
	}
#endif

	return true;
}

bool OBSBasic::shouldPromptForClose()
{
	bool confirmOnExit = config_get_bool(App()->GetUserConfig(), "General", "ConfirmOnExit");
	if (confirmOnExit && outputHandler && outputHandler->Active() && !clearingFailed) {
		return true;
	}

	return false;
}

bool OBSBasic::promptToClose()
{
	isClosePromptOpen_ = true;

	SetShowing(true);
	QMessageBox::StandardButton button =
		OBSMessageBox::question(this, QTStr("ConfirmExit.Title"), QTStr("ConfirmExit.Text"),
					QMessageBox::StandardButtons(QMessageBox::Ok | QMessageBox::Cancel));

	if (button == QMessageBox::Cancel) {
		isClosePromptOpen_ = false;
		return false;
	}

	isClosePromptOpen_ = false;
	return true;
}

void OBSBasic::closeWindow()
{
	if (isClosing()) {
		return;
	}

	blog(LOG_INFO, SHUTDOWN_SEPARATOR);

	isClosing_ = true;

	/* While closing, a resize event to OBSQTDisplay could be triggered.
	 * The graphics thread on macOS dispatches a lambda function to be
	 * executed asynchronously in the main thread. However, the display is
	 * sometimes deleted before the lambda function is actually executed.
	 * To avoid such a case, destroy displays earlier than others such as
	 * deleting browser docks. */
	ui->preview->DestroyDisplay();
	if (program) {
		program->DestroyDisplay();
	}

	if (outputHandler->VirtualCamActive()) {
		outputHandler->StopVirtualCam();
	}

	if (introCheckThread) {
		introCheckThread->wait();
	}
	if (whatsNewInitThread) {
		whatsNewInitThread->wait();
	}
	if (updateCheckThread) {
		updateCheckThread->wait();
	}
	if (logUploadThread) {
		logUploadThread->wait();
	}
	if (devicePropertiesThread && devicePropertiesThread->isRunning()) {
		devicePropertiesThread->wait();
		devicePropertiesThread.reset();
	}

	QApplication::sendPostedEvents(nullptr);

	signalHandlers.clear();
	delete extraBrowsers;

	saveAll();

	auth.reset();

#ifdef BROWSER_AVAILABLE
	ClearExtraBrowserDocks();

#ifdef YOUTUBE_ENABLED
	if (youtubeAppDock) {
		RemoveDockWidget(youtubeAppDock->objectName());
		youtubeAppDock = nullptr;
	}
#endif
#endif

	OnEvent(OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN);

	disableSaving++;

	/* Clear all scene data (dialogs, widgets, widget sub-items, scenes,
	 * sources, etc) so that all references are released before shutdown */
	ClearSceneData();

	OnEvent(OBS_FRONTEND_EVENT_EXIT);

	// Destroys the frontend API so plugins can't continue calling it
	obs_frontend_set_callbacks_internal(nullptr);
	api = nullptr;

	applicationShutdown();

#ifndef __APPLE__
	deleteLater();

	emit mainWindowClosed();

	QMetaObject::invokeMethod(App(), &OBSApp::quit, Qt::QueuedConnection);
#else
	QMetaObject::invokeMethod(App(), &OBSApp::quit, Qt::QueuedConnection);
#endif
}

void OBSBasic::UpdateEditMenu()
{
	QModelIndexList items = GetAllSelectedSourceItems();
	int totalCount = items.count();
	size_t filter_count = 0;

	if (totalCount == 1) {
		OBSSceneItem sceneItem = ui->sources->Get(GetTopSelectedSourceItem());
		OBSSource source = obs_sceneitem_get_source(sceneItem);
		filter_count = obs_source_filter_count(source);
	}

	int videoCount = 0;
	bool canTransformMultiple = false;
	for (int i = 0; i < totalCount; i++) {
		OBSSceneItem item = ui->sources->Get(items.value(i).row());
		OBSSource source = obs_sceneitem_get_source(item);
		const uint32_t flags = obs_source_get_output_flags(source);
		const bool hasVideo = (flags & OBS_SOURCE_VIDEO) != 0;
		if (hasVideo && !obs_sceneitem_locked(item)) {
			canTransformMultiple = true;
		}

		if (hasVideo) {
			videoCount++;
		}
	}
	const bool canTransformSingle = videoCount == 1 && totalCount == 1;

	OBSSceneItem curItem = GetCurrentSceneItem();
	bool locked = curItem && obs_sceneitem_locked(curItem);

	ui->actionCopySource->setEnabled(totalCount > 0);
	ui->actionEditTransform->setEnabled(canTransformSingle && !locked);
	ui->actionCopyTransform->setEnabled(canTransformSingle);
	ui->actionPasteTransform->setEnabled(canTransformMultiple && hasCopiedTransform && videoCount > 0);
	ui->actionCopyFilters->setEnabled(filter_count > 0);
	ui->actionPasteFilters->setEnabled(!obs_weak_source_expired(copyFiltersSource()) && totalCount > 0);
	auto pasteType = getItemPasteType();
	ui->actionPasteRef->setEnabled(pasteType == OBS::ItemPasteType::Reference ||
				       pasteType == OBS::ItemPasteType::Both);
	ui->actionPasteDup->setEnabled(pasteType == OBS::ItemPasteType::Duplicate ||
				       pasteType == OBS::ItemPasteType::Both);

	ui->actionMoveUp->setEnabled(totalCount > 0);
	ui->actionMoveDown->setEnabled(totalCount > 0);
	ui->actionMoveToTop->setEnabled(totalCount > 0);
	ui->actionMoveToBottom->setEnabled(totalCount > 0);

	ui->actionResetTransform->setEnabled(canTransformMultiple);
	ui->actionRotate90CW->setEnabled(canTransformMultiple);
	ui->actionRotate90CCW->setEnabled(canTransformMultiple);
	ui->actionRotate180->setEnabled(canTransformMultiple);
	ui->actionFlipHorizontal->setEnabled(canTransformMultiple);
	ui->actionFlipVertical->setEnabled(canTransformMultiple);
	ui->actionFitToScreen->setEnabled(canTransformMultiple);
	ui->actionStretchToScreen->setEnabled(canTransformMultiple);
	ui->actionCenterToScreen->setEnabled(canTransformMultiple);
	ui->actionVerticalCenter->setEnabled(canTransformMultiple);
	ui->actionHorizontalCenter->setEnabled(canTransformMultiple);
}

void OBSBasic::UpdateTitleBar()
{
	stringstream name;

	const char *profile = config_get_string(App()->GetUserConfig(), "Basic", "Profile");
	const char *sceneCollection = config_get_string(App()->GetUserConfig(), "Basic", "SceneCollection");

	name << "OBS ";
	if (previewProgramMode) {
		name << "Studio ";
	}

	name << App()->GetVersionString(false);
	if (safe_mode) {
		name << " (" << Str("TitleBar.SafeMode") << ")";
	}
	if (App()->IsPortableMode()) {
		name << " - " << Str("TitleBar.PortableMode");
	}

	name << " - " << Str("TitleBar.Profile") << ": " << profile;
	name << " - " << Str("TitleBar.Scenes") << ": " << sceneCollection;

	setWindowTitle(QT_UTF8(name.str().c_str()));
}

OBSBasic *OBSBasic::Get()
{
	return reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
}

void OBSBasic::UpdatePatronJson(const std::string &text, const std::string &error)
{
	if (!error.empty()) {
		return;
	}

	patronJson = text;
}

void OBSBasic::SetDisplayAffinity(QWindow *window)
{
	if (!SetDisplayAffinitySupported()) {
		return;
	}

	bool hideFromCapture = config_get_bool(App()->GetUserConfig(), "BasicWindow", "HideOBSWindowsFromCapture");

	// Don't hide projectors, those are designed to be visible / captured
	if (window->property("isOBSProjectorWindow") == true) {
		return;
	}

#ifdef _WIN32
	HWND hwnd = (HWND)window->winId();

	DWORD curAffinity;
	if (GetWindowDisplayAffinity(hwnd, &curAffinity)) {
		if (hideFromCapture && curAffinity != WDA_EXCLUDEFROMCAPTURE) {
			SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
		} else if (!hideFromCapture && curAffinity != WDA_NONE) {
			SetWindowDisplayAffinity(hwnd, WDA_NONE);
		}
	}

#else
	// TODO: Implement for other platforms if possible. Don't forget to
	// implement SetDisplayAffinitySupported too!
	UNUSED_PARAMETER(hideFromCapture);
#endif
}

void OBSBasic::OnEvent(enum obs_frontend_event event)
{
	if (api) {
		api->on_event(event);
	}
}

OBSPromptResult OBSBasic::PromptForName(const OBSPromptRequest &request, const OBSPromptCallback &callback)
{
	OBSPromptResult result;

	for (;;) {
		result.success = false;

		if (request.withOption && !request.optionPrompt.empty()) {
			result.optionValue = request.optionValue;

			result.success = NameDialog::AskForNameWithOption(
				this, request.title.c_str(), request.prompt.c_str(), result.promptValue,
				request.optionPrompt.c_str(), result.optionValue,
				(request.promptValue.empty() ? nullptr : request.promptValue.c_str()));

		} else {
			result.success = NameDialog::AskForName(
				this, request.title.c_str(), request.prompt.c_str(), result.promptValue,
				(request.promptValue.empty() ? nullptr : request.promptValue.c_str()));
		}

		if (!result.success) {
			break;
		}

		if (result.promptValue.empty()) {
			OBSMessageBox::warning(this, QTStr("NoNameEntered.Title"), QTStr("NoNameEntered.Text"));
			continue;
		}

		if (!callback(result)) {
			OBSMessageBox::warning(this, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
			continue;
		}

		break;
	}

	return result;
}

void OBSBasic::on_actionOpenPluginManager_triggered()
{
	App()->pluginManagerOpenDialog();
}


void OBSBasic::SetupSimpleRecorderUi()
{
	// Keep OBS's capture backends and output handlers; this is a recorder-first
	// workspace layered over the existing OBS frontend.
	auto *bar = ui->simpleRecorderBar;
	if (!bar)
		return;

	bar->setVisible(true);
	bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	connect(ui->simpleSettingsButton, &QPushButton::clicked, this, &OBSBasic::on_action_RecordingSettings_triggered);
	connect(ui->simpleRecordingsButton, &QPushButton::clicked, this, [this]() {
		const char *mode = config_get_string(activeConfiguration, "Output", "Mode");
		const bool advanced = mode && strcmp(mode, "Advanced") == 0;
		const char *advancedType = config_get_string(activeConfiguration, "AdvOut", "RecType");
		const char *path = advanced
				   ? ((advancedType && strcmp(advancedType, "Standard") != 0)
					      ? config_get_string(activeConfiguration, "AdvOut", "FFFilePath")
					      : config_get_string(activeConfiguration, "AdvOut", "RecFilePath"))
				   : config_get_string(activeConfiguration, "SimpleOutput", "FilePath");
		const QString recordingPath = QString::fromUtf8(path ? path : "");

		QDialog dialog(this);
		dialog.setWindowTitle(QStringLiteral("Последние записи"));
		dialog.resize(820, 620);

		auto *layout = new QVBoxLayout(&dialog);
		auto *title = new QLabel(QStringLiteral("Последние записи"), &dialog);
		title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #f0f2f5;"));
		auto *folder = new QLabel(recordingPath, &dialog);
		folder->setStyleSheet(QStringLiteral("color: #9299a5; font-size: 10px;"));
		folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
		folder->setWordWrap(true);
		auto *list = new QListWidget(&dialog);
		list->setViewMode(QListView::IconMode);
		list->setFlow(QListView::LeftToRight);
		list->setResizeMode(QListView::Adjust);
		list->setMovement(QListView::Static);
		list->setWrapping(true);
		list->setWordWrap(true);
		list->setSpacing(8);
		list->setIconSize(QSize(168, 94));
		list->setGridSize(QSize(192, 150));
		list->setSelectionMode(QAbstractItemView::SingleSelection);
		list->setUniformItemSizes(true);
		dialog.setStyleSheet(QStringLiteral(
			"QDialog { background: #1e2024; color: #eef0f3; }"
			"QLabel { color: #c7cbd2; }"
			"QListWidget { background: #17191d; border: 1px solid #343840; border-radius: 10px; padding: 8px; }"
			"QListWidget::item { color: #e7eaf0; background: #262a30; border: 1px solid #343840; "
			"border-radius: 8px; padding: 7px; }"
			"QListWidget::item:hover { background: #303640; }"
			"QListWidget::item:selected { background: #34485f; border-color: #648bb4; }"
			"QPushButton { color: #eef0f3; background: #30343b; border: 1px solid #464c56; "
			"border-radius: 7px; padding: 8px 14px; min-height: 22px; }"
			"QPushButton:hover { background: #3c424c; }"
			"QPushButton:disabled { color: #747b86; background: #25282d; }"));
		auto *empty = new QLabel(&dialog);
		empty->setAlignment(Qt::AlignCenter);
		empty->setWordWrap(true);
		empty->setMinimumHeight(70);
		auto *buttons = new QHBoxLayout;
		auto *openButton = new QPushButton(QStringLiteral("Открыть запись"), &dialog);
		auto *folderButton = new QPushButton(QStringLiteral("Открыть папку"), &dialog);
		auto *refreshButton = new QPushButton(QStringLiteral("Обновить"), &dialog);
		auto *closeButton = new QPushButton(QStringLiteral("Закрыть"), &dialog);
		openButton->setEnabled(false);
		folderButton->setEnabled(!recordingPath.isEmpty() && QDir(recordingPath).exists());
		buttons->addWidget(openButton);
		buttons->addWidget(folderButton);
		buttons->addStretch();
		buttons->addWidget(refreshButton);
		buttons->addWidget(closeButton);
		layout->addWidget(title);
		layout->addWidget(folder);
		layout->addWidget(list, 1);
		layout->addWidget(empty);
		layout->addLayout(buttons);

		auto refreshList = [list, empty, recordingPath]() {
			list->clear();
			if (recordingPath.isEmpty()) {
				empty->setText(QStringLiteral(
					"Папка записей не настроена. Откройте настройки качества и укажите путь сохранения."));
				empty->show();
				return;
			}

			QDir directory(recordingPath);
			if (!directory.exists()) {
				empty->setText(QStringLiteral("Папка записей ещё не создана:\n%1").arg(recordingPath));
				empty->show();
				return;
			}

			const QStringList filters = {
				QStringLiteral("*.mkv"),  QStringLiteral("*.mp4"), QStringLiteral("*.mov"),
				QStringLiteral("*.m4v"),  QStringLiteral("*.mpeg"), QStringLiteral("*.mpg"),
				QStringLiteral("*.m2ts"), QStringLiteral("*.mts"),  QStringLiteral("*.flv"),
				QStringLiteral("*.ts"),   QStringLiteral("*.avi"),  QStringLiteral("*.webm"),
				QStringLiteral("*.wmv"),  QStringLiteral("*.ogv"),  QStringLiteral("*.3gp")};
			const QFileInfoList files =
				directory.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Time | QDir::Reversed);
			QFileIconProvider iconProvider;
			int added = 0;
			for (const QFileInfo &info : files) {
				if (added >= 30)
					break;
				const QString size = info.size() >= 1024LL * 1024LL * 1024LL
						     ? QStringLiteral("%1 GB").arg(info.size() / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1)
						     : QStringLiteral("%1 MB").arg(info.size() / (1024.0 * 1024.0), 0, 'f', 1);
				auto *item = new QListWidgetItem(
					QStringLiteral("%1\n%2  •  %3")
						.arg(info.fileName(), info.lastModified().toString(QStringLiteral("dd.MM.yyyy HH:mm")), size),
					list);
				item->setIcon(iconProvider.icon(info));
				item->setData(Qt::UserRole, info.absoluteFilePath());
				item->setToolTip(QStringLiteral("%1\n%2").arg(info.absoluteFilePath(),
									     info.lastModified().toString(Qt::ISODate)));
				item->setTextAlignment(Qt::AlignHCenter);
				item->setSizeHint(QSize(184, 142));
#ifdef _WIN32
				QPointer<QListWidget> listGuard(list);
				const QString filePath = info.absoluteFilePath();
				QThreadPool::globalInstance()->start(QRunnable::create([listGuard, filePath]() {
					QImage thumbnail = LoadShellVideoThumbnail(filePath);
					if (thumbnail.isNull() || !listGuard)
						return;

					QMetaObject::invokeMethod(listGuard.data(), [listGuard, filePath, thumbnail]() {
						if (!listGuard)
							return;
						for (int row = 0; row < listGuard->count(); ++row) {
							QListWidgetItem *current = listGuard->item(row);
							if (current->data(Qt::UserRole).toString() == filePath) {
								current->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
								break;
							}
						}
					}, Qt::QueuedConnection);
				}));
#endif
				++added;
			}

			empty->setText(added == 0 ? QStringLiteral("В этой папке пока нет записей.") : QString());
			empty->setVisible(added == 0);
		};

		auto openSelected = [&dialog, list]() {
			QListWidgetItem *item = list->currentItem();
			if (!item)
				return;
			const QString filePath = item->data(Qt::UserRole).toString();
			if (QFileInfo::exists(filePath)) {
				QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
				dialog.accept();
			}
		};
		connect(list, &QListWidget::currentItemChanged, &dialog,
			[openButton](QListWidgetItem *current, QListWidgetItem *) { openButton->setEnabled(current != nullptr); });
		connect(list, &QListWidget::itemDoubleClicked, &dialog,
			[openSelected](QListWidgetItem *) { openSelected(); });
		connect(openButton, &QPushButton::clicked, &dialog, openSelected);
		connect(folderButton, &QPushButton::clicked, &dialog, [recordingPath]() {
			if (!recordingPath.isEmpty() && QDir(recordingPath).exists())
				QDesktopServices::openUrl(QUrl::fromLocalFile(recordingPath));
		});
		connect(refreshButton, &QPushButton::clicked, &dialog, refreshList);
		connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
		refreshList();
		dialog.exec();
	});
	connect(ui->simpleCaptureSourceButton, &QPushButton::clicked, this, &OBSBasic::AddSimpleCaptureSource);
	connect(ui->simpleRecordButton, &QPushButton::clicked, this, [this]() {
		RecordActionTriggered();
	});
	connect(ui->simpleStudioModeButton, &QPushButton::toggled, this, &OBSBasic::SetSimpleRecorderMode);
	connect(ui->simplePreviewButton, &QPushButton::toggled, this, [this](bool) {
		if (ui->simpleStudioModeButton->isChecked())
			return;
		SetSimpleRecorderMode(false);
	});

	simpleRecordTimer = new QTimer(this);
	simpleRecordTimer->setInterval(1000);

	auto updateRecordButton = [this]() {
		const bool active = RecordingActive();
		const bool paused = active && isRecordingPausable && outputHandler && outputHandler->fileOutput &&
				    obs_output_paused(outputHandler->fileOutput);
		const uint32_t hours = simpleRecordingSeconds / 3600;
		const uint32_t minutes = (simpleRecordingSeconds / 60) % 60;
		const uint32_t seconds = simpleRecordingSeconds % 60;
		const QString elapsed = QString::asprintf("%02u:%02u:%02u", hours, minutes, seconds);

		ui->simpleRecordButton->setText(active ? QStringLiteral("■  Остановить запись")
							       : QStringLiteral("●  Начать запись"));
		ui->simpleRecordStatus->setText(
			paused ? QStringLiteral("ПАУЗА  ") + elapsed
			       : active ? QStringLiteral("● REC  ") + elapsed : QStringLiteral("ГОТОВ К ЗАПИСИ"));
		ui->simpleRecordStatus->setStyleSheet(
			active ? QStringLiteral("color: #ff6874; font-size: 10px; font-weight: 700; letter-spacing: 0.5px;")
			       : QStringLiteral("color: #aeb4be; font-size: 10px; font-weight: 700; letter-spacing: 0.5px;"));

		const bool canPause = active && isRecordingPausable;
		ui->simplePauseButton->setVisible(canPause);
		ui->simplePauseButton->setText(paused ? QStringLiteral("▶  Продолжить")
							      : QStringLiteral("Ⅱ  Пауза"));
	};

	connect(simpleRecordTimer, &QTimer::timeout, this, [this, updateRecordButton]() {
		++simpleRecordingSeconds;
		updateRecordButton();
	});
	connect(this, &OBSBasic::RecordingStarted, this, [this, updateRecordButton]() {
		simpleRecordingSeconds = 0;
		simpleRecordTimer->start();
		updateRecordButton();
	});
	connect(this, &OBSBasic::RecordingStopped, this, [this, updateRecordButton]() {
		simpleRecordTimer->stop();
		simpleRecordingSeconds = 0;
		updateRecordButton();
	});
	connect(this, &OBSBasic::RecordingPaused, this, [this, updateRecordButton]() {
		simpleRecordTimer->stop();
		updateRecordButton();
	});
	connect(this, &OBSBasic::RecordingUnpaused, this, [this, updateRecordButton]() {
		simpleRecordTimer->start();
		updateRecordButton();
	});
	connect(ui->simplePauseButton, &QPushButton::clicked, this, [this]() {
		RecordPauseToggled();
	});
	updateRecordButton();

	// OBS output channels 1 and 3 are the configured desktop and microphone
	// buses. A missing device disables its shortcut instead of muting an
	// unrelated mixer source.
	auto connectAudioToggle = [this](QPushButton *button, uint32_t channel, const QString &label) {
		connect(button, &QPushButton::toggled, this, [button, channel, label](bool enabled) {
			OBSSourceAutoRelease source(obs_get_output_source(channel));
			if (!source) {
				button->setEnabled(false);
				return;
			}
			obs_source_set_muted(source.Get(), !enabled);
			button->setText(enabled ? label : label + QStringLiteral(" — выкл."));
		});
	};
	connectAudioToggle(ui->simpleMicButton, 3, QStringLiteral("Микрофон"));
	connectAudioToggle(ui->simpleSystemAudioButton, 1, QStringLiteral("Системный звук"));

	ui->simplePreviewButton->setChecked(false);
	ui->simpleStudioModeButton->setChecked(false);
}

void OBSBasic::AddSimpleCaptureSource()
{
#if defined(_WIN32)
	const int mode = ui->simpleCaptureMode->currentIndex();
	const char *sourceId = nullptr;
	const char *baseName = nullptr;

	switch (mode) {
	case 0:
		sourceId = "monitor_capture";
		baseName = "Захват экрана";
		break;
	case 1:
		sourceId = "game_capture";
		baseName = "Захват игры";
		break;
	case 2:
		sourceId = "window_capture";
		baseName = "Захват окна";
		break;
	case 3:
		sourceId = "monitor_capture";
		baseName = "Захват области";
		break;
	default:
		return;
	}

	std::string sourceName = baseName;
	for (uint32_t suffix = 2;; ++suffix) {
		OBSSourceAutoRelease existing(obs_get_source_by_name(sourceName.c_str()));
		if (!existing)
			break;
		sourceName = std::string(baseName) + " (" + std::to_string(suffix) + ")";
	}

	const char *latestSourceId = obs_get_latest_input_type_id(sourceId);
	if (!latestSourceId) {
		QMessageBox::warning(this, QStringLiteral("Источник недоступен"),
				     QStringLiteral("Выбранный тип захвата не зарегистрирован в этой сборке OBS."));
		return;
	}

	// Match OBS's native source creation path: use the latest registered input
	// type ID and let the source apply its own default settings.
	OBSSourceAutoRelease source(obs_source_create(latestSourceId, sourceName.c_str(), nullptr, nullptr));
	if (!source) {
		QMessageBox::warning(
			this, QStringLiteral("Не удалось добавить источник"),
			QStringLiteral("OBS не смог создать выбранный источник захвата. Проверьте, что модуль захвата Windows загружен."));
		return;
	}

	// Use OBS's own dynamic properties for monitor/window selection. This
	// reuses Win-Capture's native device and target enumeration instead of
	// maintaining a second, potentially stale list in the recorder UI.
	OBSDataAutoRelease sourceSettings = obs_source_get_settings(source.Get());
	OBSDataAutoRelease originalSettings = obs_data_create();
	obs_data_apply(originalSettings.Get(), sourceSettings.Get());

	QDialog setupDialog(this);
	setupDialog.setWindowTitle(QStringLiteral("Настройка захвата"));
	setupDialog.resize(560, 420);
	auto *setupLayout = new QVBoxLayout(&setupDialog);
	auto *setupLabel = new QLabel(QStringLiteral(
		"Выберите монитор, окно или способ захвата. Список формируется самим OBS."), &setupDialog);
	setupLabel->setWordWrap(true);
	auto *properties = new OBSPropertiesView(sourceSettings.Get(), source.Get(),
						  (PropertiesReloadCallback)obs_source_properties, nullptr,
						  (PropertiesVisualUpdateCb)obs_source_update);
	properties->setMinimumWidth(500);
	properties->setMinimumHeight(220);
	auto *setupButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &setupDialog);
	setupButtons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Добавить источник"));
	connect(setupButtons, &QDialogButtonBox::accepted, &setupDialog, &QDialog::accept);
	connect(setupButtons, &QDialogButtonBox::rejected, &setupDialog, &QDialog::reject);
	setupLayout->addWidget(setupLabel);
	setupLayout->addWidget(properties, 1);
	setupLayout->addWidget(setupButtons);

	if (setupDialog.exec() != QDialog::Accepted) {
		obs_source_update(source.Get(), originalSettings.Get());
		obs_source_remove(source.Get());
		return;
	}
	if (properties->DeferUpdate())
		properties->UpdateSettings();

	OBSScene scene = GetCurrentScene();
	if (!scene) {
		QMessageBox::warning(this, QStringLiteral("Нет активной сцены"),
				     QStringLiteral("Сначала создайте или выберите сцену OBS."));
		obs_source_remove(source.Get());
		return;
	}

	obs_sceneitem_t *sceneItem = obs_scene_add(scene.Get(), source.Get());
	if (!sceneItem) {
		QMessageBox::warning(this, QStringLiteral("Не удалось добавить источник"),
				     QStringLiteral("Источник создан, но не удалось добавить его в текущую сцену."));
		obs_source_remove(source.Get());
		return;
	}

	// Select the new source in OBS so preview transforms (including Alt-drag
	// cropping in Area mode) target the source the user just added.
	obs_sceneitem_select(sceneItem, true);

	// Register the operation in OBS's native undo stack so Ctrl+Z and redo
	// behave exactly as they do for sources created in the standard picker.
	OBSSource currentSceneSource = GetCurrentSceneSource();
	const char *sceneUuidRaw = currentSceneSource ? obs_source_get_uuid(currentSceneSource.Get()) : nullptr;
	const char *sourceUuidRaw = obs_source_get_uuid(source.Get());
	if (sceneUuidRaw && sourceUuidRaw) {
		const std::string sceneUuid(sceneUuidRaw);
		OBSDataAutoRelease wrapper(obs_data_create());
		obs_data_set_string(wrapper.Get(), "id", latestSourceId);
		obs_data_set_int(wrapper.Get(), "item_id", obs_sceneitem_get_id(sceneItem));
		obs_data_set_string(wrapper.Get(), "name", sourceName.c_str());
		obs_data_set_bool(wrapper.Get(), "visible", true);
		OBSDataAutoRelease savedSourceSettings = obs_source_get_settings(source.Get());
		obs_data_set_obj(wrapper.Get(), "settings", savedSourceSettings.Get());

		auto undo = [sceneUuid](const std::string &uuid) {
			OBSBasic *main = OBSBasic::Get();
			OBSSourceAutoRelease addedSource(obs_get_source_by_uuid(uuid.c_str()));
			if (addedSource)
				obs_source_remove(addedSource.Get());
			OBSSourceAutoRelease sceneSource(obs_get_source_by_uuid(sceneUuid.c_str()));
			if (sceneSource)
				main->SetCurrentScene(sceneSource.Get(), true);
		};
		auto redo = [sceneUuid](const std::string &data) {
			OBSBasic *main = OBSBasic::Get();
			OBSSourceAutoRelease sceneSource(obs_get_source_by_uuid(sceneUuid.c_str()));
			if (!sceneSource)
				return;
			main->SetCurrentScene(sceneSource.Get(), true);

			OBSDataAutoRelease saved(obs_data_create_from_json(data.c_str()));
			const char *savedId = obs_get_latest_input_type_id(obs_data_get_string(saved.Get(), "id"));
			if (!savedId)
				return;
			OBSDataAutoRelease savedSourceSettings = obs_data_get_obj(saved.Get(), "settings");
			OBSSourceAutoRelease restored(obs_source_create(
				savedId, obs_data_get_string(saved.Get(), "name"), savedSourceSettings.Get(), nullptr));
			if (!restored)
				return;

			OBSScene currentScene = main->GetCurrentScene();
			if (!currentScene)
				return;
			obs_sceneitem_t *restoredItem = obs_scene_add(currentScene.Get(), restored.Get());
			if (restoredItem) {
				obs_sceneitem_set_id(restoredItem, static_cast<int64_t>(obs_data_get_int(saved.Get(), "item_id")));
				obs_sceneitem_set_visible(restoredItem, obs_data_get_bool(saved.Get(), "visible"));
				obs_sceneitem_select(restoredItem, true);
			}
		};
		undo_s.add_action(QTStr("Undo.Add").arg(QString::fromStdString(sourceName)), undo, redo,
				  std::string(sourceUuidRaw), std::string(obs_data_get_json(wrapper.Get())));
	}

	// Open the native properties dialog so the user can select the display,
	// target window, or game capture method without exposing the full dock UI.
	if (mode == 3) {
		ui->simplePreviewButton->setChecked(true);
		ui->simpleCaptureSourceButton->setToolTip(QStringLiteral(
			"Для кадрирования удерживайте Alt и перетаскивайте границу источника в предпросмотре."));
	} else {
		ui->simpleCaptureSourceButton->setToolTip(QStringLiteral("Добавить выбранный тип захвата"));
	}
 	// The source target was already configured in the compact setup dialog.
#else
	// The quick source-type mapping uses the Windows capture source IDs.
	// On other platforms, fall back to OBS's native source picker.
	ui->actionAddSource->trigger();
#endif
}


void OBSBasic::SetSimpleRecorderMode(bool studioMode)
{
	// Capture the full dock layout (including plug-in and user-created docks)
	// before entering the recorder workspace. Reapply it when returning to OBS.
	if (studioMode != simpleStudioMode) {
		if (studioMode) {
			if (simpleStudioDockStateSaved)
				restoreState(simpleStudioDockState);
			if (simpleStudioWindowSize.isValid())
				resize(simpleStudioWindowSize);
		} else {
			simpleStudioDockState = saveState();
			simpleStudioDockStateSaved = true;
			simpleStudioWindowSize = size();
		}
		simpleStudioMode = studioMode;
	}

	if (!studioMode) {
		for (QDockWidget *dock : findChildren<QDockWidget *>())
			dock->hide();
	}

	const bool previewVisible = studioMode || ui->simplePreviewButton->isChecked();
	ui->canvasEditor->setVisible(previewVisible);
	ui->contextContainer->setVisible(previewVisible);
	ui->simplePreviewButton->setVisible(!studioMode);
	ui->menubar->setVisible(studioMode);
	ui->statusbar->setVisible(studioMode);

	ui->simpleStudioModeButton->setText(studioMode ? QStringLiteral("Простой режим")
								       : QStringLiteral("Студийный режим"));
	setWindowTitle(studioMode ? QStringLiteral("OBS Studio") : QStringLiteral("OBS Recorder"));

	// In recorder mode keep the main window close to a compact Bandicam-style
	// control panel. Showing preview expands the window without losing the
	// previous studio geometry.
	if (!studioMode)
		resize(960, previewVisible ? 760 : 180);

	auto syncAudioButton = [](QPushButton *button, uint32_t channel, const QString &label) {
		QSignalBlocker signalBlocker(button);
		OBSSourceAutoRelease source(obs_get_output_source(channel));
		if (!source) {
			button->setEnabled(false);
			button->setChecked(false);
			button->setText(label + QStringLiteral(" — не настроен"));
			return;
		}
		button->setEnabled(true);
		const bool enabled = !obs_source_muted(source.Get());
		button->setChecked(enabled);
		button->setText(enabled ? label : label + QStringLiteral(" — выкл."));
	};
	if (!studioMode) {
		syncAudioButton(ui->simpleMicButton, 3, QStringLiteral("Микрофон"));
		syncAudioButton(ui->simpleSystemAudioButton, 1, QStringLiteral("Системный звук"));
	}
}
