/******************************************************************************
    Bandicam-inspired Modern Fluent Recorder Workspace for OBS Studio.
******************************************************************************/

#include "BandicamRecorderWidget.hpp"
#include "BandicamRegionOverlay.hpp"
#include "OBSBasic.hpp"
#include "OBSApp.hpp"
#include "ui_OBSBasic.h"

#include <properties-view.hpp>
#include <qt-wrappers.hpp>
#include <utility/platform.hpp>

#include <util/platform.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QRunnable>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <ShObjIdl.h>
#endif

extern bool EncoderAvailable(const char *encoder);

namespace {

constexpr const char *kConfigSection = "BandicamUI";
constexpr uint32_t kDesktopAudioChannel = 1;
constexpr uint32_t kMicAudioChannel = 3;

enum class IconKind {
	Area,
	Screen,
	Game,
	Window,
	Webcam,
	Speaker,
	SpeakerMuted,
	Mic,
	MicMuted,
	Cursor,
	Screenshot,
	Record,
	Stop,
	Pause,
	Play,
	Folder,
	Settings,
	Preview,
	Home,
	Video,
	Library,
	General,
	Mini,
	FullObs,
	Back
};

QIcon MakeVectorIcon(IconKind kind, const QColor &color = QColor("#e6eaf2"), int size = 18)
{
	QPixmap pm(size * 2, size * 2);
	pm.setDevicePixelRatio(2.0);
	pm.fill(Qt::transparent);

	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);
	QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	const qreal s = size;
	switch (kind) {
	case IconKind::Area: {
		QPen dashed = pen;
		dashed.setStyle(Qt::DashLine);
		p.setPen(dashed);
		p.drawRect(QRectF(2.5, 3.0, s - 5.0, s - 6.0));
		p.setPen(pen);
		p.drawLine(QPointF(2.5, 6.0), QPointF(2.5, 3.0));
		p.drawLine(QPointF(2.5, 3.0), QPointF(5.5, 3.0));
		p.drawLine(QPointF(s - 2.5, s - 6.0), QPointF(s - 2.5, s - 3.0));
		p.drawLine(QPointF(s - 5.5, s - 3.0), QPointF(s - 2.5, s - 3.0));
		break;
	}
	case IconKind::Screen:
		p.drawRoundedRect(QRectF(2.0, 2.5, s - 4.0, s - 7.5), 2.0, 2.0);
		p.drawLine(QPointF(s * 0.5, s - 5.0), QPointF(s * 0.5, s - 2.2));
		p.drawLine(QPointF(s * 0.28, s - 2.2), QPointF(s * 0.72, s - 2.2));
		break;
	case IconKind::Game:
		p.drawRoundedRect(QRectF(2.0, 4.5, s - 4.0, s - 9.0), 4.0, 4.0);
		p.drawLine(QPointF(5.0, s * 0.5), QPointF(8.2, s * 0.5));
		p.drawLine(QPointF(6.6, s * 0.5 - 1.6), QPointF(6.6, s * 0.5 + 1.6));
		p.setBrush(color);
		p.setPen(Qt::NoPen);
		p.drawEllipse(QPointF(s - 6.8, s * 0.5 - 1.2), 1.1, 1.1);
		p.drawEllipse(QPointF(s - 4.8, s * 0.5 + 1.2), 1.1, 1.1);
		break;
	case IconKind::Window:
		p.drawRoundedRect(QRectF(2.2, 2.5, s - 4.4, s - 5.0), 2.2, 2.2);
		p.drawLine(QPointF(2.2, 6.2), QPointF(s - 2.2, 6.2));
		break;
	case IconKind::Webcam:
		p.drawEllipse(QPointF(s * 0.5, s * 0.44), s * 0.28, s * 0.28);
		p.drawEllipse(QPointF(s * 0.5, s * 0.44), s * 0.10, s * 0.10);
		p.drawLine(QPointF(s * 0.28, s - 2.2), QPointF(s * 0.72, s - 2.2));
		break;
	case IconKind::Speaker:
	case IconKind::SpeakerMuted: {
		QPainterPath sp;
		sp.moveTo(2.5, s * 0.38);
		sp.lineTo(5.8, s * 0.38);
		sp.lineTo(9.5, s * 0.20);
		sp.lineTo(9.5, s * 0.80);
		sp.lineTo(5.8, s * 0.62);
		sp.lineTo(2.5, s * 0.62);
		sp.closeSubpath();
		p.drawPath(sp);
		if (kind == IconKind::Speaker) {
			p.drawArc(QRectF(9.5, s * 0.28, 4.0, s * 0.44), -50 * 16, 100 * 16);
			p.drawArc(QRectF(11.0, s * 0.16, 5.2, s * 0.68), -50 * 16, 100 * 16);
		} else {
			p.setPen(QPen(QColor("#f87171"), 1.8, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(12.0, s * 0.34), QPointF(s - 2.0, s * 0.66));
			p.drawLine(QPointF(s - 2.0, s * 0.34), QPointF(12.0, s * 0.66));
		}
		break;
	}
	case IconKind::Mic:
	case IconKind::MicMuted:
		p.drawRoundedRect(QRectF(s * 0.34, 2.2, s * 0.32, s * 0.46), 3.0, 3.0);
		p.drawArc(QRectF(s * 0.20, s * 0.32, s * 0.60, s * 0.44), 180 * 16, 180 * 16);
		p.drawLine(QPointF(s * 0.5, s * 0.76), QPointF(s * 0.5, s - 2.2));
		p.drawLine(QPointF(s * 0.34, s - 2.2), QPointF(s * 0.66, s - 2.2));
		if (kind == IconKind::MicMuted) {
			p.setPen(QPen(QColor("#f87171"), 1.8, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(3.0, 3.0), QPointF(s - 3.0, s - 3.0));
		}
		break;
	case IconKind::Cursor: {
		QPainterPath cur;
		cur.moveTo(4.5, 2.5);
		cur.lineTo(4.5, s - 3.5);
		cur.lineTo(8.2, s - 7.0);
		cur.lineTo(11.2, s - 2.5);
		cur.lineTo(13.2, s - 3.6);
		cur.lineTo(10.3, s - 8.2);
		cur.lineTo(14.5, s - 8.2);
		cur.closeSubpath();
		p.drawPath(cur);
		break;
	}
	case IconKind::Screenshot:
		p.drawRoundedRect(QRectF(2.0, 4.5, s - 4.0, s - 7.5), 2.2, 2.2);
		p.drawLine(QPointF(6.0, 4.5), QPointF(7.2, 2.6));
		p.drawLine(QPointF(7.2, 2.6), QPointF(s - 7.2, 2.6));
		p.drawLine(QPointF(s - 7.2, 2.6), QPointF(s - 6.0, 4.5));
		p.drawEllipse(QPointF(s * 0.5, s * 0.58), 2.7, 2.7);
		break;
	case IconKind::Record:
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.32, s * 0.32);
		break;
	case IconKind::Stop:
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(QRectF(s * 0.24, s * 0.24, s * 0.52, s * 0.52), 2.0, 2.0);
		break;
	case IconKind::Pause:
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(QRectF(s * 0.24, s * 0.22, s * 0.18, s * 0.56), 1.2, 1.2);
		p.drawRoundedRect(QRectF(s * 0.58, s * 0.22, s * 0.18, s * 0.56), 1.2, 1.2);
		break;
	case IconKind::Play: {
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		QPolygonF tri;
		tri << QPointF(s * 0.30, s * 0.22) << QPointF(s * 0.30, s * 0.78) << QPointF(s * 0.78, s * 0.50);
		p.drawPolygon(tri);
		break;
	}
	case IconKind::Folder: {
		QPainterPath fp;
		fp.moveTo(2.0, 4.0);
		fp.lineTo(7.2, 4.0);
		fp.lineTo(9.0, 6.0);
		fp.lineTo(s - 2.0, 6.0);
		fp.lineTo(s - 2.0, s - 3.0);
		fp.lineTo(2.0, s - 3.0);
		fp.closeSubpath();
		p.drawPath(fp);
		break;
	}
	case IconKind::Settings:
	case IconKind::General:
		p.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.22, s * 0.22);
		for (int i = 0; i < 6; ++i) {
			const double a = i * (3.14159265 / 3.0);
			p.drawLine(QPointF(s * 0.5 + std::cos(a) * s * 0.27, s * 0.5 + std::sin(a) * s * 0.27),
				   QPointF(s * 0.5 + std::cos(a) * s * 0.40, s * 0.5 + std::sin(a) * s * 0.40));
		}
		break;
	case IconKind::Preview:
		p.drawEllipse(QPointF(s * 0.5, s * 0.5), 2.3, 2.3);
		p.drawArc(QRectF(2.0, s * 0.22, s - 4.0, s * 0.56), 0, 180 * 16);
		p.drawArc(QRectF(2.0, s * 0.22, s - 4.0, s * 0.56), 180 * 16, 180 * 16);
		break;
	case IconKind::Home: {
		QPainterPath hp;
		hp.moveTo(2.5, s * 0.50);
		hp.lineTo(s * 0.50, 2.5);
		hp.lineTo(s - 2.5, s * 0.50);
		p.drawPath(hp);
		p.drawRect(QRectF(4.5, s * 0.50, s - 9.0, s * 0.36));
		break;
	}
	case IconKind::Video:
		p.drawRoundedRect(QRectF(2.0, 4.0, s - 6.5, s - 8.0), 2.2, 2.2);
		p.drawLine(QPointF(s - 4.5, s * 0.42), QPointF(s - 1.8, s * 0.30));
		p.drawLine(QPointF(s - 1.8, s * 0.30), QPointF(s - 1.8, s * 0.70));
		p.drawLine(QPointF(s - 1.8, s * 0.70), QPointF(s - 4.5, s * 0.58));
		break;
	case IconKind::Library:
		p.drawRoundedRect(QRectF(2.2, 3.0, s - 4.4, s - 6.0), 2.0, 2.0);
		p.drawLine(QPointF(5.5, 3.0), QPointF(5.5, s - 3.0));
		p.drawLine(QPointF(s - 5.5, 3.0), QPointF(s - 5.5, s - 3.0));
		break;
	case IconKind::Mini:
		p.drawRoundedRect(QRectF(2.2, 3.5, s - 4.4, 5.0), 1.5, 1.5);
		p.drawLine(QPointF(s * 0.32, s - 4.0), QPointF(s * 0.50, s - 6.2));
		p.drawLine(QPointF(s * 0.50, s - 6.2), QPointF(s * 0.68, s - 4.0));
		break;
	case IconKind::FullObs:
		p.drawRoundedRect(QRectF(2.2, 2.5, s - 4.4, s - 5.0), 2.0, 2.0);
		p.drawLine(QPointF(2.2, s * 0.62), QPointF(s - 2.2, s * 0.62));
		p.drawLine(QPointF(s * 0.50, s * 0.62), QPointF(s * 0.50, s - 2.5));
		break;
	case IconKind::Back:
		p.drawLine(QPointF(s - 3.0, s * 0.50), QPointF(3.5, s * 0.50));
		p.drawLine(QPointF(7.5, s * 0.25), QPointF(3.5, s * 0.50));
		p.drawLine(QPointF(7.5, s * 0.75), QPointF(3.5, s * 0.50));
		break;
	}

	return QIcon(pm);
}

QThreadPool &BandicamThumbnailPool()
{
	static QThreadPool pool;
	pool.setMaxThreadCount(2);
	return pool;
}

QImage CreateMediaPlaceholder(const QString &extension, bool isImage)
{
	QImage image(320, 180, QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor("#1c2027"));

	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor("#29303b"));
	painter.drawRoundedRect(QRectF(8, 8, 304, 164), 10, 10);

	if (isImage) {
		painter.setBrush(QColor("#3c495c"));
		painter.drawRoundedRect(QRectF(118, 48, 84, 62), 8, 8);
		painter.setBrush(QColor("#93c5fd"));
		painter.drawEllipse(QPointF(140, 66), 7, 7);
		QPolygonF mtn;
		mtn << QPointF(126, 102) << QPointF(152, 72) << QPointF(172, 92) << QPointF(182, 82)
		    << QPointF(196, 102);
		painter.setBrush(QColor("#cbd5e1"));
		painter.drawPolygon(mtn);
	} else {
		painter.setBrush(QColor("#3d4a5c"));
		painter.drawEllipse(QRectF(130, 50, 60, 60));
		QPolygonF playTri;
		playTri << QPointF(152, 64) << QPointF(152, 96) << QPointF(179, 80);
		painter.setBrush(QColor("#f8fafc"));
		painter.drawPolygon(playTri);
	}

	QFont font = painter.font();
	font.setBold(true);
	font.setPointSize(10);
	painter.setFont(font);
	painter.setPen(QColor("#9aa4b4"));
	painter.drawText(QRect(16, 138, 288, 22), Qt::AlignRight | Qt::AlignVCenter, extension.toUpper());
	return image;
}

QImage LoadMediaThumbnail(const QString &filePath, bool isImage)
{
	if (isImage) {
		QImageReader reader(filePath);
		reader.setAutoTransform(true);
		const QSize orig = reader.size();
		if (orig.isValid()) {
			QSize scaled = orig.scaled(320, 180, Qt::KeepAspectRatio);
			reader.setScaledSize(scaled);
		}
		QImage img = reader.read();
		if (!img.isNull())
			return img;
	}

#ifdef _WIN32
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
		result = imageFactory->GetImage(targetSize, SIIGBF_THUMBNAILONLY, &thumbnail);
		if (SUCCEEDED(result) && thumbnail) {
			BITMAP bitmap = {};
			if (GetObjectW(thumbnail, static_cast<int>(sizeof(bitmap)), &bitmap) ==
				    static_cast<int>(sizeof(bitmap)) &&
			    bitmap.bmWidth > 0 && bitmap.bmHeight > 0) {
				BITMAPINFO info = {};
				info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				info.bmiHeader.biWidth = bitmap.bmWidth;
				info.bmiHeader.biHeight = -bitmap.bmHeight;
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				info.bmiHeader.biCompression = BI_RGB;

				image = QImage(bitmap.bmWidth, bitmap.bmHeight, QImage::Format_RGB32);
				HDC dc = GetDC(nullptr);
				if (!dc || GetDIBits(dc, thumbnail, 0, static_cast<UINT>(bitmap.bmHeight), image.bits(),
						     &info, DIB_RGB_COLORS) != bitmap.bmHeight) {
					image = QImage();
				}
				if (dc)
					ReleaseDC(nullptr, dc);
			}
			DeleteObject(thumbnail);
		}
		imageFactory->Release();
	}

	if (uninitializeCom)
		CoUninitialize();
	return image;
#else
	return QImage();
#endif
}

bool IsInputRegistered(const char *unversionedId)
{
	if (!unversionedId)
		return false;
	for (size_t index = 0;; ++index) {
		const char *registeredId = nullptr;
		if (!obs_enum_input_types2(index, nullptr, &registeredId))
			return false;
		if (registeredId && strcmp(registeredId, unversionedId) == 0)
			return true;
	}
}

bool IsCaptureSourceId(const char *unversionedId)
{
	if (!unversionedId)
		return false;
	return strcmp(unversionedId, "monitor_capture") == 0 || strcmp(unversionedId, "game_capture") == 0 ||
	       strcmp(unversionedId, "window_capture") == 0 || strcmp(unversionedId, "display_capture") == 0;
}

bool IsWebcamSourceId(const char *unversionedId)
{
	if (!unversionedId)
		return false;
	return strcmp(unversionedId, "dshow_input") == 0 || strcmp(unversionedId, "v4l2_input") == 0 ||
	       strcmp(unversionedId, "av_capture_input") == 0;
}

struct AddSceneItemData {
	obs_source_t *source = nullptr;
	obs_sceneitem_t *sceneItem = nullptr;
};

void AddSceneItemAtomic(void *data, obs_scene_t *scene)
{
	auto *d = static_cast<AddSceneItemData *>(data);
	d->sceneItem = obs_scene_add(scene, d->source);
}

bool SelectSingleSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *target = static_cast<obs_sceneitem_t *>(param);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, SelectSingleSceneItem, param);
	obs_sceneitem_select(item, item == target);
	return true;
}

