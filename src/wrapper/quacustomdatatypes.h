#ifndef QUACUSTOMDATATYPES_H
#define QUACUSTOMDATATYPES_H

#include <QObject>
#include <QVariant>
#include <QUuid>
#include <QRegularExpression>
#include <QDate>
#include <QTimeZone>
#include <QQueue>
#include <QDataStream>
#include <QMetaEnum>
#include <QDebug>
#include "pch_open62541.h"

class QUaNode;

Q_DECLARE_METATYPE(QTimeZone);

// traits used to static assert that a method cannot be used
// https://stackoverflow.com/questions/24609872/delete-virtual-function-from-a-derived-class
template <typename T>
struct QUaFail : std::false_type
{
};

// to have UA_NodeId as a hash key
inline bool operator==(const UA_NodeId &e1, const UA_NodeId &e2)
{
    return UA_NodeId_equal(&e1, &e2);
}

inline size_t qHash(const UA_NodeId &key, size_t seed)
{
    return
            qHash(key.namespaceIndex, seed) ^
            qHash(key.identifierType, seed) ^
            (key.identifierType == UA_NODEIDTYPE_NUMERIC ?
                 qHash(key.identifier.numeric, seed) :
                 UA_NodeId_hash(&key)
                 );
}

namespace QUa
{
Q_NAMESPACE

enum class NodeIdType {
    Numeric    = UA_NodeIdType::UA_NODEIDTYPE_NUMERIC,
    String     = UA_NodeIdType::UA_NODEIDTYPE_STRING,
    Guid       = UA_NodeIdType::UA_NODEIDTYPE_GUID,
    ByteString = UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING
};
Q_ENUM_NS(NodeIdType)

// Part 8 - 6.3.2 Operation level result codes
enum class Status
{
    Good                                    = static_cast<int>(UA_STATUSCODE_GOOD),
    GoodLocalOverride                       = static_cast<int>(UA_STATUSCODE_GOODLOCALOVERRIDE),
    Uncertain                               = static_cast<int>(0x40000000),
    UncertainNoCommunicationLastUsableValue = static_cast<int>(UA_STATUSCODE_UNCERTAINNOCOMMUNICATIONLASTUSABLEVALUE),
    UncertainLastUsableValue                = static_cast<int>(UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE),
    UncertainSubstituteValue                = static_cast<int>(UA_STATUSCODE_UNCERTAINSUBSTITUTEVALUE),
    UncertainInitialValue                   = static_cast<int>(UA_STATUSCODE_UNCERTAININITIALVALUE),
    UncertainSensorNotAccurate              = static_cast<int>(UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE),
    UncertainEngineeringUnitsExceeded       = static_cast<int>(UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED),
    UncertainSubNormal                      = static_cast<int>(UA_STATUSCODE_UNCERTAINSUBNORMAL),
    Bad                                     = static_cast<int>(0x80000000),
    BadConfigurationError	                = static_cast<int>(UA_STATUSCODE_BADCONFIGURATIONERROR),
    BadNotConnected			                = static_cast<int>(UA_STATUSCODE_BADNOTCONNECTED),
    BadDeviceFailure		                = static_cast<int>(UA_STATUSCODE_BADDEVICEFAILURE),
    BadSensorFailure		                = static_cast<int>(UA_STATUSCODE_BADSENSORFAILURE),
    BadOutOfService			                = static_cast<int>(UA_STATUSCODE_BADOUTOFSERVICE),
    BadDeadbandFilterInvalid                = static_cast<int>(UA_STATUSCODE_BADDEADBANDFILTERINVALID)
};
Q_ENUM_NS(Status)
size_t qHash(const Status& key, size_t seed = 0);

enum class LogLevel {
    Trace   = UA_LogLevel::UA_LOGLEVEL_TRACE,
    Debug   = UA_LogLevel::UA_LOGLEVEL_DEBUG,
    Info    = UA_LogLevel::UA_LOGLEVEL_INFO,
    Warning = UA_LogLevel::UA_LOGLEVEL_WARNING,
    Error   = UA_LogLevel::UA_LOGLEVEL_ERROR,
    Fatal   = UA_LogLevel::UA_LOGLEVEL_FATAL
};
Q_ENUM_NS(LogLevel)
size_t qHash(const LogLevel& key, size_t seed = 0);

enum class LogCategory {
    Network        = UA_LogCategory::UA_LOGCATEGORY_NETWORK,
    SecureChannel  = UA_LogCategory::UA_LOGCATEGORY_SECURECHANNEL,
    Session        = UA_LogCategory::UA_LOGCATEGORY_SESSION,
    Server         = UA_LogCategory::UA_LOGCATEGORY_SERVER,
    Client         = UA_LogCategory::UA_LOGCATEGORY_CLIENT,
    UserLand       = UA_LogCategory::UA_LOGCATEGORY_USERLAND,
    SecurityPolicy = UA_LogCategory::UA_LOGCATEGORY_SECURITYPOLICY,
    Serialization,
    History,
    Application
};
Q_ENUM_NS(LogCategory)
size_t qHash(const LogCategory& key, size_t seed = 0);

enum class ExclusiveLimitState {
    None     = 0,
    Normal   = None,
    HighHigh = 1,
    High     = 2,
    Low      = 3,
    LowLow   = 4
};
Q_ENUM_NS(ExclusiveLimitState)
size_t qHash(const ExclusiveLimitState& key, size_t seed = 0);

enum class ExclusiveLimitTransition {
    None           = 0,
    Null           = None,
    HighHighToHigh = 1,
    HighToHighHigh = 2,
    LowLowToLow    = 3,
    LowToLowLow    = 4
};
Q_ENUM_NS(ExclusiveLimitTransition)
size_t qHash(const ExclusiveLimitTransition& key, size_t seed = 0);

enum class ChangeVerb
{
    NodeAdded        = 1,
    NodeDeleted      = 2,
    ReferenceAdded   = 4,
    ReferenceDeleted = 8,
    DataTypeChanged  = 16
};
Q_ENUM_NS(ChangeVerb)
size_t qHash(const ChangeVerb& key, size_t seed = 0);

// Basic128Rsa15 and Basic256 are only available when open62541 is built with UA_INCLUDE_INSECURE_POLICIES
enum class SecurityPolicy
{
    None                  = 0x001,
    Basic128Rsa15         = 0x002,
    Basic256              = 0x004,
    Basic256Sha256        = 0x008,
    Aes128Sha256RsaOaep   = 0x010,
    Aes256Sha256RsaPss    = 0x020,
    EccNistP256AesGcm     = 0x040,
    EccNistP256ChaChaPoly = 0x080,
    EccCurve25519         = 0x100,
    EccCurve448           = 0x200,
    All                   = 0x3FF
};
Q_DECLARE_FLAGS(SecurityPolicies, SecurityPolicy)
Q_FLAG_NS(SecurityPolicies)

enum class MessageSecurityMode
{
    None           = 0x1,
    Sign           = 0x2,
    SignAndEncrypt = 0x4,
    All            = 0x7
};
Q_DECLARE_FLAGS(MessageSecurityModes, MessageSecurityMode)
Q_FLAG_NS(MessageSecurityModes)
}
Q_DECLARE_OPERATORS_FOR_FLAGS(QUa::SecurityPolicies)
Q_DECLARE_OPERATORS_FOR_FLAGS(QUa::MessageSecurityModes)

