#include "waveform/renderers/allshader/waveformrenderbeat.h"

#include <QtGui/qvectornd.h>

#include <QDomNode>

#include "moc_waveformrenderbeat.cpp"
#include "rendergraph/geometry.h"
#include "rendergraph/material/rgbamaterial.h"
#include "rendergraph/vertexupdaters/rgbavertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveformwidgetfactory.h"
#include "widget/wskincolor.h"

using namespace rendergraph;

namespace allshader {

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
    initForRectangles<RGBAMaterial>(0);
    setUsePreprocess(true);

    auto pNode = std::make_unique<DigitsRenderNode>();
    m_pDigitsRenderNode = pNode.get();
    appendChildNode(std::move(pNode));
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& skinContext) {
    m_color = QColor(skinContext.selectString(node, QStringLiteral("BeatColor")));
    m_color = WSkinColor::getCorrectColor(m_color).toRgb();
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderBeat::preprocess() {
    if (!preprocessInner()) {
        geometry().allocate(0);
        markDirtyGeometry();
    }
}

bool WaveformRenderBeat::preprocessInner() {
    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
        return false;
    }

#ifndef __SCENEGRAPH__
    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return false;
    }
    m_color.setAlphaF(alpha / 100.0f);
#endif

    if (!m_color.alpha()) {
        // Don't render the beatgrid lines is there are fully transparent
        return true;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0.0) {
        return false;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    if (!startPosition.isValid() || !endPosition.isValid()) {
        return false;
    }

    const float rendererBreadth = m_waveformRenderer->getBreadth();

    const int numVerticesPerLine = 6; // 2 triangles

    // Count the number of beats in the range to reserve space in the m_vertices vector.
    // Note that we could also use
    //   int numBearsInRange = trackBeats->numBeatsInRange(startPosition, endPosition);
    // for this, but there have been reports of that method failing with a DEBUG_ASSERT.
    int numBeatsInRange = 0;
    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        numBeatsInRange++;
    }

    const int reserved = numBeatsInRange * numVerticesPerLine;
    geometry().allocate(reserved);

    RGBAVertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::RGBAColoredPoint2D>()};
    QVector4D beatColor{
            m_color.red() / 255.0f,
            m_color.green() / 255.0f,
            m_color.blue() / 255.0f,
            m_color.alpha() / 255.0f};
    QVector4D barColor{
            255 / 255.0f,
            255 / 255.0f,
            0 / 255.0f,
            m_color.alpha() / 255.0f};
    QVector4D phraseColor{
            255 / 255.0f,
            0 / 255.0f,
            0 / 255.0f,
            m_color.alpha() / 255.0f};

    int beatIndex = trackBeats->iteratorFrom(startPosition) - trackBeats->iteratorFrom(mixxx::audio::FramePos(0.0));
    const auto m_untilMarkTextSize =
            WaveformWidgetFactory::instance()->getUntilMarkTextPointSize();
    const auto untilMarkTextHeightLimit =
            WaveformWidgetFactory::instance()
                    ->getUntilMarkTextHeightLimit();
    m_pDigitsRenderNode->clear();
    m_pDigitsRenderNode->updateTexture(m_waveformRenderer->getContext(),
            m_untilMarkTextSize,
            std::roundf(rendererBreadth * untilMarkTextHeightLimit),
            m_waveformRenderer->getDevicePixelRatio());
    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        beatPosition, positionType);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        const float x1 = static_cast<float>(xBeatPoint);
        const float x2 = x1 + 1.f;

        if (beatIndex % 16 == 0) {
            vertexUpdater.addRectangle({x1 - 2.0f, 0.f},
                    {x2 + 2.0f, m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth},
                    phraseColor);
        } else if (beatIndex % 4 == 0) {
            vertexUpdater.addRectangle({x1 - 1.0f, 0.f},
                    {x2 + 1.0f, m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth},
                    barColor);
        } else {
            vertexUpdater.addRectangle({x1, 0.f},
                    {x2, m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth},
                    beatColor);
        }
        if (beatIndex >= 0 && beatIndex % 4 == 0) {
            m_pDigitsRenderNode->update(
                    x1 + 4.0f,
                    0.0f,
                    false,
                    QString::number(beatIndex / 4),
                    QString{},
                    true);
        }

        ++beatIndex;
    }
    markDirtyGeometry();

    DEBUG_ASSERT(reserved == vertexUpdater.index());

    markDirtyMaterial();

    return true;
}

} // namespace allshader