QString FormatBytesCompact(uint64_t bytes)
{
	if (bytes >= 1024ULL * 1024ULL * 1024ULL)
		return QStringLiteral("%1 ГБ").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
	if (bytes >= 1024ULL * 1024ULL)
		return QStringLiteral("%1 МБ").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
	return QStringLiteral("%1 КБ").arg(bytes / 1024.0, 0, 'f', 0);
}

#ifdef _WIN32
struct WinMonitorMatch {
	std::string deviceId;
	std::string deviceName;
	RECT rect{};
	bool found = false;
};

WinMonitorMatch FindWindowsMonitorForPhysicalPoint(int physX, int physY)
{
	WinMonitorMatch out;
	POINT pt{physX, physY};
	HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
	if (!hMon)
		return out;

	MONITORINFOEXA mi{};
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoA(hMon, reinterpret_cast<LPMONITORINFO>(&mi)))
		return out;

	out.rect = mi.rcMonitor;
	out.deviceName = mi.szDevice;
	out.found = true;

	DISPLAY_DEVICEA dev{};
	dev.cb = sizeof(dev);
	if (EnumDisplayDevicesA(mi.szDevice, 0, &dev, EDD_GET_DEVICE_INTERFACE_NAME) && dev.DeviceID[0] != '\0') {
		out.deviceId = dev.DeviceID;
	} else {
		out.deviceId = mi.szDevice;
	}
	return out;
}
#endif

} // namespace

// ============================================================================
// BandicamAudioMeter: Compact real-time VU level bar powered by obs_volmeter_t
// ============================================================================

BandicamAudioMeter::BandicamAudioMeter(QWidget *parent) : QWidget(parent)
{
	setFixedSize(46, 8);
	decayTimer = new QTimer(this);
	decayTimer->setInterval(45);
	connect(decayTimer, &QTimer::timeout, this, [this]() {
		const float target = (mutedState || !configuredState) ? 0.0f : peakNormalized.load();
		if (target >= displayLevel)
			displayLevel = target;
		else
			displayLevel = std::max(0.0f, displayLevel - 0.08f);
		update();
	});
	decayTimer->start();
}

BandicamAudioMeter::~BandicamAudioMeter()
{
	DetachSource();
}

void BandicamAudioMeter::VolMeterCallback(void *data, const float[MAX_AUDIO_CHANNELS],
					  const float peak[MAX_AUDIO_CHANNELS], const float[MAX_AUDIO_CHANNELS])
{
	auto *self = static_cast<BandicamAudioMeter *>(data);
	float maxDb = -60.0f;
	for (int i = 0; i < 2; ++i) {
		if (std::isfinite(peak[i]) && peak[i] > maxDb)
			maxDb = peak[i];
	}
	const float norm = std::clamp((maxDb + 60.0f) / 60.0f, 0.0f, 1.0f);
	self->peakNormalized.store(norm);
}

void BandicamAudioMeter::AttachSource(obs_source_t *source)
{
	DetachSource();
	if (!source) {
		configuredState = false;
		peakNormalized.store(0.0f);
		update();
		return;
	}

	configuredState = true;
	volmeter = obs_volmeter_create(OBS_FADER_LOG);
	if (volmeter) {
		obs_volmeter_add_callback(volmeter, VolMeterCallback, this);
		obs_volmeter_attach_source(volmeter, source);
	}
	update();
}

void BandicamAudioMeter::DetachSource()
{
	if (volmeter) {
		obs_volmeter_remove_callback(volmeter, VolMeterCallback, this);
		obs_volmeter_detach_source(volmeter);
		obs_volmeter_destroy(volmeter);
		volmeter = nullptr;
	}
	peakNormalized.store(0.0f);
}

void BandicamAudioMeter::SetMuted(bool muted)
{
	mutedState = muted;
	update();
}

void BandicamAudioMeter::SetConfigured(bool configured)
{
	configuredState = configured;
	update();
}

void BandicamAudioMeter::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	const QRectF bg(0, 1, width(), height() - 2);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor("#14171d"));
	p.drawRoundedRect(bg, 3.0, 3.0);

	if (!configuredState || mutedState || displayLevel <= 0.01f)
		return;

	const qreal fillW = std::max<qreal>(3.0, bg.width() * displayLevel);
	QColor barColor("#22c55e");
	if (displayLevel > 0.88f)
		barColor = QColor("#ef4444");
	else if (displayLevel > 0.70f)
		barColor = QColor("#f59e0b");

	p.setBrush(barColor);
	p.drawRoundedRect(QRectF(bg.left(), bg.top(), fillW, bg.height()), 3.0, 3.0);
}

// ============================================================================
// BandicamRecorderWidget
// ============================================================================

BandicamRecorderWidget::BandicamRecorderWidget(OBSBasic *mainWindow, QWidget *parent)
	: QWidget(parent),
	  main(mainWindow)
{
	setObjectName(QStringLiteral("bandicamRecorderRoot"));
	BuildUi();

	statusTimer = new QTimer(this);
	statusTimer->setInterval(500);
	connect(statusTimer, &QTimer::timeout, this, [this]() {
		UpdateRecordingUi();
		UpdateFooterStatus();
	});

	audioSyncTimer = new QTimer(this);
	audioSyncTimer->setInterval(1200);
	connect(audioSyncTimer, &QTimer::timeout, this, [this]() {
		if (initialized && !shuttingDown) {
			SyncAudioControls();
			UpdateWebcamButtonState();
			UpdateCursorButtonState();
			if (!main->RecordingActive())
				UpdateFooterStatus();
		}
	});

	toastTimer = new QTimer(this);
	toastTimer->setSingleShot(true);
	connect(toastTimer, &QTimer::timeout, this, [this]() {
		if (footerToastLabel) {
			footerToastLabel->clear();
			footerToastLabel->setVisible(false);
		}
	});
}

BandicamRecorderWidget::~BandicamRecorderWidget()
{
	PrepareShutdown();
}

void BandicamRecorderWidget::PrepareShutdown()
{
	if (shuttingDown)
		return;
	shuttingDown = true;

	if (statusTimer)
		statusTimer->stop();
	if (audioSyncTimer)
		audioSyncTimer->stop();

	if (sysAudioMeter)
		sysAudioMeter->DetachSource();
	if (micAudioMeter)
		micAudioMeter->DetachSource();
	attachedSysSource = nullptr;
	attachedMicSource = nullptr;

	if (regionSelector) {
		regionSelector->hide();
		regionSelector->deleteLater();
		regionSelector = nullptr;
	}
	if (regionFrame) {
		regionFrame->hide();
		regionFrame->deleteLater();
		regionFrame = nullptr;
	}

	BandicamThumbnailPool().clear();
	BandicamThumbnailPool().waitForDone();
}

void BandicamRecorderWidget::BuildUi()
{
	setStyleSheet(QStringLiteral(
		"QWidget#bandicamRecorderRoot { background: #181a1f; color: #eef1f6; }"
		"QFrame#bandicamTopBar { background: #1f2229; border: 1px solid #2e333e; border-radius: 10px; }"
		"QFrame#bandicamStudioStrip { background: #1e2128; border: 1px solid #2e333e; border-radius: 7px; }"
		"QFrame#bandicamSidebar { background: #1b1e24; border: 1px solid #2b303a; border-radius: 10px; }"
		"QFrame#bandicamContentFrame { background: #1c1f26; border: 1px solid #2b303a; border-radius: 10px; }"
		"QFrame#bandicamCard { background: #22262f; border: 1px solid #323846; border-radius: 10px; }"
		"QFrame#bandicamFooter { background: #1b1e24; border: 1px solid #2b303a; border-radius: 8px; }"
		"QLabel { color: #e8ecf4; background: transparent; }"
		"QLabel#mutedLabel { color: #96a0b2; font-size: 12px; }"
		"QLabel#sectionTitle { color: #f4f7fb; font-size: 15px; font-weight: 700; }"
		"QLabel#subHeading { color: #b7c0d0; font-size: 12px; font-weight: 700; }"
		"QLabel#timerPill { background: #262b35; border: 1px solid #384050; border-radius: 7px; "
		"color: #d6dce7; font-size: 12px; font-weight: 700; padding: 5px 10px; }"
		"QLabel#timerPill[recState=\"recording\"] { background: #3b1c22; border-color: #c93b49; color: #ff8590; }"
		"QLabel#timerPill[recState=\"paused\"] { background: #3b2f19; border-color: #d99326; color: #fcd34d; }"
		"QPushButton { background: #2a2f3a; color: #eef1f6; border: 1px solid #3d4454; border-radius: 7px; "
		"padding: 6px 11px; font-size: 12px; font-weight: 600; }"
		"QPushButton:hover { background: #343b49; border-color: #505a6e; }"
		"QPushButton:pressed { background: #232730; }"
		"QPushButton:disabled { color: #6c7484; background: #22252d; border-color: #2f343f; }"
		"QPushButton[role=\"modeBtn\"] { padding: 6px 10px; background: #252932; border-color: #353b48; }"
		"QPushButton[role=\"modeBtn\"]:checked { background: #234573; border-color: #4f86d6; color: #ffffff; }"
		"QPushButton[role=\"mediaToggle\"] { padding: 5px 9px; background: #252932; border-color: #353b48; }"
		"QPushButton[role=\"mediaToggle\"]:checked { background: #1d4a39; border-color: #34a076; color: #eefcf6; }"
		"QPushButton[role=\"navBtn\"] { text-align: left; padding: 9px 12px; background: transparent; "
		"border: 1px solid transparent; border-radius: 8px; color: #b8c1d1; font-size: 13px; }"
		"QPushButton[role=\"navBtn\"]:hover { background: #262b35; color: #f2f5fa; }"
		"QPushButton[role=\"navBtn\"]:checked { background: #253852; border-color: #3c659c; color: #ffffff; }"
		"QPushButton[role=\"modeCard\"] { text-align: left; padding: 14px; background: #232730; "
		"border: 1px solid #343b49; border-radius: 10px; font-size: 13px; }"
		"QPushButton[role=\"modeCard\"]:hover { background: #2b313e; border-color: #5273a8; }"
		"QPushButton[role=\"modeCard\"]:checked { background: #1f334d; border: 2px solid #4f8ef7; }"
		"QPushButton#bandicamRecordBtn { background: #d93645; border: 1px solid #f0606d; color: #ffffff; "
		"font-weight: 700; font-size: 13px; padding: 7px 14px; min-width: 112px; }"
		"QPushButton#bandicamRecordBtn:hover { background: #ea4554; }"
		"QPushButton#bandicamRecordBtn[recording=\"true\"] { background: #b91c1c; border-color: #f87171; }"
		"QPushButton#bandicamPauseBtn { background: #3b3324; border: 1px solid #8a6d36; color: #fde68a; }"
		"QPushButton#bandicamPauseBtn:hover { background: #4c412d; }"
		"QPushButton#bandicamShotBtn { background: #283548; border: 1px solid #415d82; color: #dbeafe; }"
		"QPushButton#bandicamShotBtn:hover { background: #32445e; }"
		"QPushButton#dangerBtn { background: #4a2328; border-color: #7d363f; color: #fecaca; }"
		"QPushButton#dangerBtn:hover { background: #632b32; }"
		"QComboBox, QLineEdit, QSpinBox { background: #252932; color: #eef1f6; border: 1px solid #3c4353; "
		"border-radius: 6px; padding: 5px 9px; min-height: 20px; }"
		"QComboBox:hover, QLineEdit:hover, QSpinBox:hover { border-color: #556178; }"
		"QComboBox QAbstractItemView { background: #22262f; color: #eef1f6; selection-background-color: #2f4d78; "
		"border: 1px solid #3f4758; }"
		"QCheckBox { color: #e2e7f0; spacing: 8px; font-size: 12px; }"
		"QListWidget { background: #15181d; border: 1px solid #2e3440; border-radius: 8px; padding: 6px; }"
		"QListWidget::item { background: #22262f; border: 1px solid #323846; border-radius: 8px; "
		"padding: 6px; color: #e8ecf4; }"
		"QListWidget::item:hover { background: #2b313d; border-color: #4a566b; }"
		"QListWidget::item:selected { background: #263d5c; border-color: #5689d6; }"));

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 4);
	root->setSpacing(6);

	topBarWidget = BuildTopBar();
	studioStripWidget = BuildStudioStrip();
	bodyWidget = BuildMainBody();
	footerWidget = BuildFooterBar();

	root->addWidget(topBarWidget);
	root->addWidget(studioStripWidget);
	root->addWidget(bodyWidget, 1);
	root->addWidget(footerWidget);

	studioStripWidget->setVisible(false);
}