using QUaLogLevel = QUa::LogLevel;
using QUaLogCategory = QUa::LogCategory;
using QUaStatus = QUa::Status;
using QUaChangeVerb = QUa::ChangeVerb;
using QUaSecurityPolicy = QUa::SecurityPolicy;
using QUaSecurityPolicies = QUa::SecurityPolicies;
using QUaMessageSecurityMode = QUa::MessageSecurityMode;
using QUaMessageSecurityModes = QUa::MessageSecurityModes;

struct QUaLog
{
    // default constructor required by Qt
    QUaLog();
    // consutructor accepting QString instead of QByteArray (to support generating messages with QObject::tr)
    QUaLog(const QString& strMessage, const QUaLogLevel& logLevel, const QUaLogCategory& logCategory);
    QUaLog(const QByteArray& baMessage, const QUaLogLevel& logLevel, const QUaLogCategory& logCategory);
    // members
    QByteArray     message;
    QUaLogLevel    level;
    QUaLogCategory category;
    QDateTime      timestamp;

    static QMetaEnum _metaEnumCategory;
    static QMetaEnum _metaEnumLevel;
    template<typename T>
    static QString toString(
        const T& log,
        const QString &separator = QStringLiteral(", "),
        const QString &timeFormat = QStringLiteral("dd.MM.yyyy hh:mm:ss.zzz"),
        const QString &lineFormat = QStringLiteral("%1%5%2%5%3%5%4")
        );
};
Q_DECLARE_METATYPE(QUaLog);

template<typename T>
inline QString QUaLog::toString(
        const T& logs,
        const QString& separator,
        const QString& timeFormat,
        const QString& lineFormat
        )
{
    QString strOut;
    auto iter = logs.begin();
    while (iter != logs.end())
    {
        auto& log = *iter;
        strOut += QUaLog::toString(log, separator, timeFormat, lineFormat);
        if (++iter != logs.end())
        {
            strOut += QLatin1Char('\n');
        }
    }
    return strOut;
}

template<>
inline QString QUaLog::toString(
        const QUaLog& log,
        const QString& separator,
        const QString& timeFormat,
        const QString& lineFormat
        )
{
    QString strTime  = log.timestamp.toLocalTime().toString(timeFormat);
    QString strLevel = QString::fromLatin1( QUaLog::_metaEnumLevel.valueToKey(static_cast<int>(log.level)) );
    QString strCateg = QString::fromLatin1( QUaLog::_metaEnumCategory.valueToKey(static_cast<int>(log.category)) );
    return lineFormat
            .arg(strTime)
            .arg(strLevel)
            .arg(strCateg)
            .arg(QString::fromUtf8(log.message))
            .arg(separator);
}

