// SPDX-License-Identifier: GPL-3.0-or-later
#include "libclient/tools/zoom.h"
#include "libclient/tools/toolcontroller.h"
#include "libclient/utils/cursors.h"
#include <QPainterPath>

namespace tools {

ZoomTool::ZoomTool(ToolController &owner)
	: Tool(
		  owner, ZOOM, utils::Cursors::zoom(),
		  Capability::AllowColorPick | Capability::HandlesRightClick |
			  Capability::SendsNoMessages)
{
}

void ZoomTool::begin(const BeginParams &params)
{
	m_scrubbing = m_scrub;
	m_reverse = params.right;
	if(m_scrubbing) {
		m_scrubPos = params.point;
		m_scrubLast = params.viewPos;
	} else {
		m_clickDetector.begin(params.viewPos, params.deviceType);
		m_start = params.point.toPoint();
		m_end = m_start;
	}
}

void ZoomTool::motion(const MotionParams &params)
{
	if(m_scrubbing) {
		QPointF viewPos = params.viewPos;
		qreal deltaY = m_scrubLast.y() - viewPos.y();
		Q_EMIT m_owner.scrubZoomRequested(
			m_reverse ? -deltaY : deltaY, m_scrubPos);
		m_scrubLast = viewPos;
	} else {
		m_clickDetector.motion(params.viewPos);
		m_end = params.point.toPoint();
		updatePreview();
	}
}

void ZoomTool::end(const EndParams &)
{
	if(m_scrubbing) {
		m_scrubbing = false;
	} else {
		m_clickDetector.end();
		removePreview();
		constexpr int STEPS = 3;
		emit m_owner.zoomRequested(
			m_clickDetector.isClick() ? getCenterRect() : getRect(),
			m_reverse ? -STEPS : STEPS);
	}
}

void ZoomTool::setScrub(bool scrub)
{
	if(scrub != m_scrub) {
		m_scrub = scrub;
		setCapability(Capability::Fractional, scrub);
	}
}

void ZoomTool::updatePreview() const
{
	QRect rect = getRect();
	if(rect.isEmpty()) {
		removePreview();
	} else {
		QPainterPath path;
		path.addRect(rect);
		emit m_owner.pathPreviewRequested(path);
	}
}

void ZoomTool::removePreview() const
{
	emit m_owner.pathPreviewRequested(QPainterPath());
}

QRect ZoomTool::getRect() const
{
	if(m_start == m_end) {
		return QRect(m_start, m_end);
	} else {
		return QRect(
			QPoint(qMin(m_start.x(), m_end.x()), qMin(m_start.y(), m_end.y())),
			QPoint(qMax(m_start.x(), m_end.x()), qMax(m_start.y(), m_end.y())));
	}
}

QRect ZoomTool::getCenterRect() const
{
	QPoint center = getRect().center();
	return QRect(center, center);
}

}
