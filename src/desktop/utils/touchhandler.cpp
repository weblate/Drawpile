// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" {
#include <dpcommon/event_log.h>
}
#include "desktop/main.h"
#include "desktop/utils/qtguicompat.h"
#include "desktop/utils/touchhandler.h"
#include "libshared/util/qtcompat.h"
#include <QDateTime>
#include <QGestureEvent>
#include <QTimer>

TouchHandler::TouchHandler(QObject *parent)
	: QObject(parent)
	, m_oneFingerTouchAction(int(ONE_FINGER_TOUCH_DEFAULT))
	, m_twoFingerPinchAction(int(desktop::settings::TwoFingerPinchAction::Zoom))
	, m_twoFingerTwistAction(
		  int(desktop::settings::TwoFingerTwistAction::Rotate))
	, m_oneFingerTapAction(int(desktop::settings::TouchTapAction::Nothing))
	, m_twoFingerTapAction(int(desktop::settings::TouchTapAction::Undo))
	, m_threeFingerTapAction(int(desktop::settings::TouchTapAction::Redo))
	, m_fourFingerTapAction(int(desktop::settings::TouchTapAction::HideDocks))
	, m_oneFingerTapAndHoldAction(
		  int(desktop::settings::TouchTapAndHoldAction::ColorPickMode))
	, m_tapTimer(Qt::CoarseTimer)
	, m_tapAndHoldTimer(new QTimer(this))
{
	m_tapAndHoldTimer->setTimerType(Qt::CoarseTimer);
	m_tapAndHoldTimer->setSingleShot(true);
	m_tapAndHoldTimer->setInterval(TAP_AND_HOLD_DELAY_MS);
	connect(
		m_tapAndHoldTimer, &QTimer::timeout, this,
		&TouchHandler::triggerTapAndHold);

	desktop::settings::Settings &settings = dpApp().settings();
	settings.bindOneFingerTouch(this, &TouchHandler::setOneFingerTouchAction);
	settings.bindTwoFingerPinch(this, &TouchHandler::setTwoFingerPinchAction);
	settings.bindTwoFingerTwist(this, &TouchHandler::setTwoFingerTwistAction);
	settings.bindOneFingerTap(this, &TouchHandler::setOneFingerTapAction);
	settings.bindTwoFingerTap(this, &TouchHandler::setTwoFingerTapAction);
	settings.bindThreeFingerTap(this, &TouchHandler::setThreeFingerTapAction);
	settings.bindFourFingerTap(this, &TouchHandler::setFourFingerTapAction);
	settings.bindOneFingerTapAndHold(
		this, &TouchHandler::setOneFingerTapAndHoldAction);
}

bool TouchHandler::isTouchDrawEnabled() const
{
	switch(m_oneFingerTouchAction) {
	case int(desktop::settings::OneFingerTouchAction::Draw):
		return true;
	case int(desktop::settings::OneFingerTouchAction::Guess):
		return !m_anyTabletEventsReceived;
	default:
		return false;
	}
}

bool TouchHandler::isTouchPanEnabled() const
{
	switch(m_oneFingerTouchAction) {
	case int(desktop::settings::OneFingerTouchAction::Pan):
		return false;
	case int(desktop::settings::OneFingerTouchAction::Guess):
		return m_anyTabletEventsReceived;
	default:
		return false;
	}
}

bool TouchHandler::isTouchDrawOrPanEnabled() const
{
	switch(m_oneFingerTouchAction) {
	case int(desktop::settings::OneFingerTouchAction::Pan):
	case int(desktop::settings::OneFingerTouchAction::Draw):
	case int(desktop::settings::OneFingerTouchAction::Guess):
		return true;
	default:
		return false;
	}
}

bool TouchHandler::isTouchPinchEnabled() const
{
	return m_twoFingerPinchAction !=
		   int(desktop::settings::TwoFingerPinchAction::Nothing);
}