union QUaWriteMask
{
    struct bit_map {
        bool bAccessLevel             : 1; // UA_WRITEMASK_ACCESSLEVEL
        bool bArrrayDimensions        : 1; // UA_WRITEMASK_ARRRAYDIMENSIONS
        bool bBrowseName              : 1; // UA_WRITEMASK_BROWSENAME
        bool bContainsNoLoops         : 1; // UA_WRITEMASK_CONTAINSNOLOOPS
        bool bDataType                : 1; // UA_WRITEMASK_DATATYPE
        bool bDescription             : 1; // UA_WRITEMASK_DESCRIPTION
        bool bDisplayName             : 1; // UA_WRITEMASK_DISPLAYNAME
        bool bEventNotifier           : 1; // UA_WRITEMASK_EVENTNOTIFIER
        bool bExecutable              : 1; // UA_WRITEMASK_EXECUTABLE
        bool bHistorizing             : 1; // UA_WRITEMASK_HISTORIZING
        bool bInverseName             : 1; // UA_WRITEMASK_INVERSENAME
        bool bIsAbstract              : 1; // UA_WRITEMASK_ISABSTRACT
        bool bMinimumSamplingInterval : 1; // UA_WRITEMASK_MINIMUMSAMPLINGINTERVAL
        bool bNodeClass               : 1; // UA_WRITEMASK_NODECLASS
        bool bNodeId                  : 1; // UA_WRITEMASK_NODEID
        bool bSymmetric               : 1; // UA_WRITEMASK_SYMMETRIC
        bool bUserAccessLevel         : 1; // UA_WRITEMASK_USERACCESSLEVEL
        bool bUserExecutable          : 1; // UA_WRITEMASK_USEREXECUTABLE
        bool bUserWriteMask           : 1; // UA_WRITEMASK_USERWRITEMASK
        bool bValueRank               : 1; // UA_WRITEMASK_VALUERANK
        bool bWriteMask               : 1; // UA_WRITEMASK_WRITEMASK
        bool bValueForVariableType    : 1; // UA_WRITEMASK_VALUEFORVARIABLETYPE
    } bits;
    quint32 intValue;
    // constructors
    QUaWriteMask()
    {
        // all attributes writable by default (getUserRightsMask_default returns 0xFFFFFFFF)
        bits.bAccessLevel             = true;
        bits.bArrrayDimensions        = true;
        bits.bBrowseName              = true;
        bits.bContainsNoLoops         = true;
        bits.bDataType                = true;
        bits.bDescription             = true;
        bits.bDisplayName             = true;
        bits.bEventNotifier           = true;
        bits.bExecutable              = true;
        bits.bHistorizing             = true;
        bits.bInverseName             = true;
        bits.bIsAbstract              = true;
        bits.bMinimumSamplingInterval = true;
        bits.bNodeClass               = true;
        bits.bNodeId                  = true;




        bits.bSymmetric               = true;
        bits.bUserAccessLevel         = true;
        bits.bUserExecutable          = true;
        bits.bUserWriteMask           = true;
        bits.bValueRank               = true;
        bits.bWriteMask               = true;
        bits.bValueForVariableType    = true;
    };
    QUaWriteMask(const quint32& value)
    {
        intValue = value;
    };
};

union QUaAccessLevel
{
    struct bit_map {
        bool bRead           : 1; // UA_ACCESSLEVELMASK_READ
        bool bWrite          : 1; // UA_ACCESSLEVELMASK_WRITE
        bool bHistoryRead    : 1; // UA_ACCESSLEVELMASK_HISTORYREAD
        bool bHistoryWrite   : 1; // UA_ACCESSLEVELMASK_HISTORYWRITE
        bool bSemanticChange : 1; // UA_ACCESSLEVELMASK_SEMANTICCHANGE
        bool bStatusWrite    : 1; // UA_ACCESSLEVELMASK_STATUSWRITE
        bool bTimestampWrite : 1; // UA_ACCESSLEVELMASK_TIMESTAMPWRITE
    } bits;
    quint8 intValue;
    // constructors
    QUaAccessLevel()
    {
        // read only by default
        bits.bRead           = true;
        bits.bWrite          = false;
        bits.bHistoryRead    = false;
        bits.bHistoryWrite   = false;
        bits.bSemanticChange = false;
        bits.bStatusWrite    = false;
        bits.bTimestampWrite = false;
    };
    QUaAccessLevel(const quint8& value)
    {
        intValue = value;
    };
};

union QUaEventNotifier
{
    // https://reference.opcfoundation.org/v104/Core/DataTypes/EventNotifierType/
    struct bit_map {
        bool bSubscribeToEvents : 1; // UA_EVENTNOTIFIERTYPE_SUBSCRIBETOEVENTS
        bool bUnused            : 1; // N/A
        bool bHistoryRead       : 1; // UA_EVENTNOTIFIERTYPE_HISTORYREAD
        bool bHistoryWrite      : 1; // UA_EVENTNOTIFIERTYPE_HISTORYWRITE
    } bits;
    quint8 intValue;
    // constructors
    QUaEventNotifier()
    {
        // read only by default
        bits.bSubscribeToEvents = false;
        bits.bUnused            = false;
        bits.bHistoryRead       = false;
        bits.bHistoryWrite      = false;
    };
    QUaEventNotifier(const quint8& value)
    {
        intValue = value;
    };
};

#define QMetaType_NodeId        static_cast<QMetaType::Type>(qMetaTypeId<QUaNodeId       >())
#define QMetaType_StatusCode    static_cast<QMetaType::Type>(qMetaTypeId<QUaStatusCode   >())
#define QMetaType_QualifiedName static_cast<QMetaType::Type>(qMetaTypeId<QUaQualifiedName>())
#define QMetaType_LocalizedText static_cast<QMetaType::Type>(qMetaTypeId<QUaLocalizedText>())
#define QMetaType_DataType      static_cast<QMetaType::Type>(qMetaTypeId<QUaDataType     >())

