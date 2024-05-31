// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" {
#include <dpengine/layer_props.h>
#include <dpengine/preview.h>
#include <dpmsg/blend_mode.h>
}
#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/layerlist.h"
#include "libclient/canvas/paintengine.h"
#include "libclient/canvas/transformmodel.h"
#include "libclient/drawdance/image.h"

namespace canvas {

TransformModel::TransformModel(CanvasModel *canvas)
	: QObject(canvas)
	, m_canvas(canvas)
{
	LayerListModel *layerlist = canvas->layerlist();
	connect(
		layerlist, &LayerListModel::modelReset, this,
		&TransformModel::updateLayerIds);
	connect(
		layerlist, &LayerListModel::layerCheckStateToggled, this,
		&TransformModel::updateLayerIds);
}

bool TransformModel::isPreviewAccurate() const
{
	return m_previewAccurate && canPreviewAccurate();
}

bool TransformModel::canPreviewAccurate() const
{
	return m_pasted || m_layerIds.size() <= DP_PREVIEW_TRANSFORM_COUNT;
}

const QImage &TransformModel::floatingImage()
{
	if(!m_pasted && m_floatingImage.isNull()) {
		switch(m_layerIds.size()) {
		case 0:
			break;
		case 1:
			m_floatingImage = getLayerImage(*m_layerIds.constBegin());
			break;
		default:
			m_floatingImage = getMergedImage();
			break;
		}
	}
	return m_floatingImage;
}

QImage TransformModel::layerImage(int layerId)
{
	if(m_layerIds.contains(layerId)) {
		QHash<int, QImage>::iterator it = m_layerImages.find(layerId);
		if(it == m_layerImages.end()) {
			QImage img = getLayerImage(layerId);
			m_layerImages.insert(layerId, img);
			return img;
		} else {
			return it.value();
		}
	} else {
		return QImage();
	}
}

void TransformModel::beginFromCanvas(
	const QRect &srcBounds, const QImage &mask, int sourceLayerId)
{
	clear();
	m_active = true;
	m_justApplied = false;
	m_srcBounds = srcBounds;
	m_dstQuad = TransformQuad(srcBounds);
	m_dstQuadValid = isQuadValid(m_dstQuad);
	m_mask = mask;
	LayerListModel *layerlist = m_canvas->layerlist();
	layerlist->initCheckedLayers(sourceLayerId);
	setLayers(layerlist->checkedLayers());
}

void TransformModel::beginFloating(const QRect &srcBounds, const QImage &image)
{
	clear();
	m_active = true;
	m_pasted = true;
	m_justApplied = false;
	m_srcBounds = srcBounds;
	m_dstQuad = TransformQuad(srcBounds);
	m_dstQuadValid = isQuadValid(m_dstQuad);
	m_floatingImage = image;
	m_canvas->layerlist()->clearCheckedLayers();
	emit transformChanged();
}

void TransformModel::setDeselectOnApply(bool deselectOnApply)
{
	if(m_active) {
		m_deselectOnApply = deselectOnApply;
	}
}

void TransformModel::setDstQuad(const TransformQuad &dstQuad)
{
	if(m_active) {
		TransformQuad result = dstQuad.round();
		if(result != m_dstQuad && !result.boundingRect().isEmpty()) {
			m_dstQuad = result;
			m_dstQuadValid = isQuadValid(result);
			m_justApplied = false;
			emit transformChanged();
		}
	} else {
		qWarning("TransformModel::setMatrix: transform not active");
	}
}

void TransformModel::setPreviewAccurate(bool previewAccurate)
{
	if(previewAccurate != m_previewAccurate) {
		m_previewAccurate = previewAccurate;
		if(m_active) {
			emit transformChanged();
		}
	}
}

void TransformModel::applyOffset(int x, int y)
{
	if(m_active) {
		m_srcBounds.translate(x, y);
		m_dstQuad.applyOffset(x, y);
		emit transformChanged();
	}
}

QVector<net::Message> TransformModel::applyActiveTransform(
	bool disguiseAsPutImage, uint8_t contextId, int layerId, int interpolation,
	bool compatibilityMode, bool stamp, bool *outMovedSelection)
{
	if(m_active && m_dstQuadValid) {
		if(m_pasted) {
			return applyFloating(
				disguiseAsPutImage, contextId, layerId, interpolation,
				compatibilityMode, stamp, outMovedSelection);
		} else {
			if(stamp) {
				if(isStampable()) {
					m_stamped = true;
				} else {
					qWarning("Attempt to stamp unstampable transform");
					return {};
				}
			}
			return applyFromCanvas(
				disguiseAsPutImage, contextId, layerId, interpolation,
				compatibilityMode, outMovedSelection);
		}
	} else {
		return {};
	}
}

void TransformModel::endActiveTransform(bool applied)
{
	if(m_active) {
		if(!m_pasted) {
			emit transformCutCleared();
		}
		clear();
		m_justApplied = applied;
		m_canvas->layerlist()->clearCheckedLayers();
		emit transformChanged();
	}
}

int TransformModel::getEffectiveInterpolation(int interpolation) const
{
	if(interpolation != DP_MSG_TRANSFORM_REGION_MODE_NEAREST &&
	   m_dstQuadValid) {
		QTransform t;
		if(QTransform::quadToQuad(
			   QPolygonF(QRectF(m_srcBounds)), m_dstQuad.polygon(), t)) {
			t.setMatrix(
				t.m11(), t.m12(), t.m13(), t.m21(), t.m22(), t.m23(), 0.0, 0.0,
				t.m33());
			if(isRightAngleRotationOrReflection(t)) {
				return DP_MSG_TRANSFORM_REGION_MODE_NEAREST;
			}
		}
	}
	return interpolation;
}

int TransformModel::getSingleLayerMoveId(int layerId) const
{
	if(m_layerIds.size() == 1) {
		QModelIndex idx = m_canvas->layerlist()->layerIndex(layerId);
		if(idx.isValid() && !idx.data(LayerListModel::IsGroupRole).toBool() &&
		   !idx.data(LayerListModel::IsCensoredInTreeRole).toBool() &&
		   !idx.data(LayerListModel::IsLockedRole).toBool()) {
			return *m_layerIds.constBegin();
		}
	}
	return 0;
}

QVector<net::Message> TransformModel::applyFromCanvas(
	bool disguiseAsPutImage, uint8_t contextId, int layerId, int interpolation,
	bool compatibilityMode, bool *outMovedSelection)
{
	Q_ASSERT(m_active);
	Q_ASSERT(!m_pasted || m_stamped);
	constexpr int SELECTION_IDS =
		(CanvasModel::MAIN_SELECTION_ID << 8) | CanvasModel::MAIN_SELECTION_ID;
	if(TransformQuad(m_srcBounds) != m_dstQuad) {
		int srcX = m_srcBounds.x();
		int srcY = m_srcBounds.y();
		int srcW = m_srcBounds.width();
		int srcH = m_srcBounds.height();
		int dstTopLeftX = qRound(m_dstQuad.topLeft().x());
		int dstTopLeftY = qRound(m_dstQuad.topLeft().y());
		bool moveContents = !m_pasted;
		bool alterSelection = !moveContents || !m_stamped;
		bool moveSelection = alterSelection && !m_deselectOnApply;
		bool needsMask = moveContents && moveNeedsMask();
		QVector<net::Message> msgs;
		msgs.reserve(1 + (moveContents ? 1 : 0) + (alterSelection ? 1 : 0));

		if(alterSelection && !moveSelection) {
			msgs.append(net::makeSelectionClearMessage(
				disguiseAsPutImage, contextId,
				canvas::CanvasModel::MAIN_SELECTION_ID));
		}

		if(compatibilityMode) {
			int dstTopRightX = qRound(m_dstQuad.topRight().x());
			int dstTopRightY = qRound(m_dstQuad.topRight().y());
			int dstBottomRightX = qRound(m_dstQuad.bottomRight().x());
			int dstBottomRightY = qRound(m_dstQuad.bottomRight().y());
			int dstBottomLeftX = qRound(m_dstQuad.bottomLeft().x());
			int dstBottomLeftY = qRound(m_dstQuad.bottomLeft().y());
			if(moveSelection && !m_deselectOnApply) {
				msgs.append(net::makeMoveRegionMessage(
					contextId, 0, srcX, srcY, srcW, srcH, dstTopLeftX,
					dstTopLeftY, dstTopRightX, dstTopRightY, dstBottomRightX,
					dstBottomRightY, dstBottomLeftX, dstBottomLeftY, QImage()));
			}
			if(moveContents) {
				for(int layerIdToMove : m_layerIds) {
					msgs.append(net::makeMoveRegionMessage(
						contextId, layerIdToMove, srcX, srcY, srcW, srcH,
						dstTopLeftX, dstTopLeftY, dstTopRightX, dstTopRightY,
						dstBottomRightX, dstBottomRightY, dstBottomLeftX,
						dstBottomLeftY,
						needsMask ? convertMaskToMono() : QImage()));
				}
			}
		} else if(moveIsOnlyTranslated()) {
			if(moveSelection) {
				msgs.append(net::makeMoveRectMessage(
					contextId, SELECTION_IDS, 0, srcX, srcY, dstTopLeftX,
					dstTopLeftY, srcW, srcH, QImage()));
			}
			if(moveContents) {
				int singleLayerSourceId = getSingleLayerMoveId(layerId);
				if(singleLayerSourceId > 0) {
					msgs.append(net::makeMoveRectMessage(
						contextId, layerId, singleLayerSourceId, srcX, srcY,
						dstTopLeftX, dstTopLeftY, srcW, srcH,
						needsMask ? m_mask : QImage()));
				} else {
					for(int layerIdToMove : m_layerIds) {
						msgs.append(net::makeMoveRectMessage(
							contextId, layerIdToMove, layerIdToMove, srcX, srcY,
							dstTopLeftX, dstTopLeftY, srcW, srcH,
							needsMask ? m_mask : QImage()));
					}
				}
			}
		} else {
			int dstTopRightX = qRound(m_dstQuad.topRight().x());
			int dstTopRightY = qRound(m_dstQuad.topRight().y());
			int dstBottomRightX = qRound(m_dstQuad.bottomRight().x());
			int dstBottomRightY = qRound(m_dstQuad.bottomRight().y());
			int dstBottomLeftX = qRound(m_dstQuad.bottomLeft().x());
			int dstBottomLeftY = qRound(m_dstQuad.bottomLeft().y());
			if(moveSelection) {
				msgs.append(net::makeTransformRegionMessage(
					contextId, SELECTION_IDS, 0, srcX, srcY, srcW, srcH,
					dstTopLeftX, dstTopLeftY, dstTopRightX, dstTopRightY,
					dstBottomRightX, dstBottomRightY, dstBottomLeftX,
					dstBottomLeftY, getEffectiveInterpolation(interpolation),
					QImage()));
			}
			if(moveContents) {
				int singleLayerSourceId = getSingleLayerMoveId(layerId);
				if(singleLayerSourceId > 0) {
					msgs.append(net::makeTransformRegionMessage(
						contextId, layerId, singleLayerSourceId, srcX, srcY,
						srcW, srcH, dstTopLeftX, dstTopLeftY, dstTopRightX,
						dstTopRightY, dstBottomRightX, dstBottomRightY,
						dstBottomLeftX, dstBottomLeftY,
						getEffectiveInterpolation(interpolation),
						needsMask ? m_mask : QImage()));
				} else {
					for(int layerIdToMove : m_layerIds) {
						msgs.append(net::makeTransformRegionMessage(
							contextId, layerIdToMove, layerIdToMove, srcX, srcY,
							srcW, srcH, dstTopLeftX, dstTopLeftY, dstTopRightX,
							dstTopRightY, dstBottomRightX, dstBottomRightY,
							dstBottomLeftX, dstBottomLeftY,
							getEffectiveInterpolation(interpolation),
							needsMask ? m_mask : QImage()));
					}
				}
			}
		}
		if(!msgs.isEmpty() && !containsNullMessages(msgs)) {
			msgs.prepend(net::makeUndoPointMessage(contextId));
			if(m_stamped && !m_pasted) {
				m_pasted = true;
				m_mask = QImage();
				QHash<int, QImage>::const_iterator imageIt =
					m_layerImages.constBegin();
				if(imageIt == m_layerImages.constEnd()) {
					QSet<int>::const_iterator idIt = m_layerIds.constBegin();
					if(idIt == m_layerIds.constEnd()) {
						m_floatingImage = QImage();
					} else {
						m_floatingImage = getLayerImage(*idIt);
					}
				} else {
					m_floatingImage = *imageIt;
				}
				m_layerImages.clear();
				m_layerIds.clear();
				m_canvas->layerlist()->clearCheckedLayers();
				emit transformCutCleared();
				emit transformChanged();
			}
			if(outMovedSelection) {
				*outMovedSelection = moveSelection;
			}
			return msgs;
		}
	}
	return {};
}

QVector<net::Message> TransformModel::applyFloating(
	bool disguiseAsPutImage, uint8_t contextId, int layerId, int interpolation,
	bool compatibilityMode, bool stamp, bool *outMovedSelection)
{
	Q_ASSERT(m_active);
	Q_ASSERT(m_pasted);
	QPoint offset;
	QImage transformedImage = drawdance::transformImage(
		m_floatingImage, m_dstQuad.polygon().toPolygon(),
		getEffectiveInterpolation(interpolation), &offset);
	if(transformedImage.isNull()) {
		qWarning(
			"TransformModel::applyFloating: could not transform image: %s",
			DP_error());
		return {};
	}

	QVector<net::Message> msgs;
	if(m_stamped && !stamp) {
		msgs = applyFromCanvas(
			disguiseAsPutImage, contextId, layerId, interpolation,
			compatibilityMode, outMovedSelection);
	}

	// May be empty either if the transform wasn't stamped or if there was
	// nothing to modify about the selection. In both cases we still paste.
	if(msgs.isEmpty()) {
		msgs.reserve(2);
		msgs.append(net::makeUndoPointMessage(contextId));
	}

	net::makePutImageMessages(
		msgs, contextId, layerId, DP_BLEND_MODE_NORMAL, offset.x(), offset.y(),
		transformedImage);

	return msgs;
}

void TransformModel::clear()
{
	m_active = false;
	m_pasted = false;
	m_deselectOnApply = false;
	m_stamped = false;
	m_dstQuadValid = false;
	m_layerIds.clear();
	m_srcBounds = QRect();
	m_dstQuad.clear();
	m_mask = QImage();
	m_floatingImage = QImage();
	m_layerImages.clear();
}

void TransformModel::updateLayerIds()
{
	if(isMovedFromCanvas()) {
		QSet<int> layerIds = m_canvas->layerlist()->checkedLayers();
		if(layerIds != m_layerIds) {
			setLayers(layerIds);
		}
	}
}

void TransformModel::setLayers(const QSet<int> &layerIds)
{
	m_layerIds = layerIds;
	m_floatingImage = QImage();

	// Don't try replacing this with `erase`, that's bugged and causes botched
	// assertions in Qt. Also avoid similar functions like `erase_if`.
	QVector<int> layerIdsToRemove;
	for(QHash<int, QImage>::key_iterator it = m_layerImages.keyBegin(),
										 end = m_layerImages.keyEnd();
		it != end; ++it) {
		int layerId = *it;
		if(!layerIds.contains(layerId)) {
			layerIdsToRemove.append(layerId);
		}
	}
	for(int layerId : layerIdsToRemove) {
		m_layerImages.remove(layerId);
	}

	emit transformCut(m_layerIds, m_srcBounds, m_mask);
	emit transformChanged();
}

QImage TransformModel::getLayerImage(int layerId) const
{
	drawdance::CanvasState canvasState =
		m_canvas->paintEngine()->viewCanvasState();
	QRect rect = QRect(QPoint(), canvasState.size()).intersected(m_srcBounds);
	if(!rect.isEmpty()) {
		drawdance::LayerSearchResult lsr =
			canvasState.searchLayer(layerId, false);
		if(drawdance::LayerContent *layerContent =
			   std::get_if<drawdance::LayerContent>(&lsr.data)) {
			return layerContent->toImage(rect);
		}
	}
	return QImage();
}

QImage TransformModel::getMergedImage() const
{
	if(!m_layerIds.isEmpty()) {
		drawdance::CanvasState canvasState =
			m_canvas->paintEngine()->viewCanvasState();
		QRect rect =
			QRect(QPoint(), canvasState.size()).intersected(m_srcBounds);
		if(!rect.isEmpty()) {
			DP_ViewModeCallback callback = {
				TransformModel::isVisibleInViewModeCallback,
				const_cast<QSet<int> *>(&m_layerIds),
			};
			DP_ViewModeFilter vmf =
				DP_view_mode_filter_make_callback(&callback);
			return canvasState.toFlatImage(false, false, &rect, &vmf);
		}
	}
	return QImage();
}

bool TransformModel::moveNeedsMask() const
{
	Q_ASSERT(!m_pasted);
	Q_ASSERT(m_mask.format() == QImage::Format_ARGB32_Premultiplied);
	size_t count = size_t(m_mask.width()) * size_t(m_mask.height());
	const uint32_t *pixels = reinterpret_cast<const uint32_t *>(m_mask.bits());
	for(size_t i = 0; i < count; ++i) {
		if(qAlpha(pixels[i]) != 255) {
			return true;
		}
	}
	return false;
}

bool TransformModel::moveIsOnlyTranslated() const
{
	TransformQuad src(m_srcBounds);
	QPointF topLeftDelta = m_dstQuad.topLeft() - src.topLeft();
	for(int i = 1; i < 4; ++i) {
		QPointF delta = m_dstQuad.at(i) - src.at(i);
		if(!qFuzzyCompare(delta.x(), topLeftDelta.x()) ||
		   !qFuzzyCompare(delta.y(), topLeftDelta.y())) {
			return false;
		}
	}
	return true;
}

QImage TransformModel::convertMaskToMono() const
{
	Q_ASSERT(!m_mask.isNull());
	int width = m_mask.width();
	int height = m_mask.height();
	QImage mono(width, height, QImage::Format_Mono);
	mono.fill(0);
	const uint32_t *src =
		reinterpret_cast<const uint32_t *>(m_mask.constBits());
	for(int y = 0; y < height; ++y) {
		for(int x = 0; x < width; ++x) {
			if(qAlpha(src[y * width + x]) != 0) {
				mono.setPixel(x, y, 1);
			}
		}
	}
	return mono;
}

bool TransformModel::containsNullMessages(const QVector<net::Message> &msgs)
{
	for(const net::Message &msg : msgs) {
		if(msg.isNull()) {
			return true;
		}
	}
	return false;
}

bool TransformModel::isRightAngleRotationOrReflection(const QTransform &t) const
{
	QPoint scales[] = {
		QPoint(1, 1), QPoint(-1, 1), QPoint(1, -1), QPoint(-1, -1)};
	for(int angle = 0; angle <= 270; angle += 90) {
		for(const QPointF scale : scales) {
			QTransform u;
			if(angle != 0) {
				u.rotate(angle);
			}
			if(scale.x() != 1 || scale.y() != 1) {
				u.scale(scale.x(), scale.y());
			}
			if(qFuzzyCompare(t, u)) {
				return true;
			}
		}
	}
	return false;
}

bool TransformModel::isQuadValid(const TransformQuad &quad)
{
	const QPolygonF &polygon = quad.polygon();
	if(polygon.size() == 4 && !polygon.boundingRect().isEmpty()) {
		int sign = crossProductSign(polygon[0], polygon[1], polygon[2]);
		return crossProductSign(polygon[1], polygon[2], polygon[3]) == sign &&
			   crossProductSign(polygon[2], polygon[3], polygon[0]) == sign &&
			   crossProductSign(polygon[3], polygon[0], polygon[1]) == sign;
	} else {
		return false;
	}
}

int TransformModel::crossProductSign(
	const QPointF &a, const QPointF &b, const QPointF &c)
{
	qreal crossProduct =
		(b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
	return crossProduct < 0.0 ? -1 : crossProduct > 0.0 ? 1 : 0;
}

bool TransformModel::isVisibleInViewModeCallback(void *user, DP_LayerProps *lp)
{
	return !DP_layer_props_hidden(lp) &&
		   (DP_layer_props_children_noinc(lp) ||
			static_cast<const QSet<int> *>(user)->contains(
				DP_layer_props_id(lp)));
}

}