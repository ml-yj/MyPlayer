
#pragma once

#include <QMetaType>
#include <QtGlobal>
#include <QString>
#include <vector>

struct DetectionBox {

    float x1, y1, x2, y2;

    float confidence;

    int classId = 0;
    QString className;

    int trackId = -1;
};

struct DetectionResult {

    std::vector<DetectionBox> boxes;

    int frameWidth = 0;
    int frameHeight = 0;

    quint64 generation = 0;
    quint64 serial = 0;
};

Q_DECLARE_METATYPE(DetectionResult)

enum class DetectorModelFamily {
    Unknown,
    RtDetr,
    Yolo,
};

enum class DetectorBackend {
    Unknown,
    Cpu,
    Gpu,
};