bool TouchHandler::isTouchTwistEnabled() const
{
	return m_twoFingerTwistAction !=
		   int(desktop::settings::TwoFingerTwistAction::Nothing);
}

bool TouchHandler::isTouchPinchOrTwistEnabled() const
{
	return isTouchPinchEnabled() || isTouchTwistEnabled();
}

void TouchHandler::handleTouchBegin(QTouchEvent *event)
{
	const QList<compat::TouchPoint> &points = compat::touchPoints(*event);
	int pointsCount = points.size();
	QPointF posf = compat::touchPos(points.first());

	m_touchPos = QPointF(0.0, 0.0);
	for(const compat::TouchPoint &tp : compat::touchPoints(*event)) {
		m_touchPos += compat::touchPos(tp);
	}
	m_touchPos /= pointsCount;

	m_touchDrawBuffer.clear();
	m_touchDragging = false;
	m_touchRotating = false;
	m_touchHeld = false;
	m_maxTouchPoints = pointsCount;
	m_tapTimer.setRemainingTime(TAP_MAX_DELAY_MS);
	if(isTouchDrawEnabled() && pointsCount == 1 && !compat::isTouchPad(event)) {
		DP_EVENT_LOG(
			"touch_draw_begin x=%f y=%f touching=%d type=%d device=%s "
			"points=%s timestamp=%llu",
			posf.x(), posf.y(), int(m_touching), compat::touchDeviceType(event),
			qUtf8Printable(compat::touchDeviceName(event)),
			qUtf8Printable(compat::debug(points)),
			qulonglong(event->timestamp()));
		if(isTouchPanEnabled() || isTouchPinchOrTwistEnabled() ||
		   m_oneFingerTapAction !=
			   int(desktop::settings::TouchTapAction::Nothing) ||
		   m_oneFingerTapAndHoldAction !=
			   int(desktop::settings::TouchTapAndHoldAction::Nothing)) {
			// Buffer the touch first, since it might end up being the
			// beginning of an action that involves multiple fingers.
			m_touchDrawBuffer.append(
				{QDateTime::currentMSecsSinceEpoch(), posf});
			m_touchMode = TouchMode::Unknown;
		} else {
			// There's no other actions other than drawing enabled, so we
			// can just start drawing without awaiting what happens next.
			m_touchMode = TouchMode::Drawing;
			emit touchPressed(event, QDateTime::currentMSecsSinceEpoch(), posf);
		}
	} else {
		DP_EVENT_LOG(
			"touch_begin touching=%d type=%d device=%s points=%s "
			"timestamp=%llu",
			int(m_touching), compat::touchDeviceType(event),
			qUtf8Printable(compat::touchDeviceName(event)),
			qUtf8Printable(compat::debug(points)),
			qulonglong(event->timestamp()));
		m_touchMode = TouchMode::Moving;
	}

	if(pointsCount == 1 && m_touchMode != TouchMode::Drawing &&
	   m_oneFingerTapAndHoldAction !=
		   int(desktop::settings::TouchTapAndHoldAction::Nothing)) {
		m_tapAndHoldTimer->start();
	} else {
		m_tapAndHoldTimer->stop();
	}
}

