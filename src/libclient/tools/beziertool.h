// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOOLS_BEZIER_H
#define TOOLS_BEZIER_H

#include "libclient/tools/tool.h"
#include "libclient/drawdance/brushengine.h"

namespace tools {

/**
 * \brief A bezier curve tool
 */
class BezierTool final : public Tool {
public:
	BezierTool(ToolController &owner);

	void begin(const canvas::Point& point, bool right, float zoom) override;
	void motion(const canvas::Point& point, bool constrain, bool center) override;
	void hover(const QPointF& point) override;
	void end() override;
	void finishMultipart() override;
	void cancelMultipart() override;
	void undoMultipart() override;
	bool isMultipart() const override { return !m_points.isEmpty(); }

private:
	void updatePreview();
	canvas::PointVector calculateBezierCurve() const;

	struct ControlPoint {
		QPointF point;
		QPointF cp; // second control point, relative to the main point
	};

	drawdance::BrushEngine m_brushEngine;
	QVector<ControlPoint> m_points;
	QPointF m_beginPoint;
	bool m_rightButton;
	float m_zoom;
};

}

#endif