QWidget *BandicamRecorderWidget::BuildTopBar()
{
	auto *bar = new QFrame(this);
	bar->setObjectName(QStringLiteral("bandicamTopBar"));
	bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto *layout = new QHBoxLayout(bar);
	layout->setContentsMargins(10, 8, 10, 8);
	layout->setSpacing(6);

	auto makeModeBtn = [this, bar](const QString &text, IconKind icon, CaptureMode mode, const QString &tip) {
		auto *btn = new QPushButton(MakeVectorIcon(icon), text, bar);
		btn->setProperty("role", QStringLiteral("modeBtn"));
		btn->setCheckable(true);
		btn->setToolTip(tip);
		connect(btn, &QPushButton::clicked, this, [this, mode]() { SelectCaptureMode(mode, true); });
		return btn;
	};

	modeAreaBtn = makeModeBtn(QStringLiteral("Область"), IconKind::Area, CaptureMode::Area,
				  QStringLiteral("Выделить прямоугольную область экрана (как в Bandicam)"));
	modeScreenBtn = makeModeBtn(QStringLiteral("Экран"), IconKind::Screen, CaptureMode::Screen,
				    QStringLiteral("Запись всего экрана"));
	modeGameBtn = makeModeBtn(QStringLiteral("Игра"), IconKind::Game, CaptureMode::Game,
				  QStringLiteral("Игровой режим (DirectX / Vulkan / OpenGL)"));
	modeWindowBtn = makeModeBtn(QStringLiteral("Окно"), IconKind::Window, CaptureMode::Window,
				    QStringLiteral("Запись конкретного окна приложения"));

	layout->addWidget(modeAreaBtn);
	layout->addWidget(modeScreenBtn);
	layout->addWidget(modeGameBtn);
	layout->addWidget(modeWindowBtn);

	auto addDivider = [bar, layout]() {
		auto *div = new QFrame(bar);
		div->setFrameShape(QFrame::VLine);
		div->setFixedWidth(1);
		div->setStyleSheet(QStringLiteral("background: #323846; border: none;"));
		layout->addWidget(div);
	};

	addDivider();

	// Webcam PiP toggle
	webcamBtn = new QPushButton(MakeVectorIcon(IconKind::Webcam), QStringLiteral("Камера"), bar);
	webcamBtn->setProperty("role", QStringLiteral("mediaToggle"));
	webcamBtn->setCheckable(true);
	webcamBtn->setToolTip(QStringLiteral("Добавить или скрыть веб-камеру поверх видео (PiP)"));
	connect(webcamBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::ToggleWebcamOverlay);
	layout->addWidget(webcamBtn);

	// System audio button + live VU bar
	auto *sysWrap = new QWidget(bar);
	auto *sysLay = new QVBoxLayout(sysWrap);
	sysLay->setContentsMargins(0, 0, 0, 0);
	sysLay->setSpacing(2);
	sysAudioBtn = new QPushButton(MakeVectorIcon(IconKind::Speaker), QStringLiteral("Звук"), sysWrap);
	sysAudioBtn->setProperty("role", QStringLiteral("mediaToggle"));
	sysAudioBtn->setCheckable(true);
	sysAudioBtn->setToolTip(QStringLiteral("Включить или выключить звук системы (ПК)"));
	connect(sysAudioBtn, &QPushButton::clicked, this,
		[this]() { ToggleAudioChannel(kDesktopAudioChannel, QStringLiteral("Звук системы")); });
	sysAudioMeter = new BandicamAudioMeter(sysWrap);
	sysLay->addWidget(sysAudioBtn);
	sysLay->addWidget(sysAudioMeter, 0, Qt::AlignHCenter);
	layout->addWidget(sysWrap);

	// Mic button + live VU bar
	auto *micWrap = new QWidget(bar);
	auto *micLay = new QVBoxLayout(micWrap);
	micLay->setContentsMargins(0, 0, 0, 0);
	micLay->setSpacing(2);
	micAudioBtn = new QPushButton(MakeVectorIcon(IconKind::Mic), QStringLiteral("Микр."), micWrap);
	micAudioBtn->setProperty("role", QStringLiteral("mediaToggle"));
	micAudioBtn->setCheckable(true);
	micAudioBtn->setToolTip(QStringLiteral("Включить или выключить микрофон"));
	connect(micAudioBtn, &QPushButton::clicked, this,
		[this]() { ToggleAudioChannel(kMicAudioChannel, QStringLiteral("Микрофон")); });
	micAudioMeter = new BandicamAudioMeter(micWrap);
	micLay->addWidget(micAudioBtn);
	micLay->addWidget(micAudioMeter, 0, Qt::AlignHCenter);
	layout->addWidget(micWrap);

	// Mouse cursor toggle
	cursorBtn = new QPushButton(MakeVectorIcon(IconKind::Cursor), QString(), bar);
	cursorBtn->setProperty("role", QStringLiteral("mediaToggle"));
	cursorBtn->setCheckable(true);
	cursorBtn->setChecked(true);
	cursorBtn->setToolTip(QStringLiteral("Показывать курсор мыши на записи"));
	connect(cursorBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::ToggleCursorCapture);
	layout->addWidget(cursorBtn);

	layout->addStretch(1);

	// Instant Screenshot button
	screenshotBtn = new QPushButton(MakeVectorIcon(IconKind::Screenshot, QColor("#dbeafe")), QString(), bar);
	screenshotBtn->setObjectName(QStringLiteral("bandicamShotBtn"));
	screenshotBtn->setToolTip(QStringLiteral("Сделать скриншот (сохраняется в папку записей)"));
	connect(screenshotBtn, &QPushButton::clicked, this, [this]() {
		if (!EnsureCaptureSourceBeforeRecording())
			return;
		main->Screenshot();
	});
	layout->addWidget(screenshotBtn);

	// Timer & live file size pill
	timerPillLabel = new QLabel(QStringLiteral("00:00:00"), bar);
	timerPillLabel->setObjectName(QStringLiteral("timerPill"));
	timerPillLabel->setAlignment(Qt::AlignCenter);
	timerPillLabel->setMinimumWidth(86);
	layout->addWidget(timerPillLabel);

	// Pause button
	pauseBtn = new QPushButton(MakeVectorIcon(IconKind::Pause, QColor("#fde68a")), QStringLiteral("Пауза"), bar);
	pauseBtn->setObjectName(QStringLiteral("bandicamPauseBtn"));
	pauseBtn->setToolTip(QStringLiteral("Приостановить или продолжить запись"));
	pauseBtn->setVisible(false);
	connect(pauseBtn, &QPushButton::clicked, this, [this]() { main->RecordPauseToggled(); });
	layout->addWidget(pauseBtn);

	// Big Bandicam REC / STOP button
	recordBtn = new QPushButton(MakeVectorIcon(IconKind::Record, Qt::white), QStringLiteral("REC"), bar);
	recordBtn->setObjectName(QStringLiteral("bandicamRecordBtn"));
	recordBtn->setToolTip(QStringLiteral("Начать или остановить запись видео"));
	connect(recordBtn, &QPushButton::clicked, this, [this]() {
		if (!main->RecordingActive() && !EnsureCaptureSourceBeforeRecording())
			return;
		main->RecordActionTriggered();
	});
	layout->addWidget(recordBtn);

	addDivider();

	// Mini bar collapse button
	miniToggleBtn = new QPushButton(MakeVectorIcon(IconKind::Mini), QString(), bar);
	miniToggleBtn->setCheckable(true);
	miniToggleBtn->setToolTip(QStringLiteral("Свернуть в компактную мини-панель"));
	connect(miniToggleBtn, &QPushButton::clicked, this, [this](bool checked) {
		SetViewMode(checked ? ViewMode::Mini : ViewMode::Bandicam);
	});
	layout->addWidget(miniToggleBtn);

	// Switch to Full OBS Studio
	fullObsBtn = new QPushButton(MakeVectorIcon(IconKind::FullObs), QStringLiteral("OBS"), bar);
	fullObsBtn->setToolTip(QStringLiteral("Переключиться на полный интерфейс OBS Studio (сцены, доки, микшер)"));
	connect(fullObsBtn, &QPushButton::clicked, this, [this]() { SetViewMode(ViewMode::Studio); });
	layout->addWidget(fullObsBtn);

	return bar;
}

QWidget *BandicamRecorderWidget::BuildStudioStrip()
{
	auto *strip = new QFrame(this);
	strip->setObjectName(QStringLiteral("bandicamStudioStrip"));
	strip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto *layout = new QHBoxLayout(strip);
	layout->setContentsMargins(10, 4, 10, 4);
	layout->setSpacing(8);

	studioReturnBtn = new QPushButton(MakeVectorIcon(IconKind::Back), QStringLiteral("Режим Bandicam"), strip);
	studioReturnBtn->setToolTip(QStringLiteral("Вернуться к простому интерфейсу записи"));
	connect(studioReturnBtn, &QPushButton::clicked, this, [this]() { SetViewMode(ViewMode::Bandicam); });
	layout->addWidget(studioReturnBtn);

	auto *hint = new QLabel(QStringLiteral("Полный интерфейс OBS Studio"), strip);
	hint->setObjectName(QStringLiteral("mutedLabel"));
	layout->addWidget(hint);

	layout->addStretch(1);

	studioStatusLabel = new QLabel(QStringLiteral("Готов к записи"), strip);
	studioStatusLabel->setObjectName(QStringLiteral("mutedLabel"));
	layout->addWidget(studioStatusLabel);

	studioShotBtn = new QPushButton(MakeVectorIcon(IconKind::Screenshot), QStringLiteral("Скриншот"), strip);
	connect(studioShotBtn, &QPushButton::clicked, this, [this]() { main->Screenshot(); });
	layout->addWidget(studioShotBtn);

	studioRecordBtn = new QPushButton(MakeVectorIcon(IconKind::Record, Qt::white), QStringLiteral("REC"), strip);
	studioRecordBtn->setObjectName(QStringLiteral("bandicamRecordBtn"));
	studioRecordBtn->setMinimumWidth(90);
	connect(studioRecordBtn, &QPushButton::clicked, this, [this]() { main->RecordActionTriggered(); });
	layout->addWidget(studioRecordBtn);

	return strip;
}

QWidget *BandicamRecorderWidget::BuildMainBody()
{
	auto *body = new QWidget(this);
	auto *layout = new QHBoxLayout(body);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);

	// Left Bandicam Navigation Sidebar
	auto *sidebar = new QFrame(body);
	sidebar->setObjectName(QStringLiteral("bandicamSidebar"));
	sidebar->setFixedWidth(158);
	auto *sideLay = new QVBoxLayout(sidebar);
	sideLay->setContentsMargins(8, 10, 8, 10);
	sideLay->setSpacing(4);

	auto *brand = new QLabel(QStringLiteral("OBS RECORDER"), sidebar);
	brand->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; letter-spacing: 1.2px; "
					    "color: #60a5fa; padding: 2px 8px 8px 8px;"));
	sideLay->addWidget(brand);

	auto *navGroup = new QButtonGroup(sidebar);
	navGroup->setExclusive(true);

	auto makeNav = [this, sidebar, navGroup, sideLay](const QString &label, IconKind icon, int pageIndex) {
		auto *btn = new QPushButton(MakeVectorIcon(icon), label, sidebar);
		btn->setProperty("role", QStringLiteral("navBtn"));
		btn->setCheckable(true);
		navGroup->addButton(btn, pageIndex);
		sideLay->addWidget(btn);
		connect(btn, &QPushButton::clicked, this, [this, pageIndex]() {
			if (pageStack)
				pageStack->setCurrentIndex(pageIndex);
			if (pageIndex == 1)
				LoadVideoPageFromConfig();
			else if (pageIndex == 2)
				RefreshRecordingsList();
			else if (pageIndex == 3)
				LoadGeneralPageFromConfig();
		});
		return btn;
	};

	navHomeBtn = makeNav(QStringLiteral("Главная"), IconKind::Home, 0);
	navVideoBtn = makeNav(QStringLiteral("Видео"), IconKind::Video, 1);
	navRecordingsBtn = makeNav(QStringLiteral("Записи"), IconKind::Library, 2);
	navGeneralBtn = makeNav(QStringLiteral("Основные"), IconKind::General, 3);
	navHomeBtn->setChecked(true);

	sideLay->addStretch(1);

	auto *div = new QFrame(sidebar);
	div->setFrameShape(QFrame::HLine);
	div->setFixedHeight(1);
	div->setStyleSheet(QStringLiteral("background: #2c313c; border: none;"));
	sideLay->addWidget(div);

	navPreviewBtn = new QPushButton(MakeVectorIcon(IconKind::Preview), QStringLiteral("Предпросмотр"), sidebar);
	navPreviewBtn->setProperty("role", QStringLiteral("navBtn"));
	navPreviewBtn->setCheckable(true);
	navPreviewBtn->setToolTip(QStringLiteral("Показать или скрыть живой предпросмотр записи"));
	connect(navPreviewBtn, &QPushButton::toggled, this, &BandicamRecorderWidget::TogglePreviewVisibility);
	sideLay->addWidget(navPreviewBtn);

	navFolderBtn = new QPushButton(MakeVectorIcon(IconKind::Folder), QStringLiteral("Открыть папку"), sidebar);
	navFolderBtn->setProperty("role", QStringLiteral("navBtn"));
	navFolderBtn->setToolTip(QStringLiteral("Открыть папку с сохранёнными видео и скриншотами"));
	connect(navFolderBtn, &QPushButton::clicked, this, [this]() {
		const QString dir = ResolveOutputDirectory();
		if (!dir.isEmpty() && QDir(dir).exists())
			QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
		else
			ShowToast(QStringLiteral("Папка сохранения ещё не существует"), true);
	});
	sideLay->addWidget(navFolderBtn);

	// Right Stacked Content Panel
	auto *contentFrame = new QFrame(body);
	contentFrame->setObjectName(QStringLiteral("bandicamContentFrame"));
	auto *contentLay = new QVBoxLayout(contentFrame);
	contentLay->setContentsMargins(14, 12, 14, 12);
	contentLay->setSpacing(0);

	pageStack = new QStackedWidget(contentFrame);
	pageStack->addWidget(BuildHomePage());
	pageStack->addWidget(BuildVideoPage());
	pageStack->addWidget(BuildRecordingsPage());
	pageStack->addWidget(BuildGeneralPage());
	contentLay->addWidget(pageStack);

	layout->addWidget(sidebar);
	layout->addWidget(contentFrame, 1);
	return body;
}

QWidget *BandicamRecorderWidget::BuildHomePage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	auto *header = new QLabel(QStringLiteral("Начало работы — выберите режим записи"), page);
	header->setObjectName(QStringLiteral("sectionTitle"));
	layout->addWidget(header);

	// 2x2 Bandicam mode cards
	auto *grid = new QGridLayout;
	grid->setSpacing(10);

	auto makeCard = [this, page](const QString &title, const QString &subtitle, IconKind icon, CaptureMode mode) {
		auto *btn = new QPushButton(MakeVectorIcon(icon, QColor("#93c5fd"), 22),
					    title + QStringLiteral("\n") + subtitle, page);
		btn->setProperty("role", QStringLiteral("modeCard"));
		btn->setCheckable(true);
		btn->setMinimumHeight(74);
		btn->setIconSize(QSize(24, 24));
		connect(btn, &QPushButton::clicked, this, [this, mode]() { SelectCaptureMode(mode, true); });
		return btn;
	};

	cardAreaBtn = makeCard(QStringLiteral("Прямоугольная область"),
			       QStringLiteral("Выделите любую часть экрана рамкой"), IconKind::Area, CaptureMode::Area);
	cardScreenBtn = makeCard(QStringLiteral("Полный экран"), QStringLiteral("Запись всего выбранного монитора"),
				 IconKind::Screen, CaptureMode::Screen);
	cardGameBtn = makeCard(QStringLiteral("Игровой режим"), QStringLiteral("Захват игр DirectX / Vulkan / OpenGL"),
			       IconKind::Game, CaptureMode::Game);
	cardWindowBtn = makeCard(QStringLiteral("Захват окна"), QStringLiteral("Запись одного окна приложения"),
				 IconKind::Window, CaptureMode::Window);

	grid->addWidget(cardAreaBtn, 0, 0);
	grid->addWidget(cardScreenBtn, 0, 1);
	grid->addWidget(cardGameBtn, 1, 0);
	grid->addWidget(cardWindowBtn, 1, 1);
	layout->addLayout(grid);

	// Active Capture Source Card
	auto *srcCard = new QFrame(page);
	srcCard->setObjectName(QStringLiteral("bandicamCard"));
	auto *srcLay = new QHBoxLayout(srcCard);
	srcLay->setContentsMargins(12, 10, 12, 10);
	srcLay->setSpacing(10);

	auto *srcInfoLay = new QVBoxLayout;
	srcInfoLay->setSpacing(2);
	activeSourceTitleLabel = new QLabel(QStringLiteral("Источник не выбран"), srcCard);
	activeSourceTitleLabel->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 13px; color: #f4f7fb;"));
	activeSourceDetailLabel = new QLabel(
		QStringLiteral("Выберите режим выше или нажмите REC — основной экран добавится автоматически."),
		srcCard);
	activeSourceDetailLabel->setObjectName(QStringLiteral("mutedLabel"));
	activeSourceDetailLabel->setWordWrap(true);
	srcInfoLay->addWidget(activeSourceTitleLabel);
	srcInfoLay->addWidget(activeSourceDetailLabel);
	srcLay->addLayout(srcInfoLay, 1);

	reselectRegionBtn = new QPushButton(MakeVectorIcon(IconKind::Area), QStringLiteral("Выбрать область"), srcCard);
	reselectRegionBtn->setVisible(false);
	connect(reselectRegionBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::StartRegionSelection);
	srcLay->addWidget(reselectRegionBtn);

	configureSourceBtn = new QPushButton(MakeVectorIcon(IconKind::Settings), QStringLiteral("Настроить"), srcCard);
	connect(configureSourceBtn, &QPushButton::clicked, this,
		&BandicamRecorderWidget::ConfigureCurrentPrimarySource);
	srcLay->addWidget(configureSourceBtn);

	clearSourceBtn = new QPushButton(QStringLiteral("Удалить"), srcCard);
	clearSourceBtn->setObjectName(QStringLiteral("dangerBtn"));
	connect(clearSourceBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::RemovePrimaryCaptureSource);
	srcLay->addWidget(clearSourceBtn);

	layout->addWidget(srcCard);

	// Quick summary & tips card
	auto *summaryCard = new QFrame(page);
	summaryCard->setObjectName(QStringLiteral("bandicamCard"));
	auto *sumLay = new QHBoxLayout(summaryCard);
	sumLay->setContentsMargins(12, 8, 12, 8);
	homeSummaryLabel = new QLabel(summaryCard);
	homeSummaryLabel->setObjectName(QStringLiteral("mutedLabel"));
	homeSummaryLabel->setWordWrap(true);
	sumLay->addWidget(homeSummaryLabel, 1);

	auto *quickVideoBtn = new QPushButton(QStringLiteral("Параметры видео →"), summaryCard);
	connect(quickVideoBtn, &QPushButton::clicked, this, [this]() {
		navVideoBtn->setChecked(true);
		pageStack->setCurrentIndex(1);
		LoadVideoPageFromConfig();
	});
	sumLay->addWidget(quickVideoBtn);
	layout->addWidget(summaryCard);

	layout->addStretch(1);
	return page;
}