// custom list types
#define QMetaType_List_NodeId        static_cast<QMetaType::Type>(qMetaTypeId<QList<QUaNodeId       >>())
#define QMetaType_List_StatusCode    static_cast<QMetaType::Type>(qMetaTypeId<QList<QUaStatusCode   >>())
#define QMetaType_List_QualifiedName static_cast<QMetaType::Type>(qMetaTypeId<QList<QUaQualifiedName>>())
#define QMetaType_List_LocalizedText static_cast<QMetaType::Type>(qMetaTypeId<QList<QUaLocalizedText>>())
#define QMetaType_List_DataType      static_cast<QMetaType::Type>(qMetaTypeId<QList<QUaDataType     >>())

#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
#define QMetaType_Image              static_cast<QMetaType::Type>(qMetaTypeId<QByteArray             >())
#define QMetaType_OptionSet          static_cast<QMetaType::Type>(qMetaTypeId<QUaOptionSet           >())
#define QMetaType_List_OptionSet     static_cast<QMetaType::Type>(qMetaTypeId<QList<QUaOptionSet>    >())
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
#define QMetaType_TimeZone                     static_cast<QMetaType::Type>(qMetaTypeId<QTimeZone>())
#define QMetaType_ChangeStructureDataType      static_cast<QMetaType::Type>(qMetaTypeId<QUaChangeStructureDataType>())
#define QMetaType_List_ChangeStructureDataType static_cast<QMetaType::Type>(qMetaTypeId<QUaChangesList>())
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

class QUaDataType
{

public:
    QUaDataType();
    QUaDataType(const QMetaType::Type& metaType);
    QUaDataType(const QString& strType);
    operator QMetaType::Type() const;
    operator QString() const;
    bool operator==(const QMetaType::Type& metaType);
    void operator=(const QString& strType);

    // static

    static bool               isSupportedQType(const QMetaType::Type& type);
    static QMetaType::Type    qTypeByNodeId   (const UA_NodeId &nodeId);
    static QMetaType::Type    qTypeByTypeIndex(const int& typeIndex);
    static UA_NodeId          nodeIdByQType   (const QMetaType::Type& type);
    static const UA_DataType* dataTypeByQType (const QMetaType::Type& type);
    static QString            stringByQType   (const QMetaType::Type& type);

private:
    QMetaType::Type _type;
    static QHash<QString  , QMetaType::Type> _custTypesByName;
    static QHash<UA_NodeId, QMetaType::Type> _custTypesByNodeId;
    static QHash<int      , QMetaType::Type> _custTypesByTypeIndex;
    struct TypeData
    {
        QString            name;
        UA_NodeId          nodeId;
        const UA_DataType* dataType;
    };
    static QHash<QMetaType::Type, TypeData> _custTypesByType;
};

Q_DECLARE_METATYPE(QUaDataType);

class QUaStatusCode
{

public:
    QUaStatusCode();
    QUaStatusCode(const QUaStatus& uaStatus);
    QUaStatusCode(const UA_StatusCode& intStatus);
    QUaStatusCode(const QString& strStatus);
    operator QUaStatus() const;
    operator UA_StatusCode() const;
    operator QString() const;
    bool operator==(const QUaStatus& uaStatus) const;
    void operator=(const QString& strStatus);

    static QString longDescription(const QUaStatusCode& statusCode);

private:
    QUaStatus _status;
    static QMetaEnum _metaEnum;
    static QHash<QUaStatus, QString> _descriptions;
    QUaStatusCode(const QByteArray& byteStatus);
};

Q_DECLARE_METATYPE(QUaStatusCode);

using QUaNodeIdType = QUa::NodeIdType;

class QUaNodeId
{
public:
    QUaNodeId();
    ~QUaNodeId();
    QUaNodeId(const quint16& index, const quint32& numericId);
    QUaNodeId(const quint16& index, const QString& stringId);
    QUaNodeId(const quint16& index, const char*    stringId);
    QUaNodeId(const quint16& index, const QUuid& uuId);
    QUaNodeId(const quint16& index, const QByteArray& byteArrayId);
    QUaNodeId(const QUaNodeId& other);
    QUaNodeId(const UA_NodeId& uaNodeId);
    QUaNodeId(const QString&   strXmlNodeId);
    QUaNodeId(const char*      strXmlNodeId);
    operator UA_NodeId() const; // needs cleanup with UA_NodeId_clear after use
    operator QString() const;
    void operator= (const UA_NodeId& uaNodeId);
    void operator= (const QString& strXmlNodeId);
    void operator= (const char* strXmlNodeId);
    void operator= (const QUaNodeId& other);
    bool operator==(const QUaNodeId& other) const;
    bool operator!=(const QUaNodeId& other) const;
    bool operator==(const UA_NodeId& other) const;
    bool operator< (const QUaNodeId& other) const;

    quint16 namespaceIndex() const;
    void    setNamespaceIndex(const quint16& index);

    QUaNodeIdType type() const;

