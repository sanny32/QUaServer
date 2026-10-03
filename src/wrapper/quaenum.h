#ifndef QUAENUM_H
#define QUAENUM_H

#include <QUaCustomDataTypes>

using QUaEnumKey = qint64;
struct QUaEnumEntry
{
    QUaLocalizedText displayName;
    QUaLocalizedText description;
};
Q_DECLARE_METATYPE(QUaEnumEntry);
inline bool operator==(const QUaEnumEntry& lhs, const QUaEnumEntry& rhs)
{
    return lhs.displayName == rhs.displayName && lhs.description == rhs.description;
}
using QUaEnumMap = QMap<QUaEnumKey, QUaEnumEntry>;
using QUaEnumMapIter = QMapIterator<QUaEnumKey, QUaEnumEntry>;

#endif // QUAENUM_H