QWidget *BandicamRecorderWidget::BuildVideoPage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	auto *header = new QLabel(QStringLiteral("Настройки видео и качества записи"), page);
	header->setObjectName(QStringLiteral("sectionTitle"));
	layout->addWidget(header);

	videoLockedBanner = new QLabel(QStringLiteral("Остановите текущую запись, чтобы изменить параметры видео."), page);
	videoLockedBanner->setStyleSheet(QStringLiteral(
		"background: #3b2d18; border: 1px solid #8a6d36; border-radius: 7px; color: #fde68a; "
		"padding: 7px 10px; font-weight: 600;"));
	videoLockedBanner->setVisible(false);
	layout->addWidget(videoLockedBanner);

	videoControlsContainer = new QFrame(page);
	videoControlsContainer->setObjectName(QStringLiteral("bandicamCard"));
	auto *form = new QFormLayout(videoControlsContainer);
	form->setContentsMargins(14, 12, 14, 12);
	form->setSpacing(9);
	form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	formatCombo = new QComboBox(videoControlsContainer);
	formatCombo->addItem(QStringLiteral("MP4 (Hybrid — надёжный, рекомендуется)"), QStringLiteral("hybrid_mp4"));
	formatCombo->addItem(QStringLiteral("MKV (устойчив к сбоям)"), QStringLiteral("mkv"));
	formatCombo->addItem(QStringLiteral("MP4 (стандартный)"), QStringLiteral("mp4"));
	formatCombo->addItem(QStringLiteral("MOV (Hybrid)"), QStringLiteral("hybrid_mov"));
	form->addRow(QStringLiteral("Формат видео:"), formatCombo);

	qualityCombo = new QComboBox(videoControlsContainer);
	qualityCombo->addItem(QStringLiteral("Высокое качество, средний размер (рекомендуется)"),
			      QStringLiteral("Small"));
	qualityCombo->addItem(QStringLiteral("Максимальное качество, большой размер"), QStringLiteral("HQ"));
	qualityCombo->addItem(QStringLiteral("Без потерь (огромный размер)"), QStringLiteral("Lossless"));
	qualityCombo->addItem(QStringLiteral("Как для стрима (пауза недоступна)"), QStringLiteral("Stream"));
	form->addRow(QStringLiteral("Качество записи:"), qualityCombo);

	encoderCombo = new QComboBox(videoControlsContainer);
	form->addRow(QStringLiteral("Кодировщик:"), encoderCombo);

	resolutionCombo = new QComboBox(videoControlsContainer);
	resolutionCombo->addItem(QStringLiteral("Как на экране (оригинал)"), QStringLiteral("native"));
	resolutionCombo->addItem(QStringLiteral("1920×1080 (Full HD)"), QStringLiteral("1920x1080"));
	resolutionCombo->addItem(QStringLiteral("1280×720 (HD)"), QStringLiteral("1280x720"));
	form->addRow(QStringLiteral("Разрешение:"), resolutionCombo);

	fpsCombo = new QComboBox(videoControlsContainer);
	fpsCombo->addItem(QStringLiteral("60 FPS (плавное видео / игры)"), QStringLiteral("60"));
	fpsCombo->addItem(QStringLiteral("30 FPS (обычная запись экрана)"), QStringLiteral("30"));
	fpsCombo->addItem(QStringLiteral("24 FPS (кино)"), QStringLiteral("24 NTSC"));
	form->addRow(QStringLiteral("Частота кадров:"), fpsCombo);

	videoCursorCheck = new QCheckBox(QStringLiteral("Показывать курсор мыши на записи"), videoControlsContainer);
	form->addRow(QString(), videoCursorCheck);

	autoRemuxCheck = new QCheckBox(QStringLiteral("Автоматически перепаковывать MKV в MP4 после остановки"),
				       videoControlsContainer);
	form->addRow(QString(), autoRemuxCheck);

	layout->addWidget(videoControlsContainer);

	auto *bottomRow = new QHBoxLayout;
	auto *allSettingsBtn = new QPushButton(MakeVectorIcon(IconKind::Settings),
					       QStringLiteral("Все настройки OBS (горячие клавиши, звук, битрейт)..."),
					       page);
	connect(allSettingsBtn, &QPushButton::clicked, this, [this]() {
		main->on_action_RecordingSettings_triggered();
		RefreshAfterSettingsChanged();
	});
	bottomRow->addWidget(allSettingsBtn);
	bottomRow->addStretch(1);
	layout->addLayout(bottomRow);
	layout->addStretch(1);

	auto applyHandler = [this]() {
		if (!syncingUi)
			ApplyVideoSettingsToConfig();
	};
	connect(formatCombo, &QComboBox::currentIndexChanged, this, applyHandler);
	connect(qualityCombo, &QComboBox::currentIndexChanged, this, applyHandler);
	connect(encoderCombo, &QComboBox::currentIndexChanged, this, applyHandler);
	connect(resolutionCombo, &QComboBox::currentIndexChanged, this, applyHandler);
	connect(fpsCombo, &QComboBox::currentIndexChanged, this, applyHandler);
	connect(autoRemuxCheck, &QCheckBox::toggled, this, applyHandler);
	connect(videoCursorCheck, &QCheckBox::toggled, this, [this](bool checked) {
		if (syncingUi)
			return;
		cursorBtn->setChecked(checked);
		ToggleCursorCapture();
	});

	return page;
}

QWidget *BandicamRecorderWidget::BuildRecordingsPage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);

	auto *topRow = new QHBoxLayout;
	auto *title = new QLabel(QStringLiteral("Видео и скриншоты"), page);
	title->setObjectName(QStringLiteral("sectionTitle"));
	topRow->addWidget(title);

	topRow->addSpacing(8);

	auto *filterGroup = new QButtonGroup(page);
	filterGroup->setExclusive(true);
	auto makeFilter = [this, page, filterGroup, topRow](const QString &label, int mode) {
		auto *btn = new QPushButton(label, page);
		btn->setProperty("role", QStringLiteral("modeBtn"));
		btn->setCheckable(true);
		filterGroup->addButton(btn, mode);
		topRow->addWidget(btn);
		connect(btn, &QPushButton::clicked, this, [this, mode]() {
			recordingsFilterMode = mode;
			RefreshRecordingsList();
		});
		return btn;
	};
	filterVideosBtn = makeFilter(QStringLiteral("Видео"), 0);
	filterShotsBtn = makeFilter(QStringLiteral("Скриншоты"), 1);
	filterAllBtn = makeFilter(QStringLiteral("Все"), 2);
	filterVideosBtn->setChecked(true);

	topRow->addStretch(1);

	recordingsCountLabel = new QLabel(page);
	recordingsCountLabel->setObjectName(QStringLiteral("mutedLabel"));
	topRow->addWidget(recordingsCountLabel);

	auto *refreshBtn = new QPushButton(QStringLiteral("Обновить"), page);
	connect(refreshBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::RefreshRecordingsList);
	topRow->addWidget(refreshBtn);

	layout->addLayout(topRow);

	recordingsList = new QListWidget(page);
	recordingsList->setViewMode(QListView::IconMode);
	recordingsList->setFlow(QListView::LeftToRight);
	recordingsList->setResizeMode(QListView::Adjust);
	recordingsList->setMovement(QListView::Static);
	recordingsList->setWrapping(true);
	recordingsList->setWordWrap(true);
	recordingsList->setSpacing(8);
	recordingsList->setIconSize(QSize(160, 90));
	recordingsList->setGridSize(QSize(184, 146));
	recordingsList->setSelectionMode(QAbstractItemView::SingleSelection);
	recordingsList->setUniformItemSizes(true);
	layout->addWidget(recordingsList, 1);

	recordingsEmptyLabel = new QLabel(page);
	recordingsEmptyLabel->setAlignment(Qt::AlignCenter);
	recordingsEmptyLabel->setObjectName(QStringLiteral("mutedLabel"));
	recordingsEmptyLabel->setWordWrap(true);
	recordingsEmptyLabel->setMinimumHeight(42);
	recordingsEmptyLabel->setVisible(false);
	layout->addWidget(recordingsEmptyLabel);

	auto *actions = new QHBoxLayout;
	actions->setSpacing(6);
	recOpenBtn = new QPushButton(MakeVectorIcon(IconKind::Play), QStringLiteral("Открыть"), page);
	recRevealBtn = new QPushButton(MakeVectorIcon(IconKind::Folder), QStringLiteral("В папке"), page);
	recRenameBtn = new QPushButton(QStringLiteral("Переименовать"), page);
	recCopyPathBtn = new QPushButton(QStringLiteral("Копировать путь"), page);
	recDeleteBtn = new QPushButton(QStringLiteral("Удалить"), page);
	recDeleteBtn->setObjectName(QStringLiteral("dangerBtn"));

	auto setActionsEnabled = [this](bool enabled) {
		recOpenBtn->setEnabled(enabled);
		recRevealBtn->setEnabled(enabled);
		recRenameBtn->setEnabled(enabled);
		recCopyPathBtn->setEnabled(enabled);
		recDeleteBtn->setEnabled(enabled);
	};
	setActionsEnabled(false);

	connect(recordingsList, &QListWidget::currentItemChanged, this,
		[setActionsEnabled](QListWidgetItem *curr, QListWidgetItem *) { setActionsEnabled(curr != nullptr); });
	connect(recordingsList, &QListWidget::itemActivated, this,
		[this](QListWidgetItem *) { OpenSelectedRecording(); });
	connect(recOpenBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::OpenSelectedRecording);
	connect(recRevealBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::RevealSelectedRecording);
	connect(recRenameBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::RenameSelectedRecording);
	connect(recCopyPathBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::CopySelectedRecordingPath);
	connect(recDeleteBtn, &QPushButton::clicked, this, &BandicamRecorderWidget::DeleteSelectedRecording);

	actions->addWidget(recOpenBtn);
	actions->addWidget(recRevealBtn);
	actions->addWidget(recRenameBtn);
	actions->addWidget(recCopyPathBtn);
	actions->addStretch(1);
	actions->addWidget(recDeleteBtn);
	layout->addLayout(actions);

	return page;
}

QWidget *BandicamRecorderWidget::BuildGeneralPage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	auto *header = new QLabel(QStringLiteral("Основные настройки"), page);
	header->setObjectName(QStringLiteral("sectionTitle"));
	layout->addWidget(header);

	// Output folder card
	auto *folderCard = new QFrame(page);
	folderCard->setObjectName(QStringLiteral("bandicamCard"));
	auto *folderLay = new QVBoxLayout(folderCard);
	folderLay->setContentsMargins(12, 10, 12, 10);
	folderLay->setSpacing(6);

	auto *folderTitle = new QLabel(QStringLiteral("Папка сохранения видео и скриншотов"), folderCard);
	folderTitle->setObjectName(QStringLiteral("subHeading"));
	folderLay->addWidget(folderTitle);

	auto *folderRow = new QHBoxLayout;
	outputFolderEdit = new QLineEdit(folderCard);
	outputFolderEdit->setReadOnly(true);
	auto *browseBtn = new QPushButton(QStringLiteral("Выбрать..."), folderCard);
	auto *openDirBtn = new QPushButton(QStringLiteral("Открыть"), folderCard);
	connect(browseBtn, &QPushButton::clicked, this, [this]() {
		const QString chosen = QFileDialog::getExistingDirectory(
			this, QStringLiteral("Выберите папку для сохранения записей"), ResolveOutputDirectory());
		if (chosen.isEmpty())
			return;
		const QString normalized = QDir::toNativeSeparators(chosen);
		config_set_string(main->Config(), "SimpleOutput", "FilePath", QT_TO_UTF8(normalized));
		config_set_string(main->Config(), "AdvOut", "RecFilePath", QT_TO_UTF8(normalized));
		if (main->Config())
			config_save_safe(main->Config(), "tmp", nullptr);
		outputFolderEdit->setText(normalized);
		UpdateFooterStatus();
		ShowToast(QStringLiteral("Папка сохранения обновлена"));
	});
	connect(openDirBtn, &QPushButton::clicked, this, [this]() {
		const QString dir = ResolveOutputDirectory();
		if (!dir.isEmpty() && QDir(dir).exists())
			QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
	});
	folderRow->addWidget(outputFolderEdit, 1);
	folderRow->addWidget(browseBtn);
	folderRow->addWidget(openDirBtn);
	folderLay->addLayout(folderRow);
	layout->addWidget(folderCard);

	// Window behavior card
	auto *behaviorCard = new QFrame(page);
	behaviorCard->setObjectName(QStringLiteral("bandicamCard"));
	auto *behLay = new QVBoxLayout(behaviorCard);
	behLay->setContentsMargins(12, 10, 12, 10);
	behLay->setSpacing(7);

	auto *behTitle = new QLabel(QStringLiteral("Окно и удобство записи"), behaviorCard);
	behTitle->setObjectName(QStringLiteral("subHeading"));
	behLay->addWidget(behTitle);

	hideFromCaptureCheck = new QCheckBox(
		QStringLiteral("Скрывать окно OBS Recorder от захвата экрана (чтобы не было «зеркала»)"), behaviorCard);
	alwaysOnTopCheck = new QCheckBox(QStringLiteral("Поверх всех окон"), behaviorCard);
	minimizeOnRecordCheck = new QCheckBox(QStringLiteral("Сворачивать окно в трей при начале записи"),
					      behaviorCard);
	showRegionFrameCheck =
		new QCheckBox(QStringLiteral("Показывать цветную рамку вокруг выбранной области экрана"), behaviorCard);
	startInBandicamCheck =
		new QCheckBox(QStringLiteral("Всегда запускать в интерфейсе Bandicam (а не в полном OBS)"),
			      behaviorCard);

	behLay->addWidget(hideFromCaptureCheck);
	behLay->addWidget(alwaysOnTopCheck);
	behLay->addWidget(minimizeOnRecordCheck);
	behLay->addWidget(showRegionFrameCheck);
	behLay->addWidget(startInBandicamCheck);
	layout->addWidget(behaviorCard);

	// Auto-stop card (classic Bandicam feature)
	auto *autoCard = new QFrame(page);
	autoCard->setObjectName(QStringLiteral("bandicamCard"));
	auto *autoLay = new QHBoxLayout(autoCard);
	autoLay->setContentsMargins(12, 10, 12, 10);
	autoLay->setSpacing(8);

	autoStopCheck = new QCheckBox(QStringLiteral("Автоостановка записи:"), autoCard);
	autoStopTypeCombo = new QComboBox(autoCard);
	autoStopTypeCombo->addItem(QStringLiteral("по времени (минут)"), 0);
	autoStopTypeCombo->addItem(QStringLiteral("по размеру (МБ)"), 1);
	autoStopValueSpin = new QSpinBox(autoCard);
	autoStopValueSpin->setRange(1, 99999);
	autoStopValueSpin->setValue(30);

	autoLay->addWidget(autoStopCheck);
	autoLay->addWidget(autoStopTypeCombo);
	autoLay->addWidget(autoStopValueSpin);
	autoLay->addStretch(1);
	layout->addWidget(autoCard);

	layout->addStretch(1);

	connect(hideFromCaptureCheck, &QCheckBox::toggled, this, [this](bool checked) {
		if (syncingUi)
			return;
		config_set_bool(App()->GetUserConfig(), "BasicWindow", "HideOBSWindowsFromCapture", checked);
		config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
		if (QWindow *win = main->windowHandle())
			main->SetDisplayAffinity(win);
		ShowToast(checked ? QStringLiteral("Окно скрыто от захвата") : QStringLiteral("Окно видно на захвате"));
	});
	connect(alwaysOnTopCheck, &QCheckBox::toggled, this, [this](bool checked) {
		if (syncingUi)
			return;
		SetAlwaysOnTop(main, checked);
		config_set_bool(App()->GetUserConfig(), "BasicWindow", "AlwaysOnTop", checked);
		if (main->ui && main->ui->actionAlwaysOnTop) {
			QSignalBlocker b(main->ui->actionAlwaysOnTop);
			main->ui->actionAlwaysOnTop->setChecked(checked);
		}
	});
	connect(minimizeOnRecordCheck, &QCheckBox::toggled, this, [this](bool checked) {
		if (syncingUi)
			return;
		minimizeOnRecord = checked;
		SavePreferences();
	});
	connect(showRegionFrameCheck, &QCheckBox::toggled, this, [this](bool checked) {
		if (syncingUi)
			return;
		showRegionFrame = checked;
		if (regionFrame)
			regionFrame->SetFrameEnabled(showRegionFrame);
		SavePreferences();
	});
	connect(startInBandicamCheck, &QCheckBox::toggled, this, [this](bool checked) {
		if (syncingUi)
			return;
		config_set_bool(App()->GetUserConfig(), kConfigSection, "AlwaysStartBandicam", checked);
		config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
	});

	auto syncAutoStop = [this]() {
		if (syncingUi)
			return;
		autoStopEnabled = autoStopCheck->isChecked();
		autoStopType = autoStopTypeCombo->currentIndex();
		autoStopValue = autoStopValueSpin->value();
		autoStopTypeCombo->setEnabled(autoStopEnabled);
		autoStopValueSpin->setEnabled(autoStopEnabled);
		SavePreferences();
	};
	connect(autoStopCheck, &QCheckBox::toggled, this, syncAutoStop);
	connect(autoStopTypeCombo, &QComboBox::currentIndexChanged, this, syncAutoStop);
	connect(autoStopValueSpin, qOverload<int>(&QSpinBox::valueChanged), this, syncAutoStop);

	return page;
}

