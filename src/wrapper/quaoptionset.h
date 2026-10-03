#ifndef QUAOPTIONSET_H
#define QUAOPTIONSET_H

// NOTE : support up to 64-bit option sets (128bits total = 64 for value, 64 for validity)
using QUaOptionSetBit = quint64;

using QUaOptionSetMap = QMap<QUaOptionSetBit, QUaLocalizedText>;
using QUaOptionSetMapIter = QMapIterator<QUaOptionSetBit, QUaLocalizedText>;

// User validation
using QUaValidationCallback = std::function<bool(const QString&, const QString&)>;

#endif // QUAOPTIONSET_H