    quint32    numericId() const;
    void       setNumericId(const quint32& numericId);
    QString    stringId() const;
    void       setStringId(const QString& stringId);
    QUuid      uuId() const;
    void       setUuId(const QUuid& uuId);
    QByteArray byteArrayId() const;
    void       setByteArrayId(const QByteArray& byteArrayId);

    QString   toXmlString() const;
    UA_NodeId toUaNodeId() const; // needs cleanup with UA_NodeId_clear after use

    bool isNull() const;

    void clear();

    quint32 internalHash() const;

private:
    UA_NodeId _nodeId;
    friend QDataStream& operator<<(QDataStream& outStream, const QUaNodeId& inNodeId);
    friend QDataStream& operator>>(QDataStream& inStream, QUaNodeId& outNodeId);
};

Q_DECLARE_METATYPE(QUaNodeId);

inline size_t qHash(const QUaNodeId& key, size_t seed)
{
    return
            qHash(key.namespaceIndex(), seed) ^
            qHash(static_cast<int>(key.type()), seed) ^
            (key.type() == QUaNodeIdType::Numeric ?
                 qHash(key.numericId(), seed) :
                 key.internalHash()
                 );
}

inline QDataStream& operator<<(QDataStream& outStream, const QUaNodeId& inNodeId)
{
    outStream << inNodeId._nodeId.namespaceIndex;
    outStream << static_cast<quint8>(inNodeId._nodeId.identifierType);
    QUaNodeIdType type = inNodeId.type();
    switch (type)
    {
    case QUaNodeIdType::Numeric:
        outStream << inNodeId._nodeId.identifier.numeric;
        break;
    case QUaNodeIdType::String:
        outStream << inNodeId.stringId();
        break;
    case QUaNodeIdType::Guid:
        outStream << inNodeId.uuId();
        break;
    case QUaNodeIdType::ByteString:
        outStream << inNodeId.byteArrayId();
        break;
    default:
        break;
    }
    return outStream;
}

inline QDataStream& operator>>(QDataStream& inStream, QUaNodeId& outNodeId)
{
    inStream >> outNodeId._nodeId.namespaceIndex;
    quint8 identifierType;
    inStream >> identifierType;
    outNodeId._nodeId.identifierType = static_cast<UA_NodeIdType>(identifierType);
    QUaNodeIdType type = outNodeId.type();
    switch (type)
    {
    case QUaNodeIdType::Numeric:
    {
        inStream >> outNodeId._nodeId.identifier.numeric;
    }
        break;
    case QUaNodeIdType::String:
    {
        QString strId;
        inStream >> strId;
        outNodeId.setStringId(strId);
    }
        break;
    case QUaNodeIdType::Guid:
    {
        QUuid uuId;
        inStream >> uuId;
        outNodeId.setUuId(uuId);
    }
        break;
    case QUaNodeIdType::ByteString:
    {
        QByteArray byteId;
        inStream >> byteId;
        outNodeId.setByteArrayId(byteId);
    }
        break;
    default:
        break;
    }
    return inStream;
}

class QUaLocalizedText
{
public:
    QUaLocalizedText();
    QUaLocalizedText(const QString& locale, const QString& text);
    QUaLocalizedText(const char* locale, const char* text);
    QUaLocalizedText(const UA_LocalizedText& uaLocalizedText);
    QUaLocalizedText(const QString& strXmlLocalizedText);
    QUaLocalizedText(const char* strXmlLocalizedText);

    operator UA_LocalizedText() const; // needs cleanup with UA_LocalizedText_clear after use
    operator QString() const;

    void operator= (const UA_LocalizedText& uaLocalizedText);
    void operator= (const QString& strXmlLocalizedText);
    void operator= (const char* strXmlLocalizedText);
    bool operator==(const QUaLocalizedText& other) const;
    bool operator< (const QUaLocalizedText& other) const;

    QString locale() const;
    void    setLocale(const QString& locale);
    QString text() const;
    void    setText(const QString& text);

    QString          toXmlString() const;
    UA_LocalizedText toUaLocalizedText() const; // needs cleanup with UA_LocalizedText_clear after use

private:
    QString _locale;
    QString _text;
};

Q_DECLARE_METATYPE(QUaLocalizedText);

class QUaQualifiedName
{
public:
    QUaQualifiedName();
    QUaQualifiedName(const quint16 &namespaceIndex, const QString &name);
    QUaQualifiedName(const UA_QualifiedName& uaQualName);
    QUaQualifiedName(const QString& strXmlQualName);
    QUaQualifiedName(const char * strXmlQualName);
    operator UA_QualifiedName() const; // needs cleanup with UA_QualifiedName_clear after use
    operator QString() const;
    void operator=(const UA_QualifiedName& uaQualName);
    void operator=(const QString& strXmlQualName);
    void operator=(const char * strXmlQualName);
    bool operator==(const QUaQualifiedName& other) const;
    bool operator!=(const QUaQualifiedName& other) const;
    bool operator< (const QUaQualifiedName& other) const;

    quint16 namespaceIndex() const;
    void setNamespaceIndex(const quint16& index);

    QString name() const;
    void setName(const QString& name);

    QString toXmlString() const;
    UA_QualifiedName toUaQualifiedName() const; // needs cleanup with UA_QualifiedName_clear after use