QWidget *BandicamRecorderWidget::BuildFooterBar()
{
	auto *footer = new QFrame(this);
	footer->setObjectName(QStringLiteral("bandicamFooter"));
	footer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto *layout = new QHBoxLayout(footer);
	layout->setContentsMargins(10, 5, 10, 5);
	layout->setSpacing(12);

	footerSourceLabel = new QLabel(QStringLiteral("Источник: не выбран"), footer);
	footerSourceLabel->setObjectName(QStringLiteral("mutedLabel"));
	layout->addWidget(footerSourceLabel);

	footerSpecLabel = new QLabel(footer);
	footerSpecLabel->setObjectName(QStringLiteral("mutedLabel"));
	layout->addWidget(footerSpecLabel);

	layout->addStretch(1);

	footerToastLabel = new QLabel(footer);
	footerToastLabel->setStyleSheet(QStringLiteral("color: #4ade80; font-size: 12px; font-weight: 600;"));
	footerToastLabel->setVisible(false);
	layout->addWidget(footerToastLabel);

	footerDiskLabel = new QLabel(footer);
	footerDiskLabel->setObjectName(QStringLiteral("mutedLabel"));
	layout->addWidget(footerDiskLabel);

	return footer;
}

void BandicamRecorderWidget::InitializeAfterOBSReady()
{
	if (initialized)
		return;
	initialized = true;

	config_t *userCfg = App()->GetUserConfig();
	config_set_default_bool(userCfg, kConfigSection, "AlwaysStartBandicam", true);
	config_set_default_bool(userCfg, kConfigSection, "PreviewVisible", false);
	config_set_default_bool(userCfg, kConfigSection, "ShowRegionFrame", true);
	config_set_default_bool(userCfg, kConfigSection, "MinimizeOnRecord", false);
	config_set_default_bool(userCfg, kConfigSection, "AutoStopEnabled", false);
	config_set_default_int(userCfg, kConfigSection, "AutoStopType", 0);
	config_set_default_int(userCfg, kConfigSection, "AutoStopValue", 30);
	config_set_default_string(userCfg, kConfigSection, "ViewMode", "bandicam");

	// Default HideOBSWindowsFromCapture = true on fresh portable Bandicam setup for mirror-free screen capture
	if (!config_has_user_value(userCfg, "BasicWindow", "HideOBSWindowsFromCapture")) {
		config_set_bool(userCfg, "BasicWindow", "HideOBSWindowsFromCapture", true);
		if (QWindow *win = main->windowHandle())
			main->SetDisplayAffinity(win);
	}

	previewVisibleInRecorder = config_get_bool(userCfg, kConfigSection, "PreviewVisible");
	showRegionFrame = config_get_bool(userCfg, kConfigSection, "ShowRegionFrame");
	minimizeOnRecord = config_get_bool(userCfg, kConfigSection, "MinimizeOnRecord");
	autoStopEnabled = config_get_bool(userCfg, kConfigSection, "AutoStopEnabled");
	autoStopType = static_cast<int>(config_get_int(userCfg, kConfigSection, "AutoStopType"));
	autoStopValue = std::max(1, static_cast<int>(config_get_int(userCfg, kConfigSection, "AutoStopValue")));

	const bool alwaysBandicam = config_get_bool(userCfg, kConfigSection, "AlwaysStartBandicam");
	const char *savedMode = config_get_string(userCfg, kConfigSection, "ViewMode");

	ViewMode initialMode = ViewMode::Bandicam;
	if (!alwaysBandicam && savedMode) {
		if (strcmp(savedMode, "studio") == 0)
			initialMode = ViewMode::Studio;
		else if (strcmp(savedMode, "mini") == 0)
			initialMode = ViewMode::Mini;
	}

	// Capture initial OBS Studio dock layout and geometry BEFORE we hide docks for Bandicam mode
	studioDockState = main->saveState();
	studioGeometry = main->saveGeometry();
	studioStateSaved = true;

	{
		QSignalBlocker b(navPreviewBtn);
		navPreviewBtn->setChecked(previewVisibleInRecorder);
	}

	// Connect OBS recording & screenshot signals
	connect(main, &OBSBasic::RecordingStarted, this, &BandicamRecorderWidget::OnRecordingStarted);
	connect(main, &OBSBasic::RecordingStopped, this, &BandicamRecorderWidget::OnRecordingStopped);
	connect(main, &OBSBasic::RecordingPaused, this, &BandicamRecorderWidget::OnRecordingPaused);
	connect(main, &OBSBasic::RecordingUnpaused, this, &BandicamRecorderWidget::OnRecordingUnpaused);

	LoadVideoPageFromConfig();
	LoadGeneralPageFromConfig();
	SyncAudioControls();
	RefreshActiveSourceInfo();
	UpdateRecordingUi();
	UpdateFooterStatus();

	SetViewMode(initialMode, false);

	audioSyncTimer->start();
}

void BandicamRecorderWidget::SavePreferences()
{
	config_t *userCfg = App()->GetUserConfig();
	const char *modeStr = currentViewMode == ViewMode::Studio ? "studio"
			      : currentViewMode == ViewMode::Mini ? "mini"
								  : "bandicam";
	config_set_string(userCfg, kConfigSection, "ViewMode", modeStr);
	config_set_bool(userCfg, kConfigSection, "PreviewVisible", previewVisibleInRecorder);
	config_set_bool(userCfg, kConfigSection, "ShowRegionFrame", showRegionFrame);
	config_set_bool(userCfg, kConfigSection, "MinimizeOnRecord", minimizeOnRecord);
	config_set_bool(userCfg, kConfigSection, "AutoStopEnabled", autoStopEnabled);
	config_set_int(userCfg, kConfigSection, "AutoStopType", autoStopType);
	config_set_int(userCfg, kConfigSection, "AutoStopValue", autoStopValue);
	config_save_safe(userCfg, "tmp", nullptr);
}

void BandicamRecorderWidget::SetViewMode(ViewMode mode, bool savePreference)
{
	const ViewMode previousMode = currentViewMode;

	// Save Studio layout when leaving Studio mode
	if (previousMode == ViewMode::Studio && mode != ViewMode::Studio) {
		studioDockState = main->saveState();
		studioGeometry = main->saveGeometry();
		studioStateSaved = true;
	}
	// Save Bandicam window size when leaving Bandicam mode (if not maximized)
	if (previousMode == ViewMode::Bandicam && mode != ViewMode::Bandicam && !main->isMaximized()) {
		bandicamSavedSize = main->size();
		hasBandicamSavedSize = true;
	}

	currentViewMode = mode;

	const bool isStudio = (mode == ViewMode::Studio);
	const bool isMini = (mode == ViewMode::Mini);
	const bool isBandicam = (mode == ViewMode::Bandicam);

	topBarWidget->setVisible(!isStudio);
	studioStripWidget->setVisible(isStudio);
	bodyWidget->setVisible(isBandicam);
	footerWidget->setVisible(isBandicam);

	{
		QSignalBlocker b(miniToggleBtn);
		miniToggleBtn->setChecked(isMini);
	}

	if (isStudio) {
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		bodyWidget->setMaximumHeight(QWIDGETSIZE_MAX);
		main->ui->bandicamHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		main->ui->menubar->setVisible(true);
		main->ui->statusbar->setVisible(true);
		main->ui->canvasEditor->setVisible(true);
		main->ui->contextContainer->setVisible(true);
		if (studioStateSaved) {
			main->restoreGeometry(studioGeometry);
			main->restoreState(studioDockState);
		}
		if (regionFrame)
			regionFrame->hide();
	} else {
		for (QDockWidget *dock : main->findChildren<QDockWidget *>())
			dock->hide();
		main->ui->menubar->setVisible(false);
		main->ui->statusbar->setVisible(false);

		const bool showPreview = isBandicam && previewVisibleInRecorder;
		if (isMini) {
			setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			main->ui->bandicamHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		} else if (showPreview) {
			setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
			bodyWidget->setMaximumHeight(350);
			main->ui->bandicamHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
		} else {
			setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
			bodyWidget->setMaximumHeight(QWIDGETSIZE_MAX);
			main->ui->bandicamHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		}

		main->ui->canvasEditor->setVisible(showPreview);
		main->ui->contextContainer->setVisible(showPreview);
		if (showPreview && !main->previewEnabled) {
			main->EnablePreviewDisplay(true);
		}

		if (!main->isMaximized()) {
			if (isMini) {
				main->setMinimumHeight(0);
				main->resize(std::max(main->width(), 920), topBarWidget->sizeHint().height() + 16);
			} else if (showPreview) {
				main->resize(std::max(main->width(), 980), 820);
			} else if (hasBandicamSavedSize) {
				main->resize(bandicamSavedSize);
			} else {
				main->resize(940, 560);
			}
		}

		if (regionFrame && showRegionFrame && currentCaptureMode == CaptureMode::Area &&
		    lastRegionLogicalRect.isValid()) {
			regionFrame->SetFrameEnabled(true);
		}
	}

	ApplyWindowTitle();
	SyncAudioControls();
	RefreshActiveSourceInfo();
	UpdateFooterStatus();

	if (savePreference)
		SavePreferences();
}

void BandicamRecorderWidget::TogglePreviewVisibility(bool visible)
{
	previewVisibleInRecorder = visible;
	if (currentViewMode == ViewMode::Bandicam) {
		if (visible) {
			setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
			bodyWidget->setMaximumHeight(350);
			main->ui->bandicamHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
		} else {
			setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
			bodyWidget->setMaximumHeight(QWIDGETSIZE_MAX);
			main->ui->bandicamHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		}
		main->ui->canvasEditor->setVisible(visible);
		main->ui->contextContainer->setVisible(visible);
		if (visible && !main->previewEnabled)
			main->EnablePreviewDisplay(true);
		if (!main->isMaximized()) {
			main->resize(std::max(main->width(), 960), visible ? std::max(main->height(), 820) : 560);
		}
	}
	SavePreferences();
}

void BandicamRecorderWidget::ApplyWindowTitle()
{
	if (!main)
		return;

	if (currentViewMode == ViewMode::Studio) {
		main->UpdateTitleBar();
		return;
	}

	if (main->RecordingActive()) {
		const bool paused = main->isRecordingPausable && main->outputHandler &&
				    main->outputHandler->fileOutput &&
				    obs_output_paused(main->outputHandler->fileOutput);
		const QString prefix = paused ? QStringLiteral("Ⅱ ПАУЗА ") : QStringLiteral("● REC ");
		main->setWindowTitle(prefix + timerPillLabel->text() + QStringLiteral(" — OBS Recorder"));
	} else {
		main->setWindowTitle(QStringLiteral("OBS Recorder"));
	}
}

void BandicamRecorderWidget::SyncAudioControls()
{
	auto syncChannel = [this](uint32_t channel, QPushButton *btn, BandicamAudioMeter *meter,
				  OBSWeakSource &attachedRef, const QString &label, IconKind iconOn, IconKind iconOff) {
		OBSSourceAutoRelease source(obs_get_output_source(channel));
		OBSSource currentAttached = OBSGetStrongRef(attachedRef);
		if (source.Get() != currentAttached.Get()) {
			meter->AttachSource(source.Get());
			attachedRef = source ? OBSGetWeakRef(source.Get()) : nullptr;
		}

		QSignalBlocker b(btn);
		if (!source) {
			btn->setChecked(false);
			btn->setIcon(MakeVectorIcon(iconOff, QColor("#9ca3af")));
			btn->setText(label);
			btn->setToolTip(label + QStringLiteral(" не настроен — нажмите, чтобы включить устройство по умолчанию"));
			meter->SetConfigured(false);
			meter->SetMuted(true);
			return;
		}

		const bool enabled = !obs_source_muted(source.Get());
		btn->setChecked(enabled);
		btn->setIcon(MakeVectorIcon(enabled ? iconOn : iconOff, enabled ? QColor("#dcfce7") : QColor("#fca5a5")));
		btn->setText(label);
		btn->setToolTip(enabled ? label + QStringLiteral(": включён (нажмите, чтобы выключить)")
					: label + QStringLiteral(": выключен (нажмите, чтобы включить)"));
		meter->SetConfigured(true);
		meter->SetMuted(!enabled);
	};

	syncChannel(kDesktopAudioChannel, sysAudioBtn, sysAudioMeter, attachedSysSource, QStringLiteral("Звук"),
		    IconKind::Speaker, IconKind::SpeakerMuted);
	syncChannel(kMicAudioChannel, micAudioBtn, micAudioMeter, attachedMicSource, QStringLiteral("Микр."),
		    IconKind::Mic, IconKind::MicMuted);
}

void BandicamRecorderWidget::ToggleAudioChannel(uint32_t channel, const QString &friendlyName)
{
	OBSSourceAutoRelease source(obs_get_output_source(channel));
	if (!source) {
		// Automatically enable the default desktop/mic audio device instead of leaving the button dead!
		const char *sourceId = (channel == kDesktopAudioChannel) ? App()->OutputAudioSource()
									 : App()->InputAudioSource();
		const char *desc = (channel == kDesktopAudioChannel) ? Str("Basic.DesktopDevice1")
								     : Str("Basic.AuxDevice1");
		if (sourceId) {
			main->ResetAudioDevice(sourceId, "default", desc, static_cast<int>(channel));
			SyncAudioControls();
			ShowToast(friendlyName + QStringLiteral(": подключено устройство по умолчанию"));
			return;
		}
		main->on_action_Settings_triggered();
		SyncAudioControls();
		return;
	}

	const bool nowMuted = !obs_source_muted(source.Get());
	obs_source_set_muted(source.Get(), nowMuted);
	SyncAudioControls();
	ShowToast(friendlyName + (nowMuted ? QStringLiteral(": выкл.") : QStringLiteral(": вкл.")));
}

OBSSceneItem BandicamRecorderWidget::FindPrimaryCaptureItem(OBSScene *outScene) const
{
	OBSScene scene = main->GetCurrentScene();
	if (outScene)
		*outScene = scene;
	if (!scene)
		return nullptr;

	struct SearchCtx {
		OBSSceneItem found;
	} ctx;

	obs_scene_enum_items(
		scene.Get(),
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *c = static_cast<SearchCtx *>(param);
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src && IsCaptureSourceId(obs_source_get_unversioned_id(src))) {
				c->found = item;
			}
			return true;
		},
		&ctx);
	return ctx.found;
}

OBSSceneItem BandicamRecorderWidget::FindWebcamItem(OBSScene *outScene) const
{
	OBSScene scene = main->GetCurrentScene();
	if (outScene)
		*outScene = scene;
	if (!scene)
		return nullptr;

	struct SearchCtx {
		OBSSceneItem found;
	} ctx;

	obs_scene_enum_items(
		scene.Get(),
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *c = static_cast<SearchCtx *>(param);
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src && IsWebcamSourceId(obs_source_get_unversioned_id(src))) {
				c->found = item;
			}
			return true;
		},
		&ctx);
	return ctx.found;
}

void BandicamRecorderWidget::UpdateWebcamButtonState()
{
	OBSSceneItem camItem = FindWebcamItem();
	const bool active = camItem && obs_sceneitem_visible(camItem.Get());
	QSignalBlocker b(webcamBtn);
	webcamBtn->setChecked(active);
}