void TouchHandler::handleTouchUpdate(
	QTouchEvent *event, qreal zoom, qreal rotation, qreal dpr)
{
	const QList<compat::TouchPoint> &points = compat::touchPoints(*event);
	int pointsCount = points.size();
	if(pointsCount > m_maxTouchPoints) {
		m_maxTouchPoints = pointsCount;
	}

	if(pointsCount != 1) {
		m_tapAndHoldTimer->stop();
	}

	m_touchPos = QPointF(0.0, 0.0);
	for(const compat::TouchPoint &tp : compat::touchPoints(*event)) {
		m_touchPos += compat::touchPos(tp);
	}
	m_touchPos /= pointsCount;

	if(m_touchHeld) {
		emit touchColorPicked(m_touchPos);
	} else if(
		isTouchDrawEnabled() &&
		((pointsCount == 1 && m_touchMode == TouchMode::Unknown) ||
		 m_touchMode == TouchMode::Drawing) &&
		!compat::isTouchPad(event)) {
		QPointF posf = compat::touchPos(compat::touchPoints(*event).first());
		DP_EVENT_LOG(
			"touch_draw_update x=%f y=%f touching=%d type=%d device=%s "
			"points=%s timestamp=%llu",
			posf.x(), posf.y(), int(m_touching), compat::touchDeviceType(event),
			qUtf8Printable(compat::touchDeviceName(event)),
			qUtf8Printable(compat::debug(points)),
			qulonglong(event->timestamp()));
		int bufferCount = m_touchDrawBuffer.size();
		if(bufferCount == 0) {
			if(m_touchMode == TouchMode::Drawing) {
				emit touchMoved(QDateTime::currentMSecsSinceEpoch(), posf);
			} else { // Shouldn't happen, but we'll deal with it anyway.
				m_tapAndHoldTimer->stop();
				m_touchMode = TouchMode::Drawing;
				emit touchPressed(
					event, QDateTime::currentMSecsSinceEpoch(), posf);
			}
		} else {
			// This still might be the beginning of a multitouch operation.
			// If the finger didn't move enough of a distance and we didn't
			// buffer an excessive amount of touches yet. Buffer the touched
			// point and wait a bit more as to what's going to happen.
			bool shouldAppend =
				bufferCount < DRAW_BUFFER_COUNT &&
				squareDist(m_touchDrawBuffer.first().second - posf) <
					TAP_SLOP_SQUARED;
			if(shouldAppend) {
				m_touchDrawBuffer.append(
					{QDateTime::currentMSecsSinceEpoch(), posf});
			} else {
				m_tapAndHoldTimer->stop();
				m_touchMode = TouchMode::Drawing;
				flushTouchDrawBuffer();
				emit touchMoved(QDateTime::currentMSecsSinceEpoch(), posf);
			}
		}
	} else {
		m_touchMode = TouchMode::Moving;

		QPointF startCenter, lastCenter;
		for(const compat::TouchPoint &tp : compat::touchPoints(*event)) {
			QPointF startPos = compat::touchStartPos(tp);
			startCenter += startPos;
			lastCenter += compat::touchLastPos(tp);
			// This might be a tap gesture. Don't start a drag until there's
			// been sufficient movement on any of the fingers.
			if(!m_touchDragging &&
			   squareDist(startPos - compat::touchPos(tp)) > TAP_SLOP_SQUARED) {
				m_touchDragging = true;
			}
		}

		if(!m_touchDragging) {
			if(m_tapTimer.hasExpired()) {
				// The user has had their fingers down for at least a second,
				// this is clearly no longer a tap, but some longer gesture.
				m_touchDragging = true;
			} else {
				// Maybe still a tap gesture, wait for more movement.
				return;
			}
		}

		m_tapAndHoldTimer->stop();
		startCenter /= pointsCount;
		lastCenter /= pointsCount;
		QPointF center = m_touchPos;

		DP_EVENT_LOG(
			"touch_update x=%f y=%f touching=%d type=%d device=%s "
			"points=%s "
			"timestamp=%llu",
			center.x(), center.y(), int(m_touching),
			compat::touchDeviceType(event),
			qUtf8Printable(compat::touchDeviceName(event)),
			qUtf8Printable(compat::debug(points)),
			qulonglong(event->timestamp()));

		if(!m_touching) {
			m_touchStartZoom = zoom;
			m_touchStartRotate = rotation;
		}

		// We want to pan with one finger if one-finger pan is enabled and
		// also when pinching to zoom. Slightly non-obviously, we also want
		// to pan with one finger when finger drawing is enabled, because if
		// we got here with one finger, we've come out of a multitouch
		// operation and aren't going to be drawing until all fingers leave
		// the surface anyway, so panning is the only sensible option.
		bool haveMultiTouch = pointsCount >= 2;
		bool havePinchOrTwist = haveMultiTouch && isTouchPinchOrTwistEnabled();
		bool havePan = havePinchOrTwist ||
					   (isTouchDrawOrPanEnabled() &&
						(haveMultiTouch || !compat::isTouchPad(event)));
		if(havePan) {
			m_touching = true;
			qreal dx = center.x() - lastCenter.x();
			qreal dy = center.y() - lastCenter.y();
			emit touchScrolledBy(-dx, -dy);
		}

		// Scaling and rotation with two fingers
		if(havePinchOrTwist) {
			m_touching = true;
			qreal startAvgDist = 0.0;
			qreal avgDist = 0.0;
			for(const compat::TouchPoint &tp : compat::touchPoints(*event)) {
				startAvgDist +=
					squareDist(compat::touchStartPos(tp) - startCenter);
				avgDist += squareDist(compat::touchPos(tp) - center);
			}
			startAvgDist = sqrt(startAvgDist);

			qreal touchZoom = zoom;
			if(isTouchPinchEnabled()) {
				avgDist = sqrt(avgDist);
				qreal dZoom = avgDist / startAvgDist;
				touchZoom = m_touchStartZoom * dZoom;
			}

			qreal touchRotation = rotation;
			if(isTouchTwistEnabled()) {
				QLineF l1(
					compat::touchStartPos(points.first()),
					compat::touchStartPos(points.last()));
				QLineF l2(
					compat::touchPos(points.first()),
					compat::touchPos(points.last()));
				qreal dAngle = l1.angle() - l2.angle();

				// Require a small nudge to activate rotation to avoid
				// rotating when the user just wanted to zoom. Also only
				// rotate when touch points start out far enough from each
				// other. Initial angle measurement is inaccurate when
				// touchpoints are close together.
				if(startAvgDist * dpr > 80.0 &&
				   (qAbs(dAngle) > 3.0 || m_touchRotating)) {
					m_touchRotating = true;
					touchRotation =
						adjustTwistRotation(m_touchStartRotate + dAngle);
				}
			}

			emit touchZoomedRotated(touchZoom, touchRotation);
		}
	}
}

void TouchHandler::handleTouchEnd(QTouchEvent *event, bool cancel)
{
	event->accept();
	const QList<compat::TouchPoint> &points = compat::touchPoints(*event);
	m_tapAndHoldTimer->stop();
	if(m_touchHeld) {
		emit touchColorPickFinished();
	} else if(
		isTouchDrawEnabled() &&
		((m_touchMode == TouchMode::Unknown && !m_touchDrawBuffer.isEmpty() &&
		  (m_touchDragging ||
		   m_oneFingerTapAction ==
			   int(desktop::settings::TouchTapAction::Nothing))) ||
		 m_touchMode == TouchMode::Drawing)) {
		DP_EVENT_LOG(
			"touch_draw_%s touching=%d type=%d device=%s points=%s "
			"timestamp=%llu",
			cancel ? "cancel" : "end", int(m_touching),
			compat::touchDeviceType(event),
			qUtf8Printable(compat::touchDeviceName(event)),
			qUtf8Printable(compat::debug(points)),
			qulonglong(event->timestamp()));
		flushTouchDrawBuffer();
		emit touchReleased(
			QDateTime::currentMSecsSinceEpoch(),
			compat::touchPos(compat::touchPoints(*event).first()));
	} else if(m_touchDragging) {
		DP_EVENT_LOG(
			"touch_%s touching=%d type=%d device=%s points=%s timestamp=%llu",
			cancel ? "cancel" : "end", int(m_touching),
			compat::touchDeviceType(event),
			qUtf8Printable(compat::touchDeviceName(event)),
			qUtf8Printable(compat::debug(points)),
			qulonglong(event->timestamp()));
	} else {
		DP_EVENT_LOG(
			"touch_tap_%s maxTouchPoints=%d touching=%d type=%d device=%s "
			"points=%s timestamp=%llu",
			cancel ? "cancel" : "end", m_maxTouchPoints, int(m_touching),
			compat::touchDeviceType(event),
			qUtf8Printable(compat::touchDeviceName(event)),
			qUtf8Printable(compat::debug(points)),
			qulonglong(event->timestamp()));
		if(!cancel) {
			if(m_maxTouchPoints == 1) {
				emitTapAction(m_oneFingerTapAction);
			} else if(m_maxTouchPoints == 2) {
				emitTapAction(m_twoFingerTapAction);
			} else if(m_maxTouchPoints == 3) {
				emitTapAction(m_threeFingerTapAction);
			} else if(m_maxTouchPoints >= 4) {
				emitTapAction(m_fourFingerTapAction);
			}
		}
	}
	m_touching = false;
}