    bool isEmpty() const;

    // helpers

    static QUaQualifiedName fromXmlString(const QString& strXmlQualName);
    static QUaQualifiedName fromUaQualifiedName(const UA_QualifiedName& uaQualName);

    using QUaBrowsePath = QList<QUaQualifiedName>;

    static QUaBrowsePath saoToBrowsePath(const UA_SimpleAttributeOperand* sao);

    static QString reduceXml(const QUaBrowsePath& browsePath);

    static QString reduceName(const QUaBrowsePath& browsePath, const QString& separator = QStringLiteral("/"));
    static QUaBrowsePath expandName(const QString& strPath, const QString& separator = QStringLiteral("/"));

private:
    quint16 _namespace;
    QString _name;

    friend QDataStream& operator<<(QDataStream& outStream, const QUaQualifiedName& inQualName);
    friend QDataStream& operator>>(QDataStream& inStream, QUaQualifiedName& outQualName);
};

Q_DECLARE_METATYPE(QUaQualifiedName);

inline size_t qHash(const QUaQualifiedName& key)
{
    return qHash(key.name(), key.namespaceIndex());
}

inline size_t qHash(const QUaQualifiedName& key, size_t seed)
{
    return qHash(key.name(), seed) ^ key.namespaceIndex();
}

inline QDataStream& operator<<(QDataStream& outStream, const QUaQualifiedName& inQualName)
{
    outStream << inQualName._namespace;
    outStream << inQualName._name;
    return outStream;
}

inline QDataStream& operator>>(QDataStream& inStream, QUaQualifiedName& outQualName)
{
    inStream >> outQualName._namespace;
    inStream >> outQualName._name;
    return inStream;
}

using QUaBrowsePath = QList<QUaQualifiedName>;

inline size_t qHash(const QUaBrowsePath& key)
{
    size_t outKey = 0;
    for (const auto& elem : key)
    {
        outKey = outKey ^ qHash(elem);
    }
    return outKey;
}

inline size_t qHash(const QUaBrowsePath& key, size_t seed)
{
    size_t outKey = 0;
    for (const auto& elem : key)
    {
        outKey = outKey ^ qHash(elem, seed);
    }
    return outKey;
}

#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
// NOTE : Currently only support 64bit option sets, might considered templated version in the future
class QUaOptionSet
{
public:
    QUaOptionSet();
    QUaOptionSet(const QUaOptionSet& other);
    QUaOptionSet(const quint64 &values, const quint64 &validBits = 0xFFFFFFFFFFFFFFFF);
    QUaOptionSet(const UA_OptionSet& uaOptionSet);
    QUaOptionSet(const QString& strXmlOptionSet);
    QUaOptionSet(const char* strXmlOptionSet);
    operator UA_OptionSet() const; // needs cleanup with UA_OptionSet_clear after use
    operator QString() const;
    void operator= (const UA_OptionSet& uaOptionSet);
    void operator= (const QString& strXmlOptionSet);
    void operator= (const char* strXmlOptionSet);
    void operator= (const QUaOptionSet& other);
    bool operator==(const QUaOptionSet& other) const;
    bool operator!=(const QUaOptionSet& other) const;
    bool operator< (const QUaOptionSet& other) const;

    quint64 values() const;
    void    setValues(const quint64& values);

    quint64 validBits() const;
    void    setValidBits(const quint64& validBits);

    bool    bitValue(const quint8& bit);
    void    setBitValue(const quint8& bit, const bool& value);

    bool    bitValidity(const quint8& bit);
    void    setBitValidity(const quint8& bit, const bool& validity);

private:
    QByteArray _value;
    QByteArray _validBits;
};

Q_DECLARE_METATYPE(QUaOptionSet);
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL

class QUaChangeStructureDataType
{
public:
    QUaChangeStructureDataType();
    QUaChangeStructureDataType(
            const QUaNodeId     &nodeIdAffected,
            const QUaNodeId     &nodeIdAffectedType,
            const QUaChangeVerb &uiVerb
            );
    QUaChangeStructureDataType(const QString& strChangeStructure);

    operator QString() const;

    QString toString() const;

    QUaNodeId _nodeIdAffected;
    QUaNodeId _nodeIdAffectedType;
    uchar     _uiVerb;

private:
    static QMetaEnum _metaEnumVerb;
};
using QUaChangesList = QList<QUaChangeStructureDataType>;

inline bool operator==(const QUaChangeStructureDataType& lhs, const QUaChangeStructureDataType& rhs)
{
    return lhs._nodeIdAffected     == rhs._nodeIdAffected     &&
            lhs._nodeIdAffectedType == rhs._nodeIdAffectedType &&
            lhs._uiVerb == rhs._uiVerb;
}

Q_DECLARE_METATYPE(QUaChangeStructureDataType);

// NOTE : automatic
//Q_DECLARE_METATYPE(QUaChangesList);

class QUaExclusiveLimitState
{
public:
    QUaExclusiveLimitState();
    QUaExclusiveLimitState(const QUa::ExclusiveLimitState& state);
    QUaExclusiveLimitState(const QString& strState);
    QUaExclusiveLimitState(const char* strState);

