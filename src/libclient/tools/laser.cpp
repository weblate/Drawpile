// SPDX-License-Identifier: GPL-3.0-or-later

#include "libclient/drawdance/message.h"
#include "libclient/net/client.h"

#include "libclient/tools/toolcontroller.h"
#include "libclient/tools/laser.h"

#include <QPixmap>

namespace tools {

LaserPointer::LaserPointer(ToolController &owner)
	: Tool(owner, LASERPOINTER, QCursor(QPixmap(":cursors/arrow.png"), 0, 0), false, true, false),
	m_persistence(1), m_drawing(false)
{}

void LaserPointer::begin(const canvas::Point &point, bool right, float zoom)
{
	Q_UNUSED(zoom);
	Q_ASSERT(!m_drawing);
	if(right) {
		return;
	}

	m_drawing = true;

	net::Client *client = m_owner.client();
	uint8_t contextId = client->myId();
	uint32_t color = m_owner.activeBrush().qColor().rgb();
	drawdance::Message messages[] = {
		drawdance::Message::makeLaserTrail(contextId, color, m_persistence),
		drawdance::Message::makeMovePointer(contextId, point.x() * 4, point.y() * 4),
	};
	client->sendMessages(DP_ARRAY_LENGTH(messages), messages);
}

void LaserPointer::motion(const canvas::Point &point, bool constrain, bool center)
{
	Q_UNUSED(constrain);
	Q_UNUSED(center);
	if(m_drawing) {
		m_owner.client()->sendMessage(drawdance::Message::makeMovePointer(
			m_owner.client()->myId(), point.x() * 4, point.y() * 4));
	}
}

void LaserPointer::end()
{
	if(m_drawing) {
		m_drawing = false;
		m_owner.client()->sendMessage(drawdance::Message::makeLaserTrail(
			m_owner.client()->myId(), 0, 0));
	}
}

}

