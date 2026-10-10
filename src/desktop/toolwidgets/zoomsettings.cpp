// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/toolwidgets/zoomsettings.h"
#include "desktop/widgets/groupedtoolbutton.h"
#include "libclient/tools/toolcontroller.h"
#include "libclient/tools/zoom.h"
#include <QButtonGroup>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>

namespace tools {

namespace props {
static const ToolProperties::RangedValue<int> zoomMode{
	QStringLiteral("zoommode"), 1, 0, 1};
}

ZoomSettings::ZoomSettings(ToolController *ctrl, QObject *parent)
	: ToolSettings(ctrl, parent)
{
}

ToolProperties ZoomSettings::saveToolSettings()
{
	ToolProperties cfg(toolType());
	cfg.setValue(props::zoomMode, m_zoomModeGroup->checkedId());
	return cfg;
}

void ZoomSettings::restoreToolSettings(const ToolProperties &cfg)
{
	QAbstractButton *button =
		m_zoomModeGroup->button(cfg.value(props::zoomMode));
	if(button) {
		button->setChecked(true);
	}
}

void ZoomSettings::pushSettings()
{
	ToolController *ctrl = controller();
	ZoomTool *tool = static_cast<ZoomTool *>(ctrl->getTool(Tool::ZOOM));
	tool->setScrub(m_zoomModeGroup->checkedId() != 0);
}

QWidget *ZoomSettings::createUiWidget(QWidget *parent)
{
	QWidget *widget = new QWidget(parent);
	QFormLayout *form = new QFormLayout(widget);

	QHBoxLayout *modeLayout = new QHBoxLayout;
	modeLayout->setContentsMargins(0, 0, 0, 0);
	modeLayout->setSpacing(0);

	m_modeDiscreteButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupLeft);
	m_modeDiscreteButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_modeDiscreteButton->setText(tr("Discrete"));
	m_modeDiscreteButton->setToolTip(tr("Zoom in steps and by drawing boxes."));
	m_modeDiscreteButton->setStatusTip(m_modeDiscreteButton->toolTip());
	m_modeDiscreteButton->setCheckable(true);
	m_modeDiscreteButton->setChecked(true);
	modeLayout->addWidget(m_modeDiscreteButton);

	m_modeScrubButton =
		new widgets::GroupedToolButton(widgets::GroupedToolButton::GroupRight);
	m_modeScrubButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	//: Refers to zooming in and out by dragging up and down.
	m_modeScrubButton->setText(tr("Scrub"));
	m_modeScrubButton->setToolTip(tr("Zoom by dragging up and down."));
	m_modeScrubButton->setStatusTip(m_modeScrubButton->toolTip());
	m_modeScrubButton->setCheckable(true);
	modeLayout->addWidget(m_modeScrubButton);

	form->addRow(tr("Mode:"), modeLayout);

	m_zoomModeGroup = new QButtonGroup(this);
	m_zoomModeGroup->addButton(m_modeDiscreteButton, 0);
	m_zoomModeGroup->addButton(m_modeScrubButton, 1);
	connect(
		m_zoomModeGroup,
		QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this,
		&ZoomSettings::pushSettings);

	QPushButton *resetButton = new QPushButton(tr("Normal Size"), widget);
	form->addRow(resetButton);
	connect(resetButton, &QPushButton::clicked, this, &ZoomSettings::resetZoom);

	QPushButton *fitButton = new QPushButton(tr("Fit To Window"), widget);
	form->addRow(fitButton);
	connect(fitButton, &QPushButton::clicked, this, &ZoomSettings::fitToWindow);

	return widget;
}

}