    void operator=(const QUa::ExclusiveLimitState& state);
    void operator=(const QString& strState);
    void operator=(const char* strState);
    bool operator==(const QUaExclusiveLimitState& other) const;
    bool operator==(const QUa::ExclusiveLimitState& other) const;

    operator QUa::ExclusiveLimitState() const;
    operator QString() const;

    QString toString() const;

private:
    QUa::ExclusiveLimitState _state;
    static QMetaEnum _metaEnum;
};

Q_DECLARE_METATYPE(QUaExclusiveLimitState);

class QUaExclusiveLimitTransition
{
public:
    QUaExclusiveLimitTransition();
    QUaExclusiveLimitTransition(const QUa::ExclusiveLimitTransition& transition);
    QUaExclusiveLimitTransition(const QString& strTransition);
    QUaExclusiveLimitTransition(const char* strTransition);

    void operator=(const QUa::ExclusiveLimitTransition& transition);
    void operator=(const QString& strTransition);
    void operator=(const char* strTransition);
    bool operator==(const QUaExclusiveLimitTransition& other) const;
    bool operator==(const QUa::ExclusiveLimitTransition& other) const;

    operator QUa::ExclusiveLimitTransition() const;
    operator QString() const;

    QString toString() const;

private:
    QUa::ExclusiveLimitTransition _transition;
    static QMetaEnum _metaEnum;
};

Q_DECLARE_METATYPE(QUaExclusiveLimitTransition);

// Enum Stuff
#include <QUaEnum>
// OptionSet Stuff
#include <QUaOptionSet>

// Class whose only pupose is emit signals
class QUaSignaler : public QObject
{
    Q_OBJECT
public:
    explicit QUaSignaler(QObject *parent = nullptr)
        : QObject(parent)
    {
        _processing = false;
        QObject::connect(
                    this,
                    &QUaSignaler::sendEvent,
                    this,
                    &QUaSignaler::on_sendEvent,
                    Qt::QueuedConnection
                    );
    };
    template <typename M1 = const std::function<void(void)>&>
    inline void execLater(M1&& func)
    {
        _funcs.enqueue(func);
        if (_processing)
        {
            return;
        }
        _processing = true;
        emit this->sendEvent(QPrivateSignal());
    };
    inline bool processing() const
    {
        return _processing;
    };
signals:
    void signalNewInstance(QUaNode *node);
    // can only be emitted internally
    void sendEvent(QPrivateSignal);
private slots:
    inline void on_sendEvent()
    {
        Q_ASSERT(_processing);
        if (_funcs.isEmpty())
        {
            _processing = false;
            return;
        }
        _funcs.dequeue()();
        emit this->sendEvent(QPrivateSignal());
    };
private:
    bool _processing;
    QQueue<std::function<void(void)>> _funcs;
};

class QUaSession : public QObject
{
    friend class QUaServer;
    Q_OBJECT

    Q_PROPERTY(QString   sessionId       READ sessionId      )
    Q_PROPERTY(QString   userName        READ userName       )
    Q_PROPERTY(QString   applicationName READ applicationName)
    Q_PROPERTY(QString   applicationUri  READ applicationUri )
    Q_PROPERTY(QString   productUri      READ productUri     )
    Q_PROPERTY(QString   address         READ address        )
    Q_PROPERTY(quint16   port            READ port           )
    Q_PROPERTY(QDateTime timestamp       READ timestamp      )

public:

    explicit QUaSession(QObject* parent = nullptr);

    QString   sessionId     () const;
    QString   userName       () const;
    QString   applicationName() const;
    QString   applicationUri () const;
    QString   productUri     () const;
    QString   address        () const;
    quint16   port           () const;
    QDateTime timestamp      () const;

private:
    QString   _strSessionId;
    QString   _strUserName;
    QString   _strApplicationName;
    QString   _strApplicationUri;
    QString   _strProductUri;
    QString   _strAddress;
    quint16   _intPort;
    QDateTime _timestamp;
};


struct QUaReferenceType
{
    QString strForwardName;
    QString strInverseName;
};
Q_DECLARE_METATYPE(QUaReferenceType);

QDebug operator<<(QDebug debug, const QUaReferenceType& refType);

// to have QUaReferenceType as a hash key
inline bool operator==(const QUaReferenceType& e1, const QUaReferenceType& e2)
{
    return e1.strForwardName.compare(e2.strForwardName, Qt::CaseSensitive) == 0
            && e1.strInverseName.compare(e2.strInverseName, Qt::CaseSensitive) == 0;
}

inline bool operator!=(const QUaReferenceType& e1, const QUaReferenceType& e2)
{
    return !(e1 == e2);
}

inline size_t qHash(const QUaReferenceType& key, size_t seed)
{
    return qHash(key.strForwardName, seed) ^ qHash(key.strInverseName, seed);
}

struct QUaForwardReference
{
    QUaNodeId targetNodeId;
    QString   targetType;
    QUaReferenceType refType;
};

inline bool operator==(const QUaForwardReference& e1, const QUaForwardReference& e2)
{
    return e1.targetNodeId == e2.targetNodeId
            && e1.targetType.compare(e2.targetType, Qt::CaseSensitive) == 0
            && e1.refType == e2.refType;
}