void BandicamRecorderWidget::ToggleWebcamOverlay()
{
	OBSScene scene;
	OBSSceneItem camItem = FindWebcamItem(&scene);
	if (camItem) {
		const bool nextVis = !obs_sceneitem_visible(camItem.Get());
		obs_sceneitem_set_visible(camItem.Get(), nextVis);
		UpdateWebcamButtonState();
		ShowToast(nextVis ? QStringLiteral("Веб-камера включена") : QStringLiteral("Веб-камера скрыта"));
		return;
	}

#ifdef _WIN32
	const char *camId = "dshow_input";
#else
	const char *camId = "v4l2_input";
#endif
	if (!IsInputRegistered(camId)) {
		ShowToast(QStringLiteral("Модуль веб-камеры недоступен"), true);
		UpdateWebcamButtonState();
		return;
	}
	const char *latestId = obs_get_latest_input_type_id(camId);
	if (!latestId || !scene) {
		UpdateWebcamButtonState();
		return;
	}

	std::string name = "Веб-камера";
	for (uint32_t i = 2;; ++i) {
		OBSSourceAutoRelease ex(obs_get_source_by_name(name.c_str()));
		if (!ex)
			break;
		name = "Веб-камера (" + std::to_string(i) + ")";
	}

	OBSSourceAutoRelease source(obs_source_create(latestId, name.c_str(), nullptr, nullptr));
	if (!source) {
		UpdateWebcamButtonState();
		return;
	}

	OBSDataAutoRelease settings = obs_source_get_settings(source.Get());
	QDialog dlg(this);
	dlg.setWindowTitle(QStringLiteral("Настройка веб-камеры (PiP)"));
	dlg.resize(520, 400);
	auto *lay = new QVBoxLayout(&dlg);
	auto *props = new OBSPropertiesView(settings.Get(), source.Get(),
					    reinterpret_cast<PropertiesReloadCallback>(obs_source_properties), nullptr,
					    reinterpret_cast<PropertiesVisualUpdateCb>(obs_source_update));
	auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Добавить камеру"));
	connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	lay->addWidget(props, 1);
	lay->addWidget(btns);

	if (dlg.exec() != QDialog::Accepted) {
		obs_source_remove(source.Get());
		UpdateWebcamButtonState();
		return;
	}
	if (props->DeferUpdate())
		props->UpdateSettings();

	AddSceneItemData addData{source.Get(), nullptr};
	obs_enter_graphics();
	obs_scene_atomic_update(scene.Get(), AddSceneItemAtomic, &addData);
	obs_leave_graphics();

	if (addData.sceneItem) {
		// Position webcam as a clean bottom-right PiP box (24% of canvas width)
		obs_video_info ovi{};
		if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
			const float pipW = ovi.base_width * 0.24f;
			const float pipH = pipW * 9.0f / 16.0f;
			const float margin = 24.0f;
			vec2 pos{ovi.base_width - pipW - margin, ovi.base_height - pipH - margin};
			vec2 bounds{pipW, pipH};
			obs_sceneitem_set_pos(addData.sceneItem, &pos);
			obs_sceneitem_set_bounds_type(addData.sceneItem, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds(addData.sceneItem, &bounds);
			obs_sceneitem_set_order(addData.sceneItem, OBS_ORDER_MOVE_TOP);
		}
		ShowToast(QStringLiteral("Веб-камера добавлена в правый нижний угол"));
	}
	UpdateWebcamButtonState();
}

void BandicamRecorderWidget::UpdateCursorButtonState()
{
	OBSSceneItem primary = FindPrimaryCaptureItem();
	bool showCursor = true;
	if (primary) {
		obs_source_t *src = obs_sceneitem_get_source(primary.Get());
		if (src) {
			const char *id = obs_source_get_unversioned_id(src);
			OBSDataAutoRelease settings = obs_source_get_settings(src);
			if (id && strcmp(id, "window_capture") == 0)
				showCursor = obs_data_get_bool(settings.Get(), "cursor");
			else
				showCursor = obs_data_get_bool(settings.Get(), "capture_cursor");
		}
	}

	QSignalBlocker b1(cursorBtn);
	cursorBtn->setChecked(showCursor);
	if (videoCursorCheck) {
		QSignalBlocker b2(videoCursorCheck);
		videoCursorCheck->setChecked(showCursor);
	}
}

void BandicamRecorderWidget::ToggleCursorCapture()
{
	const bool showCursor = cursorBtn->isChecked();
	OBSScene scene = main->GetCurrentScene();
	if (scene) {
		struct CursorCtx {
			bool value;
		} ctx{showCursor};
		obs_scene_enum_items(
			scene.Get(),
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				auto *c = static_cast<CursorCtx *>(param);
				obs_source_t *src = obs_sceneitem_get_source(item);
				if (!src)
					return true;
				const char *id = obs_source_get_unversioned_id(src);
				if (!IsCaptureSourceId(id))
					return true;
				OBSDataAutoRelease settings = obs_source_get_settings(src);
				if (strcmp(id, "window_capture") == 0)
					obs_data_set_bool(settings.Get(), "cursor", c->value);
				else
					obs_data_set_bool(settings.Get(), "capture_cursor", c->value);
				obs_source_update(src, settings.Get());
				return true;
			},
			&ctx);
	}
	if (videoCursorCheck) {
		QSignalBlocker b(videoCursorCheck);
		videoCursorCheck->setChecked(showCursor);
	}
	ShowToast(showCursor ? QStringLiteral("Курсор мыши: показывать") : QStringLiteral("Курсор мыши: скрывать"));
}

void BandicamRecorderWidget::SelectCaptureMode(CaptureMode mode, bool configureIfNeeded)
{
	currentCaptureMode = mode;

	auto syncChecked = [mode](QPushButton *btn, CaptureMode m) {
		if (!btn)
			return;
		QSignalBlocker b(btn);
		btn->setChecked(mode == m);
	};
	syncChecked(modeAreaBtn, CaptureMode::Area);
	syncChecked(modeScreenBtn, CaptureMode::Screen);
	syncChecked(modeGameBtn, CaptureMode::Game);
	syncChecked(modeWindowBtn, CaptureMode::Window);
	syncChecked(cardAreaBtn, CaptureMode::Area);
	syncChecked(cardScreenBtn, CaptureMode::Screen);
	syncChecked(cardGameBtn, CaptureMode::Game);
	syncChecked(cardWindowBtn, CaptureMode::Window);

	if (mode != CaptureMode::Area && regionFrame)
		regionFrame->hide();

	if (!configureIfNeeded) {
		RefreshActiveSourceInfo();
		return;
	}

	if (mode == CaptureMode::Area) {
		StartRegionSelection();
	} else {
		ConfigureOrAddCaptureSource(mode, true);
	}
}

void BandicamRecorderWidget::StartRegionSelection()
{
	if (!regionSelector) {
		regionSelector = new BandicamRegionSelector(nullptr);
		connect(regionSelector, &BandicamRegionSelector::regionSelected, this,
			&BandicamRecorderWidget::ApplySelectedRegion);
		connect(regionSelector, &BandicamRegionSelector::canceled, this, [this]() {
			main->showNormal();
			main->activateWindow();
		});
	}
	if (regionFrame)
		regionFrame->hide();

	// Briefly hide main window so the user can select any part of their desktop unobstructed
	const bool wasVisible = main->isVisible();
	if (wasVisible)
		main->hide();

	QTimer::singleShot(120, this, [this]() {
		if (regionSelector)
			regionSelector->BeginSelection();
	});
}

void BandicamRecorderWidget::ApplySelectedRegion(const QRect &globalLogicalRect, QScreen *screen)
{
	main->show();
	main->activateWindow();

	if (!screen || !globalLogicalRect.isValid())
		return;

	lastRegionLogicalRect = globalLogicalRect;

#ifdef _WIN32
	if (!IsInputRegistered("monitor_capture")) {
		ShowToast(QStringLiteral("Модуль захвата экрана недоступен"), true);
		return;
	}

	const qreal dpr = std::max<qreal>(1.0, screen->devicePixelRatio());
	const QRect sg = screen->geometry();
	const int localPhysX = static_cast<int>(std::round((globalLogicalRect.left() - sg.left()) * dpr));
	const int localPhysY = static_cast<int>(std::round((globalLogicalRect.top() - sg.top()) * dpr));
	const int physW = static_cast<int>(std::round(globalLogicalRect.width() * dpr));
	const int physH = static_cast<int>(std::round(globalLogicalRect.height() * dpr));
	const int screenPhysW = static_cast<int>(std::round(sg.width() * dpr));
	const int screenPhysH = static_cast<int>(std::round(sg.height() * dpr));

	// Map center to Windows HMONITOR to get exact monitor_id for duplicator-monitor-capture
	const QPoint centerPhys(static_cast<int>(std::round(sg.left() + localPhysX + physW / 2.0)),
				static_cast<int>(std::round(sg.top() + localPhysY + physH / 2.0)));
	const WinMonitorMatch mon = FindWindowsMonitorForPhysicalPoint(centerPhys.x(), centerPhys.y());

	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return;

	// Replace existing primary capture source so sources don't pile up
	OBSSceneItem existing = FindPrimaryCaptureItem();
	OBSSource captureSource;
	OBSSceneItem targetItem;

	if (existing) {
		obs_source_t *src = obs_sceneitem_get_source(existing.Get());
		if (src && strcmp(obs_source_get_unversioned_id(src), "monitor_capture") == 0) {
			captureSource = src;
			targetItem = existing;
		} else {
			obs_sceneitem_remove(existing.Get());
		}
	}

	if (!captureSource) {
		const char *latestId = obs_get_latest_input_type_id("monitor_capture");
		if (!latestId)
			return;
		std::string name = "Захват области";
		for (uint32_t i = 2;; ++i) {
			OBSSourceAutoRelease ex(obs_get_source_by_name(name.c_str()));
			if (!ex)
				break;
			name = "Захват области (" + std::to_string(i) + ")";
		}
		OBSSourceAutoRelease created(obs_source_create(latestId, name.c_str(), nullptr, nullptr));
		if (!created)
			return;
		captureSource = created.Get();

		AddSceneItemData addData{captureSource.Get(), nullptr};
		obs_enter_graphics();
		obs_scene_atomic_update(scene.Get(), AddSceneItemAtomic, &addData);
		obs_leave_graphics();
		targetItem = addData.sceneItem;
		if (targetItem)
			obs_sceneitem_set_order(targetItem.Get(), OBS_ORDER_MOVE_BOTTOM);
	}

	if (!captureSource || !targetItem)
		return;

	OBSDataAutoRelease settings = obs_source_get_settings(captureSource.Get());
	if (mon.found && !mon.deviceId.empty())
		obs_data_set_string(settings.Get(), "monitor_id", mon.deviceId.c_str());
	obs_data_set_bool(settings.Get(), "capture_cursor", cursorBtn ? cursorBtn->isChecked() : true);
	obs_source_update(captureSource.Get(), settings.Get());

	const int monW = mon.found ? static_cast<int>(mon.rect.right - mon.rect.left) : screenPhysW;
	const int monH = mon.found ? static_cast<int>(mon.rect.bottom - mon.rect.top) : screenPhysH;

	obs_sceneitem_crop crop{};
	crop.left = std::clamp(localPhysX, 0, std::max(0, monW - 16));
	crop.top = std::clamp(localPhysY, 0, std::max(0, monH - 16));
	crop.right = std::clamp(monW - (crop.left + physW), 0, std::max(0, monW - crop.left - 16));
	crop.bottom = std::clamp(monH - (crop.top + physH), 0, std::max(0, monH - crop.top - 16));
	obs_sceneitem_set_crop(targetItem.Get(), &crop);

	// Fit cropped region cleanly inside OBS canvas
	obs_video_info ovi{};
	if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
		vec2 pos{0.0f, 0.0f};
		vec2 bounds{static_cast<float>(ovi.base_width), static_cast<float>(ovi.base_height)};
		obs_sceneitem_set_pos(targetItem.Get(), &pos);
		obs_sceneitem_set_bounds_type(targetItem.Get(), OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds_alignment(targetItem.Get(), OBS_ALIGN_CENTER);
		obs_sceneitem_set_bounds(targetItem.Get(), &bounds);
	}

	obs_scene_enum_items(scene.Get(), SelectSingleSceneItem, targetItem.Get());

	if (!regionFrame)
		regionFrame = new BandicamRegionFrame(nullptr);
	regionFrame->SetFrameEnabled(showRegionFrame);
	regionFrame->SetTargetRegion(globalLogicalRect);
	regionFrame->SetState(BandicamRegionFrame::State::Ready);

	RefreshActiveSourceInfo();
	ShowToast(QStringLiteral("Выбрана область %1 × %2 px").arg(physW).arg(physH));
#else
	Q_UNUSED(globalLogicalRect);
	Q_UNUSED(screen);
#endif
}

void BandicamRecorderWidget::ConfigureOrAddCaptureSource(CaptureMode mode, bool replaceExisting)
{
#ifdef _WIN32
	const char *sourceId = nullptr;
	const char *baseName = nullptr;
	switch (mode) {
	case CaptureMode::Screen:
	case CaptureMode::Area:
		sourceId = "monitor_capture";
		baseName = "Захват экрана";
		break;
	case CaptureMode::Game:
		sourceId = "game_capture";
		baseName = "Захват игры";
		break;
	case CaptureMode::Window:
		sourceId = "window_capture";
		baseName = "Захват окна";
		break;
	}

	if (!IsInputRegistered(sourceId)) {
		QMessageBox::warning(this, QStringLiteral("Источник недоступен"),
				     QStringLiteral("Модуль захвата Windows не зарегистрировал выбранный тип источника."));
		return;
	}

	const char *latestSourceId = obs_get_latest_input_type_id(sourceId);
	if (!latestSourceId)
		return;

	OBSScene scene = main->GetCurrentScene();
	if (!scene) {
		QMessageBox::warning(this, QStringLiteral("Нет активной сцены"),
				     QStringLiteral("Сначала создайте или выберите сцену OBS."));
		return;
	}

	// Reuse existing source if same type, or create new one
	OBSSceneItem existingItem = FindPrimaryCaptureItem();
	OBSSource reuseSource;
	if (existingItem && replaceExisting) {
		obs_source_t *src = obs_sceneitem_get_source(existingItem.Get());
		if (src && strcmp(obs_source_get_unversioned_id(src), sourceId) == 0) {
			reuseSource = src;
		}
	}

	OBSSource targetSource = reuseSource;
	bool createdNew = false;
	std::string sourceName = baseName;

	if (!targetSource) {
		for (uint32_t suffix = 2;; ++suffix) {
			OBSSourceAutoRelease existing(obs_get_source_by_name(sourceName.c_str()));
			if (!existing)
				break;
			sourceName = std::string(baseName) + " (" + std::to_string(suffix) + ")";
		}
		OBSSourceAutoRelease created(obs_source_create(latestSourceId, sourceName.c_str(), nullptr, nullptr));
		if (!created)
			return;
		targetSource = created.Get();
		createdNew = true;
	}

	OBSDataAutoRelease sourceSettings = obs_source_get_settings(targetSource.Get());
	OBSDataAutoRelease originalSettings = obs_data_create();
	obs_data_apply(originalSettings.Get(), sourceSettings.Get());

	QDialog setupDialog(this);
	setupDialog.setWindowTitle(mode == CaptureMode::Screen   ? QStringLiteral("Настройка захвата экрана")
				   : mode == CaptureMode::Game   ? QStringLiteral("Настройка игрового режима")
				   : mode == CaptureMode::Window ? QStringLiteral("Выбор окна для записи")
								 : QStringLiteral("Настройка захвата"));
	setupDialog.resize(560, 420);
	auto *setupLayout = new QVBoxLayout(&setupDialog);
	auto *setupLabel = new QLabel(
		mode == CaptureMode::Screen
			? QStringLiteral("Выберите монитор для полноэкранной записи.")
		: mode == CaptureMode::Game
			? QStringLiteral("Выберите режим захвата игры (любое полноэкранное приложение или конкретное окно).")
			: QStringLiteral("Выберите окно приложения, которое нужно записать."),
		&setupDialog);
	setupLabel->setWordWrap(true);
	auto *properties = new OBSPropertiesView(sourceSettings.Get(), targetSource.Get(),
						 reinterpret_cast<PropertiesReloadCallback>(obs_source_properties),
						 nullptr,
						 reinterpret_cast<PropertiesVisualUpdateCb>(obs_source_update));
	properties->setMinimumWidth(500);
	properties->setMinimumHeight(220);
	auto *setupButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &setupDialog);
	setupButtons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Применить"));
	connect(setupButtons, &QDialogButtonBox::accepted, &setupDialog, &QDialog::accept);
	connect(setupButtons, &QDialogButtonBox::rejected, &setupDialog, &QDialog::reject);
	setupLayout->addWidget(setupLabel);
	setupLayout->addWidget(properties, 1);
	setupLayout->addWidget(setupButtons);

	if (setupDialog.exec() != QDialog::Accepted) {
		obs_source_update(targetSource.Get(), originalSettings.Get());
		if (createdNew)
			obs_source_remove(targetSource.Get());
		RefreshActiveSourceInfo();
		return;
	}
	if (properties->DeferUpdate())
		properties->UpdateSettings();

	OBSSceneItem sceneItem = existingItem;
	if (createdNew) {
		if (replaceExisting && existingItem) {
			obs_sceneitem_remove(existingItem.Get());
		}
		AddSceneItemData addData{targetSource.Get(), nullptr};
		obs_enter_graphics();
		obs_scene_atomic_update(scene.Get(), AddSceneItemAtomic, &addData);
		obs_leave_graphics();
		sceneItem = addData.sceneItem;
		if (sceneItem)
			obs_sceneitem_set_order(sceneItem.Get(), OBS_ORDER_MOVE_BOTTOM);
	}

	if (sceneItem) {
		// Clear any previous area crop and fit cleanly to canvas
		obs_sceneitem_crop zeroCrop{0, 0, 0, 0};
		obs_sceneitem_set_crop(sceneItem.Get(), &zeroCrop);
		obs_video_info ovi{};
		if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
			vec2 pos{0.0f, 0.0f};
			vec2 bounds{static_cast<float>(ovi.base_width), static_cast<float>(ovi.base_height)};
			obs_sceneitem_set_pos(sceneItem.Get(), &pos);
			obs_sceneitem_set_bounds_type(sceneItem.Get(), OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds_alignment(sceneItem.Get(), OBS_ALIGN_CENTER);
			obs_sceneitem_set_bounds(sceneItem.Get(), &bounds);
		}
		obs_scene_enum_items(scene.Get(), SelectSingleSceneItem, sceneItem.Get());
	}

	RefreshActiveSourceInfo();
	UpdateCursorButtonState();
	ShowToast(QStringLiteral("Источник захвата готов"));
