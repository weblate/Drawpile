// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_TOOLWIDGETS_FILL_H
#define DESKTOP_TOOLWIDGETS_FILL_H
#include "desktop/toolwidgets/toolsettings.h"

class QAction;
class QButtonGroup;
class Ui_FillSettings;

namespace canvas {
struct LayerListItem;
class LayerListModel;
}

namespace tools {

/**
 * @brief Settings for the flood fill tool
 */
class FillSettings final : public ToolSettings {
	Q_OBJECT
public:
	FillSettings(ToolController *ctrl, QObject *parent = nullptr);
	~FillSettings() override;

	QString toolType() const override { return QStringLiteral("fill"); }

	bool affectsCanvas() override { return true; }
	bool affectsLayer() override { return true; }

	void quickAdjust1(qreal adjustment) override;
	void stepAdjust1(bool increase) override;
	void setForeground(const QColor &color) override;

	int getSize() const override;
	bool isSquare() const override { return true; }
	bool requiresOutline() const override { return true; }

	ToolProperties saveToolSettings() override;
	void restoreToolSettings(const ToolProperties &cfg) override;

	void setCompatibilityMode(bool compatibilityMode);

	QWidget *getHeaderWidget() override { return m_headerWidget; }

signals:
	void pixelSizeChanged(int size);

public slots:
	void pushSettings() override;
	void toggleEraserMode() override;
	void toggleRecolorMode() override;
	void updateSelection();
	void updateFillSourceLayerId(int layerId);

protected:
	QWidget *createUiWidget(QWidget *parent) override;

private:
	void updateSettings();

	void updateSize();
	static bool isSizeUnlimited(int size);
	int calculatePixelSize(int size) const;

	void initBlendModeOptions();
	void selectBlendMode(int blendMode);

	void setButtonState(bool running, bool pending);

	QWidget *m_headerWidget = nullptr;
	Ui_FillSettings *m_ui = nullptr;
	QAction *m_editableAction = nullptr;
	QAction *m_confirmAction = nullptr;
	QButtonGroup *m_sourceGroup = nullptr;
	QButtonGroup *m_areaGroup = nullptr;
	int m_previousMode;
	int m_previousEraseMode;
	qreal m_quickAdjust1 = 0.0;
	bool m_haveSelection = false;
	bool m_compatibilityMode = false;
	bool m_updating = false;
};

}

#endif
