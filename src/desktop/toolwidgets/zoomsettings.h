// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TOOLSETTINGS_ZOOM_H
#define TOOLSETTINGS_ZOOM_H
#include "desktop/toolwidgets/toolsettings.h"

class QButtonGroup;

namespace widgets {
class GroupedToolButton;
}

namespace tools {

class ZoomSettings final : public ToolSettings {
	Q_OBJECT
public:
	ZoomSettings(ToolController *ctrl, QObject *parent = nullptr);

	QString toolType() const override { return QStringLiteral("zoom"); }

	bool affectsCanvas() override { return false; }
	bool affectsLayer() override { return false; }

	ToolProperties saveToolSettings() override;
	void restoreToolSettings(const ToolProperties &cfg) override;
	void pushSettings() override;

signals:
	void resetZoom();
	void fitToWindow();

protected:
	QWidget *createUiWidget(QWidget *parent) override;

private:
	widgets::GroupedToolButton *m_modeDiscreteButton = nullptr;
	widgets::GroupedToolButton *m_modeScrubButton = nullptr;
	QButtonGroup *m_zoomModeGroup = nullptr;
};

}

#endif