#else
	Q_UNUSED(mode);
	Q_UNUSED(replaceExisting);
	main->ui->actionAddSource->trigger();
#endif
}

void BandicamRecorderWidget::ConfigureCurrentPrimarySource()
{
	OBSSceneItem item = FindPrimaryCaptureItem();
	if (!item) {
		SelectCaptureMode(currentCaptureMode, true);
		return;
	}
	obs_source_t *src = obs_sceneitem_get_source(item.Get());
	if (src) {
		main->CreatePropertiesWindow(src);
	}
}

void BandicamRecorderWidget::RemovePrimaryCaptureSource()
{
	OBSSceneItem item = FindPrimaryCaptureItem();
	if (!item)
		return;
	obs_sceneitem_remove(item.Get());
	if (regionFrame)
		regionFrame->hide();
	lastRegionLogicalRect = QRect();
	RefreshActiveSourceInfo();
	ShowToast(QStringLiteral("Источник захвата удалён"));
}

bool BandicamRecorderWidget::EnsureCaptureSourceBeforeRecording()
{
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return true;

	bool hasVideoSource = false;
	obs_scene_enum_items(
		scene.Get(),
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			if (!obs_sceneitem_visible(item))
				return true;
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src && (obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO) != 0) {
				*static_cast<bool *>(param) = true;
				return false;
			}
			return true;
		},
		&hasVideoSource);

	if (hasVideoSource)
		return true;

#ifdef _WIN32
	if (IsInputRegistered("monitor_capture")) {
		const auto answer = QMessageBox::question(
			this, QStringLiteral("Нет источника захвата"),
			QStringLiteral("В сцене пока не выбран экран, окно или игра — видео запишется чёрным.\n\n"
				       "Автоматически добавить захват основного экрана и начать запись?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
		if (answer == QMessageBox::Yes) {
			const char *latestId = obs_get_latest_input_type_id("monitor_capture");
			if (latestId) {
				OBSSourceAutoRelease src(obs_source_create(latestId, "Захват экрана", nullptr, nullptr));
				if (src) {
					AddSceneItemData addData{src.Get(), nullptr};
					obs_enter_graphics();
					obs_scene_atomic_update(scene.Get(), AddSceneItemAtomic, &addData);
					obs_leave_graphics();
					if (addData.sceneItem) {
						obs_video_info ovi{};
						if (obs_get_video_info(&ovi) && ovi.base_width > 0) {
							vec2 bounds{static_cast<float>(ovi.base_width),
								    static_cast<float>(ovi.base_height)};
							obs_sceneitem_set_bounds_type(addData.sceneItem,
										      OBS_BOUNDS_SCALE_INNER);
							obs_sceneitem_set_bounds_alignment(addData.sceneItem,
											   OBS_ALIGN_CENTER);
							obs_sceneitem_set_bounds(addData.sceneItem, &bounds);
						}
					}
					RefreshActiveSourceInfo();
					return true;
				}
			}
		}
		return false;
	}
#endif
	return true;
}

void BandicamRecorderWidget::RefreshActiveSourceInfo()
{
	OBSSceneItem item = FindPrimaryCaptureItem();
	if (!item) {
		activeSourceTitleLabel->setText(QStringLiteral("Источник захвата не выбран"));
		activeSourceDetailLabel->setText(QStringLiteral(
			"Нажмите на любую карточку выше (Область, Экран, Игра или Окно), чтобы начать."));
		configureSourceBtn->setText(QStringLiteral("Выбрать"));
		clearSourceBtn->setEnabled(false);
		reselectRegionBtn->setVisible(false);
		footerSourceLabel->setText(QStringLiteral("Источник: не выбран"));
		return;
	}

	obs_source_t *src = obs_sceneitem_get_source(item.Get());
	const char *unvId = src ? obs_source_get_unversioned_id(src) : "";
	const QString name = src ? QString::fromUtf8(obs_source_get_name(src)) : QStringLiteral("Источник");

	obs_sceneitem_crop crop{};
	obs_sceneitem_get_crop(item.Get(), &crop);
	const bool isCropped = (crop.left > 0 || crop.top > 0 || crop.right > 0 || crop.bottom > 0);

	QString typeLabel;
	if (strcmp(unvId, "monitor_capture") == 0) {
		currentCaptureMode = isCropped ? CaptureMode::Area : CaptureMode::Screen;
		typeLabel = isCropped ? QStringLiteral("Прямоугольная область экрана") : QStringLiteral("Полный экран");
	} else if (strcmp(unvId, "game_capture") == 0) {
		currentCaptureMode = CaptureMode::Game;
		typeLabel = QStringLiteral("Игровой режим");
	} else if (strcmp(unvId, "window_capture") == 0) {
		currentCaptureMode = CaptureMode::Window;
		typeLabel = QStringLiteral("Захват окна");
	} else {
		typeLabel = QStringLiteral("Источник видео");
	}

	auto syncChecked = [this](QPushButton *btn, CaptureMode m) {
		if (!btn)
			return;
		QSignalBlocker b(btn);
		btn->setChecked(currentCaptureMode == m);
	};
	syncChecked(modeAreaBtn, CaptureMode::Area);
	syncChecked(modeScreenBtn, CaptureMode::Screen);
	syncChecked(modeGameBtn, CaptureMode::Game);
	syncChecked(modeWindowBtn, CaptureMode::Window);
	syncChecked(cardAreaBtn, CaptureMode::Area);
	syncChecked(cardScreenBtn, CaptureMode::Screen);
	syncChecked(cardGameBtn, CaptureMode::Game);
	syncChecked(cardWindowBtn, CaptureMode::Window);

	activeSourceTitleLabel->setText(name + QStringLiteral("  •  ") + typeLabel);
	if (isCropped) {
		activeSourceDetailLabel->setText(
			QStringLiteral("Обрезка: слева %1, сверху %2, справа %3, снизу %4 px")
				.arg(crop.left)
				.arg(crop.top)
				.arg(crop.right)
				.arg(crop.bottom));
	} else {
		activeSourceDetailLabel->setText(
			QStringLiteral("Готов к записи. Нажмите «Настроить», чтобы сменить монитор или окно."));
	}

	configureSourceBtn->setText(QStringLiteral("Настроить"));
	clearSourceBtn->setEnabled(true);
	reselectRegionBtn->setVisible(currentCaptureMode == CaptureMode::Area);
	footerSourceLabel->setText(QStringLiteral("Источник: %1").arg(name));
}

void BandicamRecorderWidget::OnRecordingStarted()
{
	recordingElapsedMs = 0;
	recordingClock.start();
	autoStopTriggered = false;
	statusTimer->start();

	if (minimizeOnRecord && currentViewMode != ViewMode::Studio && main->isVisible()) {
		minimizedByRecording = true;
		if (main->trayIcon && main->trayIcon->isVisible())
			main->hide();
		else
			main->showMinimized();
	}

	UpdateRecordingUi();
	LoadVideoPageFromConfig();
}

void BandicamRecorderWidget::OnRecordingStopped()
{
	statusTimer->stop();
	recordingClock.invalidate();
	recordingElapsedMs = 0;
	autoStopTriggered = false;

	if (minimizedByRecording) {
		minimizedByRecording = false;
		main->showNormal();
		main->raise();
		main->activateWindow();
	}

	UpdateRecordingUi();
	LoadVideoPageFromConfig();
	RefreshRecordingsList();

	if (main->outputHandler && !main->outputHandler->lastRecordingPath.empty()) {
		const QFileInfo fi(QString::fromUtf8(main->outputHandler->lastRecordingPath.c_str()));
		ShowToast(QStringLiteral("Сохранено: %1").arg(fi.fileName()));
	}
}

void BandicamRecorderWidget::OnRecordingPaused()
{
	if (recordingClock.isValid()) {
		recordingElapsedMs += recordingClock.elapsed();
		recordingClock.invalidate();
	}
	UpdateRecordingUi();
}

void BandicamRecorderWidget::OnRecordingUnpaused()
{
	recordingClock.start();
	UpdateRecordingUi();
}

void BandicamRecorderWidget::OnScreenshotSaved(const QString &path)
{
	const QFileInfo fi(path);
	ShowToast(QStringLiteral("Скриншот: %1").arg(fi.fileName()));
	if (pageStack && pageStack->currentIndex() == 2)
		RefreshRecordingsList();
}

void BandicamRecorderWidget::UpdateRecordingUi()
{
	const bool active = main->RecordingActive();
	const bool paused = active && main->isRecordingPausable && main->outputHandler &&
			    main->outputHandler->fileOutput && obs_output_paused(main->outputHandler->fileOutput);

	const qint64 elapsedMs = recordingElapsedMs + (recordingClock.isValid() ? recordingClock.elapsed() : 0);
	const uint64_t totalSeconds = static_cast<uint64_t>(std::max<qint64>(0, elapsedMs) / 1000);
	const uint32_t hours = static_cast<uint32_t>(totalSeconds / 3600);
	const uint32_t minutes = static_cast<uint32_t>((totalSeconds / 60) % 60);
	const uint32_t seconds = static_cast<uint32_t>(totalSeconds % 60);
	const QString elapsed = QString::asprintf("%02u:%02u:%02u", hours, minutes, seconds);

	uint64_t totalBytes = 0;
	if (active && main->outputHandler && main->outputHandler->fileOutput) {
		totalBytes = obs_output_get_total_bytes(main->outputHandler->fileOutput);
	}

	QString pillText = elapsed;
	if (active && totalBytes > 0)
		pillText += QStringLiteral(" • ") + FormatBytesCompact(totalBytes);

	timerPillLabel->setText(pillText);
	const QString recState = paused ? QStringLiteral("paused") : active ? QStringLiteral("recording") : QStringLiteral("idle");
	if (timerPillLabel->property("recState").toString() != recState) {
		timerPillLabel->setProperty("recState", recState);
		timerPillLabel->style()->unpolish(timerPillLabel);
		timerPillLabel->style()->polish(timerPillLabel);
	}

	recordBtn->setIcon(MakeVectorIcon(active ? IconKind::Stop : IconKind::Record, Qt::white));
	recordBtn->setText(active ? QStringLiteral("СТОП") : QStringLiteral("REC"));
	if (recordBtn->property("recording").toBool() != active) {
		recordBtn->setProperty("recording", active);
		recordBtn->style()->unpolish(recordBtn);
		recordBtn->style()->polish(recordBtn);
	}

	const bool canPause = active && main->isRecordingPausable;
	pauseBtn->setVisible(canPause);
	pauseBtn->setIcon(MakeVectorIcon(paused ? IconKind::Play : IconKind::Pause, QColor("#fde68a")));
	pauseBtn->setText(paused ? QStringLiteral("Продолж.") : QStringLiteral("Пауза"));

	// Sync Full Studio strip too
	studioRecordBtn->setIcon(MakeVectorIcon(active ? IconKind::Stop : IconKind::Record, Qt::white));
	studioRecordBtn->setText(active ? QStringLiteral("СТОП") : QStringLiteral("REC"));
	studioStatusLabel->setText(paused   ? QStringLiteral("Пауза ") + pillText
				   : active ? QStringLiteral("● Запись ") + pillText
					    : QStringLiteral("Готов к записи"));

	// Sync floating region frame if active
	if (regionFrame && regionFrame->isVisible()) {
		if (paused)
			regionFrame->SetState(BandicamRegionFrame::State::Paused, QStringLiteral("Ⅱ ПАУЗА ") + elapsed);
		else if (active)
			regionFrame->SetState(BandicamRegionFrame::State::Recording, QStringLiteral("● REC ") + pillText);
		else
			regionFrame->SetState(BandicamRegionFrame::State::Ready);
	}

	ApplyWindowTitle();

	if (active && !paused)
		CheckAutoStop(elapsedMs, totalBytes);
}

void BandicamRecorderWidget::CheckAutoStop(qint64 elapsedMs, uint64_t totalBytes)
{
	if (!autoStopEnabled || autoStopTriggered || autoStopValue <= 0)
		return;

	bool reached = false;
	if (autoStopType == 0) {
		const qint64 limitMs = static_cast<qint64>(autoStopValue) * 60LL * 1000LL;
		reached = (elapsedMs >= limitMs);
	} else {
		const uint64_t limitBytes = static_cast<uint64_t>(autoStopValue) * 1024ULL * 1024ULL;
		reached = (totalBytes >= limitBytes);
	}

	if (reached) {
		autoStopTriggered = true;
		ShowToast(QStringLiteral("Автоостановка записи выполнена"));
		main->StopRecording();
	}
}

void BandicamRecorderWidget::LoadVideoPageFromConfig()
{
	if (!main || !main->Config())
		return;

	syncingUi = true;
	config_t *cfg = main->Config();

	const bool recording = main->RecordingActive();
	videoLockedBanner->setVisible(recording);
	videoControlsContainer->setEnabled(!recording);

	// Populate hardware & software encoders
	encoderCombo->clear();
	encoderCombo->addItem(QStringLiteral("Программный (x264)"), QStringLiteral(SIMPLE_ENCODER_X264));
	encoderCombo->addItem(QStringLiteral("Программный (x264, низкая нагрузка CPU)"),
			      QStringLiteral(SIMPLE_ENCODER_X264_LOWCPU));
	if (EncoderAvailable("ffmpeg_nvenc"))
		encoderCombo->addItem(QStringLiteral("NVIDIA NVENC H.264 (GPU — рекомендуется)"),
				      QStringLiteral(SIMPLE_ENCODER_NVENC));
	if (EncoderAvailable("ffmpeg_hevc_nvenc"))
		encoderCombo->addItem(QStringLiteral("NVIDIA NVENC HEVC / H.265 (GPU)"),
				      QStringLiteral(SIMPLE_ENCODER_NVENC_HEVC));
	if (EncoderAvailable("obs_nvenc_av1_tex"))
		encoderCombo->addItem(QStringLiteral("NVIDIA NVENC AV1 (GPU)"),
				      QStringLiteral(SIMPLE_ENCODER_NVENC_AV1));
	if (EncoderAvailable("h264_texture_amf"))
		encoderCombo->addItem(QStringLiteral("AMD HW H.264 (GPU)"), QStringLiteral(SIMPLE_ENCODER_AMD));
	if (EncoderAvailable("h265_texture_amf"))
		encoderCombo->addItem(QStringLiteral("AMD HW HEVC (GPU)"), QStringLiteral(SIMPLE_ENCODER_AMD_HEVC));
	if (EncoderAvailable("av1_texture_amf"))
		encoderCombo->addItem(QStringLiteral("AMD HW AV1 (GPU)"), QStringLiteral(SIMPLE_ENCODER_AMD_AV1));
	if (EncoderAvailable("obs_qsv11"))
		encoderCombo->addItem(QStringLiteral("Intel QuickSync H.264 (GPU)"), QStringLiteral(SIMPLE_ENCODER_QSV));
	if (EncoderAvailable("obs_qsv11_av1"))
		encoderCombo->addItem(QStringLiteral("Intel QuickSync AV1 (GPU)"),
				      QStringLiteral(SIMPLE_ENCODER_QSV_AV1));

	auto selectByData = [](QComboBox *cb, const QString &val) {
		const int idx = cb->findData(val);
		if (idx >= 0)
			cb->setCurrentIndex(idx);
	};

	const char *fmt = config_get_string(cfg, "SimpleOutput", "RecFormat2");
	selectByData(formatCombo, fmt ? QString::fromUtf8(fmt) : QStringLiteral("hybrid_mp4"));

	const char *qual = config_get_string(cfg, "SimpleOutput", "RecQuality");
	selectByData(qualityCombo, qual ? QString::fromUtf8(qual) : QStringLiteral("Small"));

	const char *enc = config_get_string(cfg, "SimpleOutput", "RecEncoder");
	selectByData(encoderCombo, enc ? QString::fromUtf8(enc) : QStringLiteral(SIMPLE_ENCODER_X264));

	const uint32_t outW = static_cast<uint32_t>(config_get_uint(cfg, "Video", "OutputCX"));
	const uint32_t outH = static_cast<uint32_t>(config_get_uint(cfg, "Video", "OutputCY"));
	const uint32_t baseW = static_cast<uint32_t>(config_get_uint(cfg, "Video", "BaseCX"));
	const uint32_t baseH = static_cast<uint32_t>(config_get_uint(cfg, "Video", "BaseCY"));
	if (outW == baseW && outH == baseH)
		selectByData(resolutionCombo, QStringLiteral("native"));
	else
		selectByData(resolutionCombo, QStringLiteral("%1x%2").arg(outW).arg(outH));

	const char *fpsStr = config_get_string(cfg, "Video", "FPSCommon");
	selectByData(fpsCombo, fpsStr ? QString::fromUtf8(fpsStr) : QStringLiteral("60"));

	autoRemuxCheck->setChecked(config_get_bool(cfg, "Video", "AutoRemux"));
	UpdateCursorButtonState();

	syncingUi = false;
	UpdateFooterStatus();
}

void BandicamRecorderWidget::ApplyVideoSettingsToConfig()
{
	if (!main || !main->Config() || main->RecordingActive())
		return;

	config_t *cfg = main->Config();
	config_set_string(cfg, "Output", "Mode", "Simple");
	config_set_string(cfg, "SimpleOutput", "RecFormat2", QT_TO_UTF8(formatCombo->currentData().toString()));
	config_set_string(cfg, "SimpleOutput", "RecQuality", QT_TO_UTF8(qualityCombo->currentData().toString()));
	config_set_string(cfg, "SimpleOutput", "RecEncoder", QT_TO_UTF8(encoderCombo->currentData().toString()));
	config_set_bool(cfg, "Video", "AutoRemux", autoRemuxCheck->isChecked());

	const QString resVal = resolutionCombo->currentData().toString();
	uint32_t baseW = static_cast<uint32_t>(config_get_uint(cfg, "Video", "BaseCX"));
	uint32_t baseH = static_cast<uint32_t>(config_get_uint(cfg, "Video", "BaseCY"));
	if (baseW < 32 || baseH < 32) {
		baseW = 1920;
		baseH = 1080;
	}
	if (resVal == QStringLiteral("1920x1080")) {
		config_set_uint(cfg, "Video", "OutputCX", 1920);
		config_set_uint(cfg, "Video", "OutputCY", 1080);
	} else if (resVal == QStringLiteral("1280x720")) {
		config_set_uint(cfg, "Video", "OutputCX", 1280);
		config_set_uint(cfg, "Video", "OutputCY", 720);
	} else {
		config_set_uint(cfg, "Video", "OutputCX", baseW);
		config_set_uint(cfg, "Video", "OutputCY", baseH);
	}

	config_set_uint(cfg, "Video", "FPSType", 0);
	config_set_string(cfg, "Video", "FPSCommon", QT_TO_UTF8(fpsCombo->currentData().toString()));

	config_save_safe(cfg, "tmp", nullptr);
	main->ResetVideo();
	main->ResetOutputs();
	main->UpdateIsRecordingPausable();

	UpdateFooterStatus();
	ShowToast(QStringLiteral("Настройки видео применены"));
}

void BandicamRecorderWidget::LoadGeneralPageFromConfig()
{
	syncingUi = true;
	config_t *userCfg = App()->GetUserConfig();

	outputFolderEdit->setText(QDir::toNativeSeparators(ResolveOutputDirectory()));
	hideFromCaptureCheck->setChecked(config_get_bool(userCfg, "BasicWindow", "HideOBSWindowsFromCapture"));
	alwaysOnTopCheck->setChecked(config_get_bool(userCfg, "BasicWindow", "AlwaysOnTop"));
	minimizeOnRecordCheck->setChecked(minimizeOnRecord);
	showRegionFrameCheck->setChecked(showRegionFrame);
	startInBandicamCheck->setChecked(config_get_bool(userCfg, kConfigSection, "AlwaysStartBandicam"));

	autoStopCheck->setChecked(autoStopEnabled);
	autoStopTypeCombo->setCurrentIndex(std::clamp(autoStopType, 0, 1));
	autoStopValueSpin->setValue(std::max(1, autoStopValue));
	autoStopTypeCombo->setEnabled(autoStopEnabled);
	autoStopValueSpin->setEnabled(autoStopEnabled);

	syncingUi = false;
}

void BandicamRecorderWidget::RefreshAfterSettingsChanged()
{
	LoadVideoPageFromConfig();
	LoadGeneralPageFromConfig();
	SyncAudioControls();
	RefreshActiveSourceInfo();
	UpdateFooterStatus();
}

QString BandicamRecorderWidget::ResolveOutputDirectory() const
{
	if (!main || !main->Config())
		return QString();
	const char *mode = config_get_string(main->Config(), "Output", "Mode");
	const bool advanced = mode && strcmp(mode, "Advanced") == 0;
	const char *advType = config_get_string(main->Config(), "AdvOut", "RecType");
	const char *path = nullptr;
	if (advanced) {
		path = (advType && strcmp(advType, "Standard") != 0)
			       ? config_get_string(main->Config(), "AdvOut", "FFFilePath")
			       : config_get_string(main->Config(), "AdvOut", "RecFilePath");
	} else {
		path = config_get_string(main->Config(), "SimpleOutput", "FilePath");
	}
	return path ? QString::fromUtf8(path) : QString();
}

void BandicamRecorderWidget::RefreshRecordingsList()
{
	if (!recordingsList)
		return;

	recordingsList->clear();
	recOpenBtn->setEnabled(false);
	recRevealBtn->setEnabled(false);
	recRenameBtn->setEnabled(false);
	recCopyPathBtn->setEnabled(false);
	recDeleteBtn->setEnabled(false);

	const QString dirPath = ResolveOutputDirectory();
	if (dirPath.isEmpty()) {
		recordingsCountLabel->setText(QStringLiteral("0 файлов"));
		recordingsEmptyLabel->setText(
			QStringLiteral("Папка сохранения не настроена. Укажите её во вкладке «Основные»."));
		recordingsEmptyLabel->setVisible(true);
		return;
	}

	QDir dir(dirPath);
	if (!dir.exists()) {
		recordingsCountLabel->setText(QStringLiteral("0 файлов"));
		recordingsEmptyLabel->setText(QStringLiteral("Папка ещё не создана:\n%1").arg(dirPath));
		recordingsEmptyLabel->setVisible(true);
		return;
	}

	const QStringList videoFilters = {QStringLiteral("*.mp4"), QStringLiteral("*.mkv"),  QStringLiteral("*.mov"),
					  QStringLiteral("*.m4v"), QStringLiteral("*.ts"),   QStringLiteral("*.flv"),
					  QStringLiteral("*.avi"), QStringLiteral("*.webm"), QStringLiteral("*.wmv")};
	const QStringList imageFilters = {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
					  QStringLiteral("*.bmp"), QStringLiteral("*.webp")};

	QStringList filters;
	if (recordingsFilterMode == 0)
		filters = videoFilters;
	else if (recordingsFilterMode == 1)
		filters = imageFilters;
	else
		filters = videoFilters + imageFilters;

	const QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Time);
	int added = 0;
	for (const QFileInfo &info : files) {
		if (added >= 60)
			break;
		const QString ext = info.suffix().toLower();
		const bool isImg = (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") ||
				    ext == QStringLiteral("jpeg") || ext == QStringLiteral("bmp") ||
				    ext == QStringLiteral("webp"));
		const QString sizeStr = FormatBytesCompact(static_cast<uint64_t>(std::max<qint64>(0, info.size())));

		auto *item = new QListWidgetItem(
			QStringLiteral("%1\n%2 • %3")
				.arg(info.fileName(), info.lastModified().toString(QStringLiteral("dd.MM HH:mm")),
				     sizeStr),
			recordingsList);
		item->setIcon(QIcon(QPixmap::fromImage(CreateMediaPlaceholder(ext, isImg))));
		item->setData(Qt::UserRole, info.absoluteFilePath());
		item->setToolTip(info.absoluteFilePath());
		item->setTextAlignment(Qt::AlignHCenter);
		item->setSizeHint(QSize(176, 138));

		auto listGuard = std::make_shared<QPointer<QListWidget>>(recordingsList);
		const QString filePath = info.absoluteFilePath();
		BandicamThumbnailPool().start(
			QRunnable::create([this, listGuard, filePath, isImg]() {
				QImage thumb = LoadMediaThumbnail(filePath, isImg);
				if (thumb.isNull())
					return;
				QMetaObject::invokeMethod(
					this,
					[listGuard, filePath, thumb]() {
						if (listGuard->isNull())
							return;
						QListWidget *target = listGuard->data();
						for (int row = 0; row < target->count(); ++row) {
							QListWidgetItem *cur = target->item(row);
							if (cur && cur->data(Qt::UserRole).toString() == filePath) {
								cur->setIcon(QIcon(QPixmap::fromImage(thumb)));
								break;
							}
						}
					},
					Qt::QueuedConnection);
			}),
			QThread::LowPriority);
		++added;
	}

	recordingsCountLabel->setText(QStringLiteral("%1 файл(ов)").arg(added));
	recordingsEmptyLabel->setText(added == 0 ? QStringLiteral("В папке пока нет файлов выбранного типа.") : QString());
	recordingsEmptyLabel->setVisible(added == 0);
}