class QUaEventHistoryQueryData
{
    friend class QUaHistoryBackend;
public:
    QUaEventHistoryQueryData(
            const QDateTime &timeStartExisting     = QDateTime(),
            const quint64   &numEventsToRead       = 0,
            const quint64   & numEventsAlreadyRead = 0
            ) :
        _timeStartExisting   (timeStartExisting),
        _numEventsToRead     (numEventsToRead),
        _numEventsAlreadyRead(numEventsAlreadyRead)
    {};

    bool operator==(const QUaEventHistoryQueryData& other) const;
    bool isValid() const;

    static QByteArray toByteArray(const QUaEventHistoryQueryData& inQueryData);
    static QUaEventHistoryQueryData fromByteArray(const QByteArray& byteArray);

    using QUaEventHistoryContinuationPoint = QHash<QUaNodeId, QUaEventHistoryQueryData>;

    static QByteArray ContinuationToByteArray(const QUaEventHistoryContinuationPoint& inContinuation);
    static QUaEventHistoryContinuationPoint ContinuationFromByteArray(const QByteArray& byteArray);

    static UA_ByteString ContinuationToUaByteString(const QUaEventHistoryContinuationPoint& inContinuation);
    static QUaEventHistoryContinuationPoint ContinuationFromUaByteString(const UA_ByteString& uaByteArray);

private:
    QDateTime _timeStartExisting;
    quint64   _numEventsToRead;
    quint64   _numEventsAlreadyRead;
    friend QDataStream& operator<<(QDataStream& outStream, const QUaEventHistoryQueryData& inQueryData);
    friend QDataStream& operator>>(QDataStream& inStream, QUaEventHistoryQueryData& outQueryData);
};

using QUaEventHistoryContinuationPoint = QHash<QUaNodeId, QUaEventHistoryQueryData>;

inline QDataStream& operator<<(QDataStream& outStream, const QUaEventHistoryQueryData& inQueryData)
{
    outStream << inQueryData._timeStartExisting.toMSecsSinceEpoch();
    outStream << inQueryData._numEventsToRead;
    outStream << inQueryData._numEventsAlreadyRead;
    return outStream;
}

inline QDataStream& operator>>(QDataStream& inStream, QUaEventHistoryQueryData& outQueryData)
{
    qint64 timeStartExisting;
    inStream >> timeStartExisting;
    outQueryData._timeStartExisting = QDateTime::fromMSecsSinceEpoch(timeStartExisting, QTimeZone::UTC);
    inStream >> outQueryData._numEventsToRead;
    inStream >> outQueryData._numEventsAlreadyRead;
    return inStream;
}

///
/// \brief Field of a structured data type registered with QUaServer::registerStructure().
///
struct QUaStructureField
{
    QUaStructureField() = default;
    ///
    /// \brief Field of a built-in type, e.g. QMetaType::Double.
    ///
    QUaStructureField(const QString& name, const QMetaType::Type& type, bool isArray = false, bool isOptional = false);
    ///
    /// \brief Field of the data type \a dataTypeId: a built-in type, an enumeration or another structure.
    ///
    QUaStructureField(const QString& name, const QUaNodeId& dataTypeId, bool isArray = false, bool isOptional = false);

    QString   name;
    QUaNodeId dataTypeId;
    /// One-dimensional array of the data type.
    bool      isArray    = false;
    /// The field may be left out of a value; a structure with optional fields is encoded with their presence mask.
    bool      isOptional = false;

    bool operator==(const QUaStructureField& other) const;
};

///
/// \brief Value of a structured data type: the NodeId of the type and the values of its fields by name.
///        A field of a structure type holds a QUaStructure, an array field a list. Fields that are not set have
///        their default value, or are left out when optional.
///
class QUaStructure
{
public:
    QUaStructure() = default;
    explicit QUaStructure(const QUaNodeId& typeId);

    QUaNodeId typeId() const;
    ///
    /// \brief Tells whether the value has no type.
    ///
    bool isNull() const;

    ///
    /// \brief Returns the names of the fields that are set, in the order they were set.
    ///
    QStringList fieldNames() const;
    bool        hasField(const QString& name) const;
    ///
    /// \brief Returns the value of a field, invalid when it is not set.
    ///
    QVariant    field(const QString& name) const;
    void        setField(const QString& name, const QVariant& value);
    void        removeField(const QString& name);

    ///
    /// \brief Compares the type and the fields, regardless of the order they were set in.
    ///
    bool operator==(const QUaStructure& other) const;
    bool operator!=(const QUaStructure& other) const;

private:
    QUaNodeId _typeId;
    QList<QPair<QString, QVariant>> _fields;

    friend QDataStream& operator<<(QDataStream& outStream, const QUaStructure& inStructure);
    friend QDataStream& operator>>(QDataStream& inStream, QUaStructure& outStructure);
};

Q_DECLARE_METATYPE(QUaStructure);

inline QDataStream& operator<<(QDataStream& outStream, const QUaStructure& inStructure)
{
    outStream << inStructure._typeId;
    outStream << inStructure._fields;
    return outStream;
}

inline QDataStream& operator>>(QDataStream& inStream, QUaStructure& outStructure)
{
    inStream >> outStructure._typeId;
    inStream >> outStructure._fields;
    return inStream;
}

#define QMetaType_Structure static_cast<QMetaType::Type>(qMetaTypeId<QUaStructure>())

#endif // QUACUSTOMDATATYPES_H