void TouchHandler::handleGesture(
	QGestureEvent *event, qreal zoom, qreal rotation)
{
	const QTapGesture *tap =
		static_cast<const QTapGesture *>(event->gesture(Qt::TapGesture));
	if(tap) {
		Qt::GestureState tapState = tap->state();
		DP_EVENT_LOG(
			"tap state=0x%x touching=%d", unsigned(tapState), int(m_touching));
		if(tapState == Qt::GestureFinished) {
			emitTapAction(m_oneFingerTapAction);
		}
	}

	const QPinchGesture *pinch =
		static_cast<const QPinchGesture *>(event->gesture(Qt::PinchGesture));
	bool hadPinchUpdate = false;
	if(pinch) {
		Qt::GestureState pinchState = pinch->state();
		QPinchGesture::ChangeFlags cf = pinch->changeFlags();
		DP_EVENT_LOG(
			"pinch state=0x%x change=0x%x scale=%f rotation=%f touching=%d",
			unsigned(pinchState), unsigned(cf), pinch->totalScaleFactor(),
			pinch->totalRotationAngle(), int(m_touching));

		switch(pinchState) {
		case Qt::GestureStarted:
			m_gestureStartZoom = zoom;
			m_gestureStartRotation = rotation;
			Q_FALLTHROUGH();
		case Qt::GestureUpdated:
		case Qt::GestureFinished: {
			if(isTouchDrawOrPanEnabled() &&
			   cf.testFlag(QPinchGesture::CenterPointChanged)) {
				QPointF d = pinch->centerPoint() - pinch->lastCenterPoint();
				emit touchScrolledBy(-d.x(), -d.y());
			}

			bool haveZoom = isTouchPinchEnabled() &&
							cf.testFlag(QPinchGesture::ScaleFactorChanged);
			bool haveRotation =
				isTouchTwistEnabled() &&
				cf.testFlag(QPinchGesture::RotationAngleChanged);
			if(haveZoom || haveRotation) {
				qreal gestureZoom =
					haveZoom ? m_gestureStartZoom * pinch->totalScaleFactor()
							 : m_gestureStartZoom;
				qreal gestureRotation = haveRotation
											? adjustTwistRotation(
												  m_gestureStartRotation +
												  pinch->totalRotationAngle())
											: m_gestureStartRotation;
				emit touchZoomedRotated(gestureZoom, gestureRotation);
			}

			hadPinchUpdate = true;
			Q_FALLTHROUGH();
		}
		case Qt::GestureCanceled:
			event->accept();
			break;
		default:
			break;
		}
	}

	const QPanGesture *pan =
		static_cast<const QPanGesture *>(event->gesture(Qt::PanGesture));
	if(pan) {
		Qt::GestureState panState = pan->state();
		QPointF delta = pan->delta();
		DP_EVENT_LOG(
			"pan state=0x%x dx=%f dy=%f touching=%d", unsigned(panState),
			delta.x(), delta.y(), int(m_touching));

		switch(panState) {
		case Qt::GestureStarted:
			m_gestureStartZoom = zoom;
			m_gestureStartRotation = rotation;
			Q_FALLTHROUGH();
		case Qt::GestureUpdated:
		case Qt::GestureFinished:
			if(!hadPinchUpdate && isTouchDrawOrPanEnabled()) {
				emit touchScrolledBy(delta.x() / -2.0, delta.y() / -2.0);
			}
			Q_FALLTHROUGH();
		case Qt::GestureCanceled:
			event->accept();
			break;
		default:
			break;
		}
	}
}