void BandicamRecorderWidget::OpenSelectedRecording()
{
	QListWidgetItem *item = recordingsList ? recordingsList->currentItem() : nullptr;
	if (!item)
		return;
	const QString path = item->data(Qt::UserRole).toString();
	if (QFileInfo::exists(path))
		QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void BandicamRecorderWidget::RevealSelectedRecording()
{
	QListWidgetItem *item = recordingsList ? recordingsList->currentItem() : nullptr;
	if (!item)
		return;
	const QString path = QDir::toNativeSeparators(item->data(Qt::UserRole).toString());
	if (!QFileInfo::exists(path))
		return;
#ifdef _WIN32
	QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + path});
#else
	QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void BandicamRecorderWidget::RenameSelectedRecording()
{
	QListWidgetItem *item = recordingsList ? recordingsList->currentItem() : nullptr;
	if (!item)
		return;
	const QString oldPath = item->data(Qt::UserRole).toString();
	const QFileInfo fi(oldPath);
	if (!fi.exists())
		return;

	bool ok = false;
	const QString newBase = QInputDialog::getText(this, QStringLiteral("Переименовать файл"),
						      QStringLiteral("Новое имя (без расширения):"), QLineEdit::Normal,
						      fi.completeBaseName(), &ok);
	if (!ok || newBase.trimmed().isEmpty())
		return;

	const QString newPath = fi.absolutePath() + QLatin1Char('/') + newBase.trimmed() + QLatin1Char('.') + fi.suffix();
	if (QFile::rename(oldPath, newPath)) {
		RefreshRecordingsList();
		ShowToast(QStringLiteral("Файл переименован"));
	} else {
		ShowToast(QStringLiteral("Не удалось переименовать файл"), true);
	}
}

void BandicamRecorderWidget::DeleteSelectedRecording()
{
	QListWidgetItem *item = recordingsList ? recordingsList->currentItem() : nullptr;
	if (!item)
		return;
	const QString path = item->data(Qt::UserRole).toString();
	const QFileInfo fi(path);
	if (!fi.exists())
		return;

	const auto res = QMessageBox::question(
		this, QStringLiteral("Удалить файл"),
		QStringLiteral("Удалить «%1»?\nФайл будет удалён с диска.").arg(fi.fileName()),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (res != QMessageBox::Yes)
		return;

	if (QFile::moveToTrash(path) || QFile::remove(path)) {
		RefreshRecordingsList();
		UpdateFooterStatus();
		ShowToast(QStringLiteral("Файл удалён"));
	} else {
		ShowToast(QStringLiteral("Не удалось удалить файл"), true);
	}
}

void BandicamRecorderWidget::CopySelectedRecordingPath()
{
	QListWidgetItem *item = recordingsList ? recordingsList->currentItem() : nullptr;
	if (!item)
		return;
	const QString path = QDir::toNativeSeparators(item->data(Qt::UserRole).toString());
	if (QClipboard *cb = QGuiApplication::clipboard()) {
		cb->setText(path);
		ShowToast(QStringLiteral("Путь скопирован в буфер"));
	}
}

void BandicamRecorderWidget::UpdateFooterStatus()
{
	if (!main || !main->Config())
		return;

	config_t *cfg = main->Config();
	const uint32_t outW = static_cast<uint32_t>(config_get_uint(cfg, "Video", "OutputCX"));
	const uint32_t outH = static_cast<uint32_t>(config_get_uint(cfg, "Video", "OutputCY"));
	const char *fps = config_get_string(cfg, "Video", "FPSCommon");
	const char *fmt = config_get_string(cfg, "SimpleOutput", "RecFormat2");
	QString fmtLabel = fmt ? QString::fromUtf8(fmt).toUpper() : QStringLiteral("MP4");
	fmtLabel.replace(QStringLiteral("HYBRID_"), QString());
	fmtLabel.replace(QStringLiteral("FRAGMENTED_"), QString());

	const QString spec = QStringLiteral("%1×%2 • %3 FPS • %4")
				     .arg(outW > 0 ? outW : 1920)
				     .arg(outH > 0 ? outH : 1080)
				     .arg(fps ? QString::fromUtf8(fps) : QStringLiteral("60"))
				     .arg(fmtLabel);
	footerSpecLabel->setText(spec);

	const QString dir = ResolveOutputDirectory();
	QString freeStr;
	if (!dir.isEmpty()) {
		const uint64_t freeBytes = os_get_free_disk_space(QT_TO_UTF8(dir));
		if (freeBytes > 0)
			freeStr = QStringLiteral("Свободно: %1").arg(FormatBytesCompact(freeBytes));
	}
	footerDiskLabel->setText(freeStr);

	if (homeSummaryLabel) {
		homeSummaryLabel->setText(
			QStringLiteral("Текущий профиль: %1   |   Папка: %2")
				.arg(spec, QDir::toNativeSeparators(dir.isEmpty() ? QStringLiteral("не задана") : dir)));
	}
}

void BandicamRecorderWidget::ShowToast(const QString &message, bool isError)
{
	if (!footerToastLabel)
		return;
	footerToastLabel->setStyleSheet(isError ? QStringLiteral("color: #f87171; font-size: 12px; font-weight: 600;")
						: QStringLiteral("color: #4ade80; font-size: 12px; font-weight: 600;"));
	footerToastLabel->setText(message);
	footerToastLabel->setVisible(true);
	if (toastTimer)
		toastTimer->start(3500);
}
