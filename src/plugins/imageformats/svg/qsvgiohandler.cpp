/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:COMM$
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** $QT_END_LICENSE$
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
****************************************************************************/

#include "qsvgiohandler.h"

#ifndef QT_NO_SVGRENDERER

#include "qsvgrenderer.h"
#include "qimage.h"
#include "qpixmap.h"
#include "qpainter.h"
#include "qvariant.h"
#include "qbuffer.h"
#include "qdebug.h"

QT_BEGIN_NAMESPACE

class QSvgIOHandlerPrivate
{
public:
    QSvgIOHandlerPrivate(QSvgIOHandler *qq)
        : q(qq), loaded(false), readDone(false), backColor(Qt::transparent)
    {}

    bool load(QIODevice *device);

    QSvgIOHandler   *q;
    QSvgRenderer     r;
    QXmlStreamReader xmlReader;
    QSize            defaultSize;
    QRect            clipRect;
    QSize            scaledSize;
    QRect            scaledClipRect;
    bool             loaded;
    bool             readDone;
    QColor           backColor;
};


bool QSvgIOHandlerPrivate::load(QIODevice *device)
{
    if (loaded)
        return true;
    if (q->format().isEmpty())
        q->canRead();

    // # The SVG renderer doesn't handle trailing, unrelated data, so we must
    // assume that all available data in the device is to be read.
    bool res = false;
    QBuffer *buf = qobject_cast<QBuffer *>(device);
    if (buf) {
        const QByteArray &ba = buf->data();
        res = r.load(QByteArray::fromRawData(ba.constData() + buf->pos(), ba.size() - buf->pos()));
        buf->seek(ba.size());
#ifndef QT_NO_COMPRESS
    } else if (q->format() == "svgz") {
        res = r.load(device->readAll());
#endif
    } else {
        xmlReader.setDevice(device);
        res = r.load(&xmlReader);
    }

    if (res) {
        defaultSize = r.defaultSize();
        loaded = true;
    }

    return loaded;
}


QSvgIOHandler::QSvgIOHandler()
    : d(new QSvgIOHandlerPrivate(this))
{

}


QSvgIOHandler::~QSvgIOHandler()
{
    delete d;
}


bool QSvgIOHandler::canRead() const
{
    if (!device())
        return false;
    if (d->loaded && !d->readDone)
        return true;        // Will happen if we have been asked for the size

    QByteArray buf = device()->peek(16);
#ifndef QT_NO_COMPRESS
    if (buf.startsWith("\x1f\x8b")) {
        setFormat("svgz");
        return true;
    } else
#endif
    if (buf.contains("<?xml") || buf.contains("<svg") || buf.contains("<!--") || buf.contains("<!DOCTYPE svg")) {
        setFormat("svg");
        return true;
    }
    return false;
}

bool QSvgIOHandler::read(QImage *image)
{
    if (!d->readDone && d->load(device())) {
        bool xform = (d->clipRect.isValid() || d->scaledSize.isValid() || d->scaledClipRect.isValid());
        QSize finalSize = d->defaultSize;
        QRectF bounds;
        if (xform && !d->defaultSize.isEmpty()) {
            bounds = QRectF(QPointF(0,0), QSizeF(d->defaultSize));
            QPoint tr1, tr2;
            QSizeF sc(1, 1);
            if (d->clipRect.isValid()) {
                tr1 = -d->clipRect.topLeft();
                finalSize = d->clipRect.size();
            }
            if (d->scaledSize.isValid()) {
                sc = QSizeF(qreal(d->scaledSize.width()) / finalSize.width(),
                            qreal(d->scaledSize.height()) / finalSize.height());
                finalSize = d->scaledSize;
            }
            if (d->scaledClipRect.isValid()) {
                tr2 = -d->scaledClipRect.topLeft();
                finalSize = d->scaledClipRect.size();
            }
            QTransform t;
            t.translate(tr2.x(), tr2.y());
            t.scale(sc.width(), sc.height());
            t.translate(tr1.x(), tr1.y());
            bounds = t.mapRect(bounds);
        }
        if (image->size() != finalSize || !image->reinterpretAsFormat(QImage::Format_ARGB32_Premultiplied)) {
            *image = QImage(finalSize, QImage::Format_ARGB32_Premultiplied);
            if (!finalSize.isEmpty() && image->isNull()) {
                qWarning("QSvgIOHandler: QImage allocation failed (size %i x %i)", finalSize.width(), finalSize.height());
                return false;
            }
        }
        if (!finalSize.isEmpty()) {
            image->fill(d->backColor.rgba());
            QPainter p(image);
            d->r.render(&p, bounds);
            p.end();
        }
        d->readDone = true;
        return true;
    }

    return false;
}


QVariant QSvgIOHandler::option(ImageOption option) const
{
    switch(option) {
    case ImageFormat:
        return QImage::Format_ARGB32_Premultiplied;
        break;
    case Size:
        d->load(device());
        return d->defaultSize;
        break;
    case ClipRect:
        return d->clipRect;
        break;
    case ScaledSize:
        return d->scaledSize;
        break;
    case ScaledClipRect:
        return d->scaledClipRect;
        break;
    case BackgroundColor:
        return d->backColor;
        break;
    default:
        break;
    }
    return QVariant();
}


void QSvgIOHandler::setOption(ImageOption option, const QVariant & value)
{
    switch(option) {
    case ClipRect:
        d->clipRect = value.toRect();
        break;
    case ScaledSize:
        d->scaledSize = value.toSize();
        break;
    case ScaledClipRect:
        d->scaledClipRect = value.toRect();
        break;
    case BackgroundColor:
        d->backColor = value.value<QColor>();
        break;
    default:
        break;
    }
}


bool QSvgIOHandler::supportsOption(ImageOption option) const
{
    switch(option)
    {
    case ImageFormat:
    case Size:
    case ClipRect:
    case ScaledSize:
    case ScaledClipRect:
    case BackgroundColor:
        return true;
    default:
        break;
    }
    return false;
}


bool QSvgIOHandler::canRead(QIODevice *device)
{
    QByteArray buf = device->peek(16);
    return
#ifndef QT_NO_COMPRESS
            buf.startsWith("\x1f\x8b") ||
#endif
            buf.contains("<?xml") || buf.contains("<svg") || buf.contains("<!--") || buf.contains("<!DOCTYPE svg");
}

QT_END_NAMESPACE

#endif // QT_NO_SVGRENDERER