void TouchHandler::setOneFingerTouchAction(int oneFingerTouchAction)
{
	m_oneFingerTouchAction = oneFingerTouchAction;
}

void TouchHandler::setTwoFingerPinchAction(int twoFingerPinchAction)
{
	m_twoFingerPinchAction = twoFingerPinchAction;
}

void TouchHandler::setTwoFingerTwistAction(int twoFingerTwistAction)
{
	m_twoFingerTwistAction = twoFingerTwistAction;
}

void TouchHandler::setOneFingerTapAction(int oneFingerTapAction)
{
	m_oneFingerTapAction = oneFingerTapAction;
}

void TouchHandler::setTwoFingerTapAction(int twoFingerTapAction)
{
	m_twoFingerTapAction = twoFingerTapAction;
}

void TouchHandler::setThreeFingerTapAction(int threeFingerTapAction)
{
	m_threeFingerTapAction = threeFingerTapAction;
}

void TouchHandler::setFourFingerTapAction(int fourFingerTapAction)
{
	m_fourFingerTapAction = fourFingerTapAction;
}

void TouchHandler::setOneFingerTapAndHoldAction(int oneFingerTapAndHoldAction)
{
	m_oneFingerTapAndHoldAction = oneFingerTapAndHoldAction;
}

void TouchHandler::triggerTapAndHold()
{
	m_tapAndHoldTimer->stop();
	if(m_maxTouchPoints == 1 && m_touchMode != TouchMode::Drawing) {
		switch(m_oneFingerTapAndHoldAction) {
		case int(desktop::settings::TouchTapAndHoldAction::ColorPickMode):
			m_touchHeld = true;
			emit touchColorPicked(m_touchPos);
			break;
		default:
			qWarning(
				"Unknown one finger tap and hold action %d",
				m_oneFingerTapAndHoldAction);
			break;
		}
	}
}

qreal TouchHandler::adjustTwistRotation(qreal degrees) const
{
	if(m_twoFingerTwistAction ==
	   int(desktop::settings::TwoFingerTwistAction::Rotate)) {
		return qAbs(std::fmod(degrees, 360.0)) < 5.0 ? 0.0 : degrees;
	} else if(
		m_twoFingerTwistAction ==
		int(desktop::settings::TwoFingerTwistAction::RotateDiscrete)) {
		qreal step = 15.0;
		qreal offset = std::fmod(degrees, step);
		if(offset <= step / 2.0) {
			return qFloor(degrees / step) * step;
		} else {
			return qCeil(degrees / step) * step;
		}
	} else {
		return degrees;
	}
}

void TouchHandler::flushTouchDrawBuffer()
{
	int bufferCount = m_touchDrawBuffer.size();
	if(bufferCount != 0) {
		const QPair<long long, QPointF> &press = m_touchDrawBuffer.first();
		emit touchPressed(nullptr, press.first, press.second);
		for(int i = 0; i < bufferCount; ++i) {
			const QPair<long long, QPointF> &move = m_touchDrawBuffer[i];
			emit touchMoved(move.first, move.second);
		}
		m_touchDrawBuffer.clear();
	}
}

void TouchHandler::emitTapAction(int action)
{
	if(action != int(desktop::settings::TouchTapAction::Nothing)) {
		emit touchTapActionActivated(action);
	}
}