#include "quacustomdatatypes.h"

#include <QUaTypesConverter>
#include <QStringView>

/* NOTE : for registering new custom types wrapping open62541 types follow steps below:
- Create a wrapper class for the underlying open62541 type (e.g. QUaQualifiedName for UA_QualifiedName)
- Add constructors, equality operators converting the underlying type, string for serializaton
- Provide any helper methods and member accessors, register to Qt using Q_DECLARE_METATYPE
- Add a #define in quacustomdatatypes.h for the Qt type id qMetaTypeId<T> QMetaType_QualifiedName
- Add the type's UA_NODEID_, UA_TYPES_, etc to the static hashes of QUaDataType:: (below)
- Add to quatypesconverter.h and .cpp specilzations for templated convertion methods and add to switch statements
- Register QString converters in QUaTypesConverter::registerCustomTypes using QMetaType::registerConverter

If array of types supported:
- Add a #define in quacustomdatatypes.h for the Qt type id qMetaTypeId<QList<T>>QMetaType_List_QualifiedName
- Register QString converters in QUaTypesConverter::registerCustomTypes using QMetaType::registerConverter
*/
QHash<QString, QMetaType::Type> QUaDataType::_custTypesByName = {
	{QStringLiteral("Bool")                      , QMetaType::Bool                  },
	{QStringLiteral("Char")                      , QMetaType::Char                  },
	{QStringLiteral("SChar")                     , QMetaType::SChar                 },
	{QStringLiteral("UChar")                     , QMetaType::UChar                 },
	{QStringLiteral("Short")                     , QMetaType::Short                 },
	{QStringLiteral("UShort")                    , QMetaType::UShort                },
	{QStringLiteral("Int")                       , QMetaType::Int                   },
	{QStringLiteral("UInt")                      , QMetaType::UInt                  },
	{QStringLiteral("Long")                      , QMetaType::Long                  },
	{QStringLiteral("LongLong")                  , QMetaType::LongLong              },
	{QStringLiteral("ULong")                     , QMetaType::ULong                 },
	{QStringLiteral("ULongLong")                 , QMetaType::ULongLong             },
	{QStringLiteral("Float")                     , QMetaType::Float                 },
	{QStringLiteral("Double")                    , QMetaType::Double                },
	{QStringLiteral("QString")                   , QMetaType::QString               },
	{QStringLiteral("QDateTime")                 , QMetaType::QDateTime             },
	{QStringLiteral("QUuid")                     , QMetaType::QUuid                 },
	{QStringLiteral("QByteArray")                , QMetaType::QByteArray            },
	{QStringLiteral("QVariant")                  , QMetaType::QVariant              },
	{QStringLiteral("UnknownType")               , QMetaType::UnknownType           },
	{QStringLiteral("QUaNodeId")                 , QMetaType_NodeId                 },
	{QStringLiteral("QUaStatusCode")             , QMetaType_StatusCode             },
	{QStringLiteral("QUaQualifiedName")          , QMetaType_QualifiedName          },
	{QStringLiteral("QUaLocalizedText")          , QMetaType_LocalizedText          },
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
	// TODO : image
	{QStringLiteral("QImage")                    , QMetaType_Image                  },
	{QStringLiteral("QUaOptionSet")              , QMetaType_OptionSet              },
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	{QStringLiteral("QTimeZone")                 , QMetaType_TimeZone               },
	{QStringLiteral("QUaChangeStructureDataType"), QMetaType_ChangeStructureDataType}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
};

QHash<UA_NodeId, QMetaType::Type> QUaDataType::_custTypesByNodeId = {
	{UA_NODEID_NUMERIC(0, UA_NS0ID_BOOLEAN)                     , QMetaType::Bool                  },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_SBYTE)                       , QMetaType::Char                  },
	//{UA_NODEID_NUMERIC(0, UA_NS0ID_SBYTE)                       , QMetaType::SChar                 },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_BYTE)                        , QMetaType::UChar                 },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_INT16)                       , QMetaType::Short                 },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_UINT16)                      , QMetaType::UShort                },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_INT32)                       , QMetaType::Int                   },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_UINT32)                      , QMetaType::UInt                  },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_INT64)                       , QMetaType::Long                  },
	//{UA_NODEID_NUMERIC(0, UA_NS0ID_INT64)                       , QMetaType::LongLong              },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_UINT64)                      , QMetaType::ULong                 },
	//{UA_NODEID_NUMERIC(0, UA_NS0ID_UINT64)                      , QMetaType::ULongLong             },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_FLOAT)                       , QMetaType::Float                 },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_DOUBLE)                      , QMetaType::Double                },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_STRING)                      , QMetaType::QString               },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_DATETIME)                    , QMetaType::QDateTime             },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_UTCTIME)                     , QMetaType::QDateTime             },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_GUID)                        , QMetaType::QUuid                 },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_BYTESTRING)                  , QMetaType::QByteArray            },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE)                , QMetaType::QVariant              },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_NODEID)                      , QMetaType_NodeId                 },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_STATUSCODE)                  , QMetaType_StatusCode             },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_QUALIFIEDNAME)               , QMetaType_QualifiedName          },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_LOCALIZEDTEXT)               , QMetaType_LocalizedText          },
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
	{UA_NODEID_NUMERIC(0, UA_NS0ID_IMAGE)                       , QMetaType_Image                  },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_OPTIONSET)                   , QMetaType_OptionSet              },
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	{UA_NODEID_NUMERIC(0, UA_NS0ID_TIMEZONEDATATYPE)            , QMetaType_TimeZone               },
	{UA_NODEID_NUMERIC(0, UA_NS0ID_MODELCHANGESTRUCTUREDATATYPE), QMetaType_ChangeStructureDataType}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
};

QHash<int, QMetaType::Type> QUaDataType::_custTypesByTypeIndex = {
	{UA_TYPES_BOOLEAN                     , QMetaType::Bool                  },
	{UA_TYPES_SBYTE                       , QMetaType::Char                  },
	//{UA_TYPES_SBYTE                       , QMetaType::SChar                 },
	{UA_TYPES_BYTE                        , QMetaType::UChar                 },
	{UA_TYPES_INT16                       , QMetaType::Short                 },
	{UA_TYPES_UINT16                      , QMetaType::UShort                },
	{UA_TYPES_INT32                       , QMetaType::Int                   },
	{UA_TYPES_UINT32                      , QMetaType::UInt                  },
	{UA_TYPES_INT64                       , QMetaType::Long                  },
	//{UA_TYPES_INT64                       , QMetaType::LongLong              },
	{UA_TYPES_UINT64                      , QMetaType::ULong                 },
	//{UA_TYPES_UINT64                      , QMetaType::ULongLong             },
	{UA_TYPES_FLOAT                       , QMetaType::Float                 },
	{UA_TYPES_DOUBLE                      , QMetaType::Double                },
	{UA_TYPES_STRING                      , QMetaType::QString               },
	{UA_TYPES_DATETIME                    , QMetaType::QDateTime             },
	{UA_TYPES_GUID                        , QMetaType::QUuid                 },
	{UA_TYPES_BYTESTRING                  , QMetaType::QByteArray            },
	{UA_TYPES_VARIANT                     , QMetaType::QVariant              },
	{UA_TYPES_NODEID                      , QMetaType_NodeId                 },
	{UA_TYPES_STATUSCODE                  , QMetaType_StatusCode             },
	{UA_TYPES_QUALIFIEDNAME               , QMetaType_QualifiedName          },
	{UA_TYPES_LOCALIZEDTEXT               , QMetaType_LocalizedText          },
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
	{UA_TYPES_IMAGEPNG                    , QMetaType_Image                  },
	{UA_TYPES_OPTIONSET                   , QMetaType_OptionSet              },
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	{UA_TYPES_TIMEZONEDATATYPE            , QMetaType_TimeZone               },
	{UA_TYPES_MODELCHANGESTRUCTUREDATATYPE, QMetaType_ChangeStructureDataType}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
};

QHash<QMetaType::Type, QUaDataType::TypeData> QUaDataType::_custTypesByType = {
	{ QMetaType::Bool                   , {QStringLiteral("Bool")                       , UA_NODEID_NUMERIC(0, UA_NS0ID_BOOLEAN)                     , &UA_TYPES[UA_TYPES_BOOLEAN                     ]} },
	{ QMetaType::Char                   , {QStringLiteral("Char")                       , UA_NODEID_NUMERIC(0, UA_NS0ID_SBYTE)                       , &UA_TYPES[UA_TYPES_SBYTE                       ]} },
	{ QMetaType::SChar                  , {QStringLiteral("SChar")                      , UA_NODEID_NUMERIC(0, UA_NS0ID_SBYTE)                       , &UA_TYPES[UA_TYPES_SBYTE                       ]} },
	{ QMetaType::UChar                  , {QStringLiteral("UChar")                      , UA_NODEID_NUMERIC(0, UA_NS0ID_BYTE)                        , &UA_TYPES[UA_TYPES_BYTE                        ]} },
	{ QMetaType::Short                  , {QStringLiteral("Short")                      , UA_NODEID_NUMERIC(0, UA_NS0ID_INT16)                       , &UA_TYPES[UA_TYPES_INT16                       ]} },
	{ QMetaType::UShort                 , {QStringLiteral("UShort")                     , UA_NODEID_NUMERIC(0, UA_NS0ID_UINT16)                      , &UA_TYPES[UA_TYPES_UINT16                      ]} },
	{ QMetaType::Int                    , {QStringLiteral("Int")                        , UA_NODEID_NUMERIC(0, UA_NS0ID_INT32)                       , &UA_TYPES[UA_TYPES_INT32                       ]} },
	{ QMetaType::UInt                   , {QStringLiteral("UInt")                       , UA_NODEID_NUMERIC(0, UA_NS0ID_UINT32)                      , &UA_TYPES[UA_TYPES_UINT32                      ]} },
	{ QMetaType::Long                   , {QStringLiteral("Long")                       , UA_NODEID_NUMERIC(0, UA_NS0ID_INT64)                       , &UA_TYPES[UA_TYPES_INT64                       ]} },
	{ QMetaType::LongLong               , {QStringLiteral("LongLong")                   , UA_NODEID_NUMERIC(0, UA_NS0ID_INT64)                       , &UA_TYPES[UA_TYPES_INT64                       ]} },
	{ QMetaType::ULong                  , {QStringLiteral("ULong")                      , UA_NODEID_NUMERIC(0, UA_NS0ID_UINT64)                      , &UA_TYPES[UA_TYPES_UINT64                      ]} },
	{ QMetaType::ULongLong              , {QStringLiteral("ULongLong")                  , UA_NODEID_NUMERIC(0, UA_NS0ID_UINT64)                      , &UA_TYPES[UA_TYPES_UINT64                      ]} },
	{ QMetaType::Float                  , {QStringLiteral("Float")                      , UA_NODEID_NUMERIC(0, UA_NS0ID_FLOAT)                       , &UA_TYPES[UA_TYPES_FLOAT                       ]} },
	{ QMetaType::Double                 , {QStringLiteral("Double")                     , UA_NODEID_NUMERIC(0, UA_NS0ID_DOUBLE)                      , &UA_TYPES[UA_TYPES_DOUBLE                      ]} },
	{ QMetaType::QString                , {QStringLiteral("QString")                    , UA_NODEID_NUMERIC(0, UA_NS0ID_STRING)                      , &UA_TYPES[UA_TYPES_STRING                      ]} },
	{ QMetaType::QDateTime              , {QStringLiteral("QDateTime")                  , UA_NODEID_NUMERIC(0, UA_NS0ID_DATETIME)                    , &UA_TYPES[UA_TYPES_DATETIME                    ]} },
	{ QMetaType::QUuid                  , {QStringLiteral("QUuid")                      , UA_NODEID_NUMERIC(0, UA_NS0ID_GUID)                        , &UA_TYPES[UA_TYPES_GUID                        ]} },
	{ QMetaType::QByteArray             , {QStringLiteral("QByteArray")                 , UA_NODEID_NUMERIC(0, UA_NS0ID_BYTESTRING)                  , &UA_TYPES[UA_TYPES_BYTESTRING                  ]} },
	{ QMetaType::QVariant               , {QStringLiteral("QVariant")                   , UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE)                , &UA_TYPES[UA_TYPES_VARIANT                     ]} },
	{ QMetaType::UnknownType            , {QStringLiteral("UnknownType")                , UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE)                , &UA_TYPES[UA_TYPES_VARIANT                     ]} },
	{ QMetaType_NodeId                  , {QStringLiteral("QUaNodeId")                  , UA_NODEID_NUMERIC(0, UA_NS0ID_NODEID)                      , &UA_TYPES[UA_TYPES_NODEID                      ]} },
	{ QMetaType_StatusCode              , {QStringLiteral("QUaStatusCode")              , UA_NODEID_NUMERIC(0, UA_NS0ID_STATUSCODE)                  , &UA_TYPES[UA_TYPES_STATUSCODE                  ]} },
	{ QMetaType_QualifiedName           , {QStringLiteral("QUaQualifiedName")           , UA_NODEID_NUMERIC(0, UA_NS0ID_QUALIFIEDNAME)               , &UA_TYPES[UA_TYPES_QUALIFIEDNAME               ]} },
	{ QMetaType_LocalizedText           , {QStringLiteral("QUaLocalizedText")           , UA_NODEID_NUMERIC(0, UA_NS0ID_LOCALIZEDTEXT)               , &UA_TYPES[UA_TYPES_LOCALIZEDTEXT               ]} },
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
	// TODO : image
	// NOTE : QMetaType_Image is the same as QMetaType::QByteArray, which must map to ByteString
	//        (ImagePNG is a different data type since open62541 v1.3)
	{ QMetaType_OptionSet               , {QStringLiteral("QUaOptionSet")               , UA_NODEID_NUMERIC(0, UA_NS0ID_OPTIONSET)                   , &UA_TYPES[UA_TYPES_OPTIONSET                   ]} },
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	{ QMetaType_TimeZone                , {QStringLiteral("QTimeZone")                  , UA_NODEID_NUMERIC(0, UA_NS0ID_TIMEZONEDATATYPE)            , &UA_TYPES[UA_TYPES_TIMEZONEDATATYPE            ]} },
	{ QMetaType_ChangeStructureDataType , {QStringLiteral("QUaChangeStructureDataType") , UA_NODEID_NUMERIC(0, UA_NS0ID_MODELCHANGESTRUCTUREDATATYPE), &UA_TYPES[UA_TYPES_MODELCHANGESTRUCTUREDATATYPE]} }
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
};

QUaDataType::QUaDataType()
	: _type(QMetaType::UnknownType)
{
}

QUaDataType::QUaDataType(const QMetaType::Type& metaType)
	: _type(metaType)
{
}

QUaDataType::QUaDataType(const QString& strType)
{
	*this = strType;
}

QUaDataType::operator QMetaType::Type() const
{
	return _type;
}

QUaDataType::operator QString() const
{
	return QUaDataType::stringByQType(_type);
}

bool QUaDataType::operator==(const QMetaType::Type& metaType)
{
	return _type == metaType;
}

void QUaDataType::operator=(const QString& strType)
{
	Q_ASSERT_X(QUaDataType::_custTypesByName.contains(strType), "QUaDataType", "Unknown type");
	if (!QUaDataType::_custTypesByName.contains(strType))
	{
		_type = QMetaType::UnknownType;
		return;
	}
	_type = QUaDataType::_custTypesByName[strType];
}

bool QUaDataType::isSupportedQType(const QMetaType::Type& type)
{
	return _custTypesByType.contains(type);
}

QMetaType::Type QUaDataType::qTypeByNodeId(const UA_NodeId& nodeId)
{
	// unmapped types are expected, e.g. session diagnostics variables created by open62541 >= 1.4
	return _custTypesByNodeId.value(nodeId, QMetaType::UnknownType);
}

QMetaType::Type QUaDataType::qTypeByTypeIndex(const int& typeIndex)
{
	Q_ASSERT(_custTypesByTypeIndex.contains(typeIndex));
	return _custTypesByTypeIndex.value(typeIndex, QMetaType::UnknownType);
}

UA_NodeId QUaDataType::nodeIdByQType(const QMetaType::Type& type)
{
	Q_ASSERT(_custTypesByType.contains(type));
	if (!_custTypesByType.contains(type))
	{
		return UA_NODEID_NULL;
	}
	return _custTypesByType[type].nodeId;
}

const UA_DataType* QUaDataType::dataTypeByQType(const QMetaType::Type& type)
{
	Q_ASSERT(_custTypesByType.contains(type));
	if (!_custTypesByType.contains(type))
	{
		return nullptr;
	}
	return _custTypesByType[type].dataType;
}

QString QUaDataType::stringByQType(const QMetaType::Type& type)
{
	Q_ASSERT(_custTypesByType.contains(type));
	if (!_custTypesByType.contains(type))
	{
		return QStringLiteral("UnknownType");
	}
	return _custTypesByType[type].name;
}

QMetaEnum QUaStatusCode::_metaEnum = QMetaEnum::fromType<QUa::Status>();

QHash<QUaStatus, QString> QUaStatusCode::_descriptions =
[]() -> QHash<QUaStatus, QString> {
	QHash<QUaStatus, QString> retHash;
	retHash[QUaStatus::Good                                   ] = QObject::tr("The operation was successful and the associated results may be used"                                       );
	retHash[QUaStatus::GoodLocalOverride                      ] = QObject::tr("The value has been overridden"                                                                             );
	retHash[QUaStatus::Uncertain                              ] = QObject::tr("The operation was partially successful and that associated results might not be suitable for some purposes");
	retHash[QUaStatus::UncertainNoCommunicationLastUsableValue] = QObject::tr("Communication to the data source has failed. The variable value is the last value that had a good quality" );
	retHash[QUaStatus::UncertainLastUsableValue               ] = QObject::tr("Whatever was updating this value has stopped doing so"                                                     );
	retHash[QUaStatus::UncertainSubstituteValue               ] = QObject::tr("The value is an operational value that was manually overwritten"                                           );
	retHash[QUaStatus::UncertainInitialValue                  ] = QObject::tr("The value is an initial value for a variable that normally receives its value from another variable"       );
	retHash[QUaStatus::UncertainSensorNotAccurate             ] = QObject::tr("The value is at one of the sensor limits"                                                                  );
	retHash[QUaStatus::UncertainEngineeringUnitsExceeded      ] = QObject::tr("The value is outside of the range of values defined for this parameter"                                    );
	retHash[QUaStatus::UncertainSubNormal                     ] = QObject::tr("The value is derived from multiple sources and has less than the required number of Good sources"            );
	retHash[QUaStatus::Bad                                    ] = QObject::tr("The operation failed and any associated results cannot be used"                                            );
	retHash[QUaStatus::BadConfigurationError                  ] = QObject::tr("There is a problem with the configuration that affects the usefulness of the value"                        );
	retHash[QUaStatus::BadNotConnected                        ] = QObject::tr("The variable should receive its value from another variable, but has never been configured to do so"       );
	retHash[QUaStatus::BadDeviceFailure                       ] = QObject::tr("There has been a failure in the device/data source that generates the value that has affected the value"   );
	retHash[QUaStatus::BadSensorFailure                       ] = QObject::tr("There has been a failure in the sensor from which the value is derived by the device/data source"          );
	retHash[QUaStatus::BadOutOfService                        ] = QObject::tr("The source of the data is not operational"                                                                 );
	retHash[QUaStatus::BadDeadbandFilterInvalid               ] = QObject::tr("The deadband filter is not valid"                                                                          );
	return retHash;
}();
QString QUaStatusCode::longDescription(const QUaStatusCode& statusCode)
{
	return QUaStatusCode::_descriptions.value(
		statusCode, 
		QObject::tr("Unknown description value %1")
			.arg(static_cast<quint32>(statusCode))
	);
}

QUaStatusCode::QUaStatusCode()
{
	_status = QUaStatus::Good;
}

QUaStatusCode::QUaStatusCode(const QUaStatus& uaStatus)
{
	_status = uaStatus;
}

QUaStatusCode::QUaStatusCode(const UA_StatusCode& intStatus)
{
	_status = static_cast<QUaStatus>(intStatus);
}

QUaStatusCode::QUaStatusCode(const QString& strStatus)
{
	*this = QUaStatusCode(strStatus.toUtf8());
}

QUaStatusCode::QUaStatusCode(const QByteArray& byteStatus)
{
	bool ok = false;
	int val = _metaEnum.keyToValue(byteStatus.constData(), &ok);
	_status = static_cast<QUaStatus>(val);
}

QUaStatusCode::operator QUaStatus() const
{
	return static_cast<QUaStatus>(_status);
}

QUaStatusCode::operator UA_StatusCode() const
{
	return static_cast<UA_StatusCode>(_status);
}

QUaStatusCode::operator QString() const
{
	const char* code = _metaEnum.valueToKey(static_cast<int>(_status));
	if (!code)
	{
		code = UA_StatusCode_name(static_cast<UA_StatusCode>(_status));
	}
	return QString::fromUtf8(code);
}

bool QUaStatusCode::operator==(const QUaStatus& uaStatus) const
{
    return _status == uaStatus;
}

void QUaStatusCode::operator=(const QString& strStatus)
{
	*this = QUaStatusCode(strStatus.toUtf8());
}

QUaQualifiedName::QUaQualifiedName() : _namespace(0)
{
}

QUaQualifiedName::QUaQualifiedName(const quint16& namespaceIndex, const QString& name) :
	_namespace(namespaceIndex),
	_name(name)
{
}

QUaQualifiedName::QUaQualifiedName(const UA_QualifiedName& uaQualName)
{
	// use overloaded equality operator
	*this = uaQualName;
}

QUaQualifiedName::QUaQualifiedName(const QString& strXmlQualName)
{
	// use overloaded equality operator
	*this = strXmlQualName;
}

QUaQualifiedName::QUaQualifiedName(const char* strXmlQualName)
{
	// use overloaded equality operator
	*this = strXmlQualName;
}

QUaQualifiedName::operator UA_QualifiedName() const
{
	UA_QualifiedName browseName;
	browseName.namespaceIndex = _namespace;
	browseName.name = QUaTypesConverter::uaStringFromQString(_name); // NOTE : allocs
	return browseName;
}

QUaQualifiedName::operator QString() const
{
	return QStringLiteral("ns=%1;s=%2").arg(_namespace).arg(_name);
}

void QUaQualifiedName::operator=(const UA_QualifiedName& uaQualName)
{
	_namespace = uaQualName.namespaceIndex;
	_name = QUaTypesConverter::uaStringToQString(uaQualName.name);
}

void QUaQualifiedName::operator=(const QString& strXmlQualName)
{
	_namespace = 0;
	auto components = QStringView(strXmlQualName).split(QLatin1Char(';'));
	// check if valid xml format
	if (components.size() != 2)
	{
		// if no valid xml format, assume ns = 0 and given string is name
		_name = strXmlQualName;
		return;
	}
	// check if valid namespace found, else assume ns = 0 and given string is name
    quint16 new_ns = _namespace;
	if (components.size() == 2 && components.at(0).startsWith(QLatin1String("ns=")))
	{
		bool success = false;
		uint ns = components.at(0).mid(3).toUInt(&success);
		if (!success || ns > (std::numeric_limits<quint16>::max)())
		{
			_name = strXmlQualName;
			return;
		}
		new_ns = ns;
	}
	// check if valid name found, else assume ns = 0 and given string is name
	auto& strLast = components.last();
	if (!strLast.contains(QLatin1String("i=")) &&
		!strLast.contains(QLatin1String("s=")) &&
		!strLast.contains(QLatin1String("g=")) &&
		!strLast.contains(QLatin1String("b=")))
	{
		_name = strXmlQualName;
		return;
	}
	auto lastParts = strLast.split(QLatin1Char('='));
	// if reached here, xml format is correct
	_namespace = new_ns;
	_name = 
		lastParts.size() == 1 ?
		QString() : // NOTE : possible that just "s="
		lastParts.size() == 2 ?
		lastParts.last().toString() :
		lastParts.at(1).toString();
}

void QUaQualifiedName::operator=(const char* strXmlQualName)
{
	// use overloaded equality operator
	*this = QString::fromUtf8(strXmlQualName);
}

bool QUaQualifiedName::operator==(const QUaQualifiedName& other) const
{
	return _namespace == other._namespace &&
		_name.compare(other._name, Qt::CaseSensitive) == 0;
}

bool QUaQualifiedName::operator!=(const QUaQualifiedName& other) const
{
	return _namespace != other._namespace ||
		_name.compare(other._name, Qt::CaseSensitive) != 0;
}

bool QUaQualifiedName::operator<(const QUaQualifiedName& other) const
{
	if (_namespace != other._namespace)
	{
		return _namespace < other._namespace;
	}
	return _name < other._name;
}

quint16 QUaQualifiedName::namespaceIndex() const
{
	return _namespace;
}

void QUaQualifiedName::setNamespaceIndex(const quint16& index)
{
	_namespace = index;
}

QString QUaQualifiedName::name() const
{
	return _name;
}

void QUaQualifiedName::setName(const QString& name)
{
	_name = name;
}

QString QUaQualifiedName::toXmlString() const
{
	// use ::operator QString()
	return *this;
}

UA_QualifiedName QUaQualifiedName::toUaQualifiedName() const
{
	// use ::operator UA_QualifiedName()
	return *this;
}

bool QUaQualifiedName::isEmpty() const
{
	return _name.isEmpty();
}

QUaQualifiedName QUaQualifiedName::fromXmlString(const QString& strXmlQualName)
{
	return QUaQualifiedName(strXmlQualName);
}

QUaQualifiedName QUaQualifiedName::fromUaQualifiedName(const UA_QualifiedName& uaQualName)
{
	return QUaQualifiedName(uaQualName);
}

QUaBrowsePath QUaQualifiedName::saoToBrowsePath(const UA_SimpleAttributeOperand* sao)
{
	QUaBrowsePath browsePath;
	for (size_t i = 0; i < sao->browsePathSize; i++)
	{
		browsePath << sao->browsePath[i];
	}
	return browsePath;
}

QString QUaQualifiedName::reduceXml(const QUaBrowsePath& browsePath)
{
	QString strRet;
	if (browsePath.count() == 1)
	{
		return browsePath.first().toXmlString();
	}
	std::for_each(browsePath.begin(), browsePath.end(),
	[&strRet](const QUaQualifiedName& browseName) {
		strRet += QLatin1Char('/') + browseName.toXmlString();
	});
	return strRet;
}

QString QUaQualifiedName::reduceName(
	const QUaBrowsePath& browsePath, 
	const QString& separator/* = QStringLiteral("/")*/
)
{
	if (browsePath.count() == 1)
	{
		return browsePath.first().name();
	}
	QString strRet;
	auto it = browsePath.begin();
	while (it != browsePath.end())
	{
		strRet += it->name();
		if (++it != browsePath.end())
		{
			strRet += separator;
		}
	}
	return strRet;
}

QUaBrowsePath QUaQualifiedName::expandName(const QString& strPath, const QString& separator)
{
	QUaBrowsePath retPath;
	auto parts = QStringView(strPath).split(separator);
	for (int i = 0; i < parts.count(); i++)
	{
		retPath << QUaQualifiedName(0, parts[i].toString());
	}
	return retPath;
}

QMetaEnum QUaChangeStructureDataType::_metaEnumVerb = QMetaEnum::fromType<QUa::ChangeVerb>();

QUaChangeStructureDataType::QUaChangeStructureDataType()
	: _uiVerb(static_cast<uchar>(QUaChangeVerb::NodeAdded))
{
}

QUaChangeStructureDataType::QUaChangeStructureDataType(
	const QUaNodeId& nodeIdAffected,
	const QUaNodeId& nodeIdAffectedType,
	const QUaChangeVerb& uiVerb)
	: _nodeIdAffected(nodeIdAffected),
	_nodeIdAffectedType(nodeIdAffectedType),
	_uiVerb(static_cast<uchar>(uiVerb))
{
}

QUaChangeStructureDataType::QUaChangeStructureDataType(const QString& strChangeStructure)
{
	auto components = QStringView(strChangeStructure).split(QLatin1Char('|'));
	if (components.count() == 0)
	{
		return;
	}
	if (components.count() >= 1)
	{
		_nodeIdAffected = components.at(0).toString();
	}
	if (components.count() >= 2)
	{
		_nodeIdAffectedType = components.at(1).toString();;
	}
	if (components.count() >= 3)
	{
		bool ok = false;
		auto byte = components.at(2).toUtf8();
		int val = _metaEnumVerb.keyToValue(byte.data(), &ok);
		_uiVerb = ok ? static_cast<uchar>(val) : static_cast<uchar>(QUaChangeVerb::NodeAdded);
	}
}

QUaChangeStructureDataType::operator QString() const
{
	const char* verb = _metaEnumVerb.valueToKey(static_cast<int>(_uiVerb));
	return QStringLiteral("%1|%2|%3")
		.arg(_nodeIdAffected)
		.arg(_nodeIdAffectedType)
		.arg(QString::fromUtf8(verb));
}

QString QUaChangeStructureDataType::toString() const
{
	return *this;
}

QUaSession::QUaSession(QObject* parent/* = 0*/) :
	QObject(parent),
	_intPort(0),
	_timestamp(QDateTime::currentDateTimeUtc())
{
}

QString QUaSession::sessionId() const
{
	return _strSessionId;
}

QString QUaSession::userName() const
{
	return _strUserName;
}

QString QUaSession::applicationName() const
{
	return _strApplicationName;
}

QString QUaSession::applicationUri() const
{
	return _strApplicationUri;
}

QString QUaSession::productUri() const
{
	return _strProductUri;
}

QString QUaSession::address() const
{
	return _strAddress;
}

quint16 QUaSession::port() const
{
	return _intPort;
}

QDateTime QUaSession::timestamp() const
{
	return _timestamp;
}

QUaLocalizedText::QUaLocalizedText()
{
}

QUaLocalizedText::QUaLocalizedText(const QString& locale, const QString& text) :
	_locale(locale),
	_text(text)
{
}

QUaLocalizedText::QUaLocalizedText(const char* locale, const char* text)
{
	*this = QUaLocalizedText(QString::fromUtf8(locale), QString::fromUtf8(text));
}

QUaLocalizedText::QUaLocalizedText(const UA_LocalizedText& uaLocalizedText)
{
	*this = uaLocalizedText;
}

QUaLocalizedText::QUaLocalizedText(const QString& strXmlLocalizedText)
{
	*this = strXmlLocalizedText;
}

QUaLocalizedText::QUaLocalizedText(const char* strXmlLocalizedText)
{
	*this = strXmlLocalizedText;
}

QUaLocalizedText::operator UA_LocalizedText() const
{
	UA_LocalizedText uaLocalizedText;
	uaLocalizedText.locale = QUaTypesConverter::uaStringFromQString(_locale);
	uaLocalizedText.text = QUaTypesConverter::uaStringFromQString(_text);
	return uaLocalizedText;
}

QUaLocalizedText::operator QString() const
{
	return _locale.isEmpty() ? _text : QStringLiteral("l=%1;t=%2").arg(_locale).arg(_text);
}

void QUaLocalizedText::operator=(const UA_LocalizedText& uaLocalizedText)
{
	_locale = QUaTypesConverter::uaStringToQString(uaLocalizedText.locale);
	_text = QUaTypesConverter::uaStringToQString(uaLocalizedText.text);
}

void QUaLocalizedText::operator=(const QString& strXmlLocalizedText)
{
	_locale = QString();
	auto components = QStringView(strXmlLocalizedText).split(QLatin1Char(';'));
	// check if valid xml format
	if (components.size() != 2)
	{
		// if no valid xml format, assume no-locale, and given string is text
		_text = strXmlLocalizedText;
		return;
	}
	// check if valid locale found, else assume no-locale
	QString new_locale;
	if (components.at(0).contains(QLatin1String("l="))) 
	{
		auto partsLocale = components.at(0).split(QLatin1Char('='));
		if (partsLocale.size() < 2)
		{
			_text = strXmlLocalizedText;
			return;
		}
		new_locale = 
			partsLocale.size() == 2 ?
			partsLocale.last().toString() :
			partsLocale.at(1).toString();
	}
	// check if valid text found, else assume no-locale and given string is name
	if (!components.last().contains(QLatin1String("t=")))
	{
		_text = strXmlLocalizedText;
		return;
	}
	auto partsText = components.last().split(QLatin1Char('='));
	// if reached here, xml format is correct
	_locale = new_locale;
	_text =
		partsText.size() == 1 ?
		QString() : // NOTE : possible that just "t="
		partsText.size() == 2 ?
		partsText.last().toString() :
		partsText.at(1).toString();
}

void QUaLocalizedText::operator=(const char* strXmlLocalizedText)
{
	*this = QString::fromUtf8(strXmlLocalizedText);
}

bool QUaLocalizedText::operator==(const QUaLocalizedText& other) const
{
	return this->_locale.compare(other._locale, Qt::CaseSensitive) == 0 &&
		this->_text.compare(other._text, Qt::CaseSensitive) == 0;
}

bool QUaLocalizedText::operator<(const QUaLocalizedText& other) const
{
	if (_locale != other._locale)
	{
		return _locale < other._locale;
	}
	return _text < other._text;
}

QString QUaLocalizedText::locale() const
{
	return _locale;
}

void QUaLocalizedText::setLocale(const QString& locale)
{
	_locale = locale;
}

QString QUaLocalizedText::text() const
{
	return _text;
}

void QUaLocalizedText::setText(const QString& text)
{
	_text = text;
}

QString QUaLocalizedText::toXmlString() const
{
	// use ::operator QString()
	return *this;
}

UA_LocalizedText QUaLocalizedText::toUaLocalizedText() const
{
	// use ::operator UA_LocalizedText()
	return *this;
}

QUaNodeId::QUaNodeId() : _nodeId(UA_NODEID_NULL)
{
}
QUaNodeId::QUaNodeId(const quint16& index, const quint32& numericId)
	: _nodeId(UA_NODEID_NULL)
{
	this->setNamespaceIndex(index);
	this->setNumericId(numericId);
}

QUaNodeId::QUaNodeId(const quint16& index, const QString& stringId)
	: _nodeId(UA_NODEID_NULL)
{
	this->setNamespaceIndex(index);
	this->setStringId(stringId);
}

QUaNodeId::QUaNodeId(const quint16& index, const char* stringId)
	: _nodeId(UA_NODEID_NULL)
{
	*this = QUaNodeId(index, QString::fromUtf8(stringId));
}

QUaNodeId::QUaNodeId(const quint16& index, const QUuid& uuId)
	: _nodeId(UA_NODEID_NULL)
{
	this->setNamespaceIndex(index);
	this->setUuId(uuId);
}

QUaNodeId::QUaNodeId(const quint16& index, const QByteArray& byteArrayId)
	: _nodeId(UA_NODEID_NULL)
{
	this->setNamespaceIndex(index);
	this->setByteArrayId(byteArrayId);
}

QUaNodeId::QUaNodeId(const QUaNodeId& other)
	: _nodeId(UA_NODEID_NULL)
{
	*this = other;
}

QUaNodeId::QUaNodeId(const UA_NodeId& uaNodeId)
	: _nodeId(UA_NODEID_NULL)
{
	*this = uaNodeId;
}

QUaNodeId::QUaNodeId(const QString& strXmlNodeId)
	: _nodeId(UA_NODEID_NULL)
{
	*this = strXmlNodeId;
}

QUaNodeId::QUaNodeId(const char* strXmlNodeId)
	: _nodeId(UA_NODEID_NULL)
{
	*this = strXmlNodeId;
}

QUaNodeId::~QUaNodeId()
{
	this->clear();
}

void QUaNodeId::operator=(const UA_NodeId& uaNodeId)
{
	this->clear();
	UA_NodeId_copy(&uaNodeId, &this->_nodeId);
}

void QUaNodeId::operator=(const QString& strXmlNodeId)
{
	this->clear();
	_nodeId = QUaTypesConverter::nodeIdFromQString(strXmlNodeId);
}

void QUaNodeId::operator=(const char* strXmlNodeId)
{
	*this = QString::fromUtf8(strXmlNodeId);
}

void QUaNodeId::operator=(const QUaNodeId& other)
{
	this->clear();
	UA_NodeId_copy(&other._nodeId, &this->_nodeId);
}

QUaNodeId::operator UA_NodeId() const
{
	UA_NodeId retNodeId;
	UA_NodeId_copy(&_nodeId, &retNodeId);
	return retNodeId;
}

QUaNodeId::operator QString() const
{
	return QUaTypesConverter::nodeIdToQString(_nodeId);
}

bool QUaNodeId::operator==(const QUaNodeId& other) const
{
	return UA_NodeId_equal(&this->_nodeId, &other._nodeId);
}

bool QUaNodeId::operator!=(const QUaNodeId& other) const
{
	return !UA_NodeId_equal(&this->_nodeId, &other._nodeId);
}

bool QUaNodeId::operator==(const UA_NodeId& other) const
{
	return UA_NodeId_equal(&this->_nodeId, &other);
}

bool QUaNodeId::operator<(const QUaNodeId& other) const
{
	if (_nodeId.namespaceIndex != other._nodeId.namespaceIndex)
	{
		return _nodeId.namespaceIndex < other._nodeId.namespaceIndex;
	}
	switch (this->type())
	{
	case QUaNodeIdType::Numeric:
		return this->numericId() < other.numericId();
	case QUaNodeIdType::String:
		return this->stringId() < other.stringId();
	case QUaNodeIdType::Guid:
		return this->uuId() < other.uuId();
	case QUaNodeIdType::ByteString:
		return this->byteArrayId() < other.byteArrayId();
	default:
		return true;
	}
}

quint16 QUaNodeId::namespaceIndex() const
{
	return _nodeId.namespaceIndex;
}

void QUaNodeId::setNamespaceIndex(const quint16& index)
{
	_nodeId.namespaceIndex = index;
}

QUaNodeIdType QUaNodeId::type() const
{
	return static_cast<QUaNodeIdType>(_nodeId.identifierType);
}

quint32 QUaNodeId::numericId() const
{
	return _nodeId.identifier.numeric;
}

void QUaNodeId::setNumericId(const quint32& numericId)
{
	if (_nodeId.identifierType != UA_NodeIdType::UA_NODEIDTYPE_NUMERIC)
	{
		auto index = _nodeId.namespaceIndex;
		this->clear();
		_nodeId.namespaceIndex = index;
		_nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_NUMERIC;
	}
	_nodeId.identifier.numeric = numericId;
}

QString QUaNodeId::stringId() const
{
	return QUaTypesConverter::uaStringToQString(_nodeId.identifier.string);
}

void QUaNodeId::setStringId(const QString& stringId)
{
	if (_nodeId.identifierType != UA_NodeIdType::UA_NODEIDTYPE_STRING)
	{
		auto index = _nodeId.namespaceIndex;
		this->clear();
		_nodeId.namespaceIndex = index;
		_nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_STRING;
	}
	QUaTypesConverter::uaVariantFromQVariantScalar<UA_String, QString>(stringId, &_nodeId.identifier.string);
}

QUuid QUaNodeId::uuId() const
{
	return QUaTypesConverter::uaVariantToQVariantScalar<QUuid, UA_Guid>(&_nodeId.identifier.guid);
}

void QUaNodeId::setUuId(const QUuid& uuId)
{
	if (_nodeId.identifierType != UA_NodeIdType::UA_NODEIDTYPE_GUID)
	{
		auto index = _nodeId.namespaceIndex;
		this->clear();
		_nodeId.namespaceIndex = index;
		_nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_GUID;
	}
	QUaTypesConverter::uaVariantFromQVariantScalar<UA_Guid, QUuid>(uuId, &_nodeId.identifier.guid);
}

QByteArray QUaNodeId::byteArrayId() const
{
	return QUaTypesConverter::uaVariantToQVariantScalar<QByteArray, UA_ByteString>(&_nodeId.identifier.byteString);
}

void QUaNodeId::setByteArrayId(const QByteArray& byteArrayId)
{
	if (_nodeId.identifierType != UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING)
	{
		auto index = _nodeId.namespaceIndex;
		this->clear();
		_nodeId.namespaceIndex = index;
		_nodeId.identifierType = UA_NodeIdType::UA_NODEIDTYPE_BYTESTRING;
	}
	QUaTypesConverter::uaVariantFromQVariantScalar<UA_ByteString, QByteArray>(byteArrayId, &_nodeId.identifier.byteString);
}

QString QUaNodeId::toXmlString() const
{
	// use ::operator QString()
	return *this;
}

UA_NodeId QUaNodeId::toUaNodeId() const
{
	// use ::operator UA_NodeId()
	return *this;
}

bool QUaNodeId::isNull() const
{
	return UA_NodeId_isNull(&_nodeId);
}

void QUaNodeId::clear()
{
	if (UA_NodeId_isNull(&_nodeId))
	{
		return;
	}
	UA_NodeId_clear(&_nodeId);
	_nodeId = UA_NODEID_NULL;
}

quint32 QUaNodeId::internalHash() const
{
	return UA_NodeId_hash(&_nodeId);
}

QMetaEnum QUaExclusiveLimitState::_metaEnum = QMetaEnum::fromType<QUa::ExclusiveLimitState>();

QUaExclusiveLimitState::QUaExclusiveLimitState()
{
	_state = QUa::ExclusiveLimitState::None;
}

QUaExclusiveLimitState::QUaExclusiveLimitState(const QUa::ExclusiveLimitState& state)
{
	*this = state;
}

QUaExclusiveLimitState::QUaExclusiveLimitState(const QString& strState)
{
	*this = strState;
}

QUaExclusiveLimitState::QUaExclusiveLimitState(const char* strState)
{
	*this = strState;
}

void QUaExclusiveLimitState::operator=(const QUa::ExclusiveLimitState& state)
{
	_state = state;
}

void QUaExclusiveLimitState::operator=(const QString& strState)
{
	QByteArray ba = strState.toUtf8();
	*this = ba.constData();
}

void QUaExclusiveLimitState::operator=(const char* strState)
{
	bool ok = false;
	int val = _metaEnum.keyToValue(strState, &ok);
	_state = ok ? static_cast<QUa::ExclusiveLimitState>(val) : QUa::ExclusiveLimitState::None;
}

bool QUaExclusiveLimitState::operator==(const QUaExclusiveLimitState& other) const
{
	return _state == other._state;
}

bool QUaExclusiveLimitState::operator==(const QUa::ExclusiveLimitState& other) const
{
	return _state == other;
}

QUaExclusiveLimitState::operator QUa::ExclusiveLimitState() const
{
	return _state;
}

QUaExclusiveLimitState::operator QString() const
{
	const char* state = _metaEnum.valueToKey(static_cast<int>(_state));
	Q_ASSERT(state);
	return QString::fromUtf8(state);
}

QString QUaExclusiveLimitState::toString() const
{
	return *this;
}

QMetaEnum QUaExclusiveLimitTransition::_metaEnum = QMetaEnum::fromType<QUa::ExclusiveLimitTransition>();

QUaExclusiveLimitTransition::QUaExclusiveLimitTransition()
{
	_transition = QUa::ExclusiveLimitTransition::Null;
}

QUaExclusiveLimitTransition::QUaExclusiveLimitTransition(const QUa::ExclusiveLimitTransition& transition)
{
	*this = transition;
}

QUaExclusiveLimitTransition::QUaExclusiveLimitTransition(const QString& strTransition)
{
	*this = strTransition;
}

QUaExclusiveLimitTransition::QUaExclusiveLimitTransition(const char* strTransition)
{
	*this = strTransition;
}

void QUaExclusiveLimitTransition::operator=(const QUa::ExclusiveLimitTransition& transition)
{
	_transition = transition;
}

void QUaExclusiveLimitTransition::operator=(const QString& strTransition)
{
	QByteArray ba = strTransition.toUtf8();
	*this = ba.constData();
}

void QUaExclusiveLimitTransition::operator=(const char* strTransition)
{
	bool ok = false;
	int val = _metaEnum.keyToValue(strTransition, &ok);
	_transition = ok ? static_cast<QUa::ExclusiveLimitTransition>(val) : QUa::ExclusiveLimitTransition::Null;
}

bool QUaExclusiveLimitTransition::operator==(const QUaExclusiveLimitTransition& other) const
{
	return _transition == other._transition;
}

bool QUaExclusiveLimitTransition::operator==(const QUa::ExclusiveLimitTransition& other) const
{
	return _transition == other;
}

QUaExclusiveLimitTransition::operator QUa::ExclusiveLimitTransition() const
{
	return _transition;
}

QUaExclusiveLimitTransition::operator QString() const
{
	const char* transition = _metaEnum.valueToKey(static_cast<int>(_transition));
	Q_ASSERT(transition);
	return QString::fromUtf8(transition);
}

QString QUaExclusiveLimitTransition::toString() const
{
	return *this;
}


bool QUaEventHistoryQueryData::operator==(const QUaEventHistoryQueryData& other) const
{
	return 
		_timeStartExisting    == other._timeStartExisting &&
		_numEventsToRead      == other._numEventsToRead   &&
		_numEventsAlreadyRead == other._numEventsAlreadyRead;
}

bool QUaEventHistoryQueryData::isValid() const
{
	return _timeStartExisting.isValid();
}

QByteArray QUaEventHistoryQueryData::toByteArray(const QUaEventHistoryQueryData& inQueryData)
{
	QByteArray byteArray;
	QDataStream outStream(&byteArray, QIODeviceBase::OpenMode( QIODeviceBase::WriteOnly | QIODeviceBase::Truncate) );
	outStream.setVersion(QDataStream::Qt_6_3);
	outStream.setByteOrder(QDataStream::BigEndian);
	outStream << inQueryData;
	return byteArray;
};

QUaEventHistoryQueryData QUaEventHistoryQueryData::fromByteArray(const QByteArray& byteArray)
{
	QUaEventHistoryQueryData outQueryData;
	if (byteArray.isEmpty())
	{
		return outQueryData;
	}
	QDataStream inStream(byteArray);
	inStream.setVersion(QDataStream::Qt_6_3);
	inStream.setByteOrder(QDataStream::BigEndian);
	inStream >> outQueryData;
	return outQueryData;
}

QByteArray QUaEventHistoryQueryData::ContinuationToByteArray(const QUaEventHistoryContinuationPoint& inContinuation)
{
	QByteArray byteArray;
	QDataStream outStream(&byteArray, QIODeviceBase::OpenMode( QIODeviceBase::WriteOnly | QIODeviceBase::Truncate) );
	outStream.setVersion(QDataStream::Qt_6_3);
	outStream.setByteOrder(QDataStream::BigEndian);
	outStream << inContinuation;
	return byteArray;
}

QUaEventHistoryContinuationPoint QUaEventHistoryQueryData::ContinuationFromByteArray(const QByteArray& byteArray)
{
	QUaEventHistoryContinuationPoint outContinuation;
	if (byteArray.isEmpty())
	{
		return outContinuation;
	}
	QDataStream inStream(byteArray);
	inStream.setVersion(QDataStream::Qt_6_3);
	inStream.setByteOrder(QDataStream::BigEndian);
	inStream >> outContinuation;
	return outContinuation;
}

UA_ByteString QUaEventHistoryQueryData::ContinuationToUaByteString(const QUaEventHistoryContinuationPoint& inContinuation)
{
	UA_ByteString byteString;
	QUaTypesConverter::uaVariantFromQVariantScalar<UA_ByteString, QByteArray>(
		QUaEventHistoryQueryData::ContinuationToByteArray(inContinuation),
		&byteString
		);
	return byteString;
}

QUaEventHistoryContinuationPoint QUaEventHistoryQueryData::ContinuationFromUaByteString(const UA_ByteString& uaByteArray)
{
	return QUaEventHistoryQueryData::ContinuationFromByteArray(
		QUaTypesConverter::uaVariantToQVariantScalar<QByteArray, UA_ByteString>(&uaByteArray)
	);
}

QMetaEnum QUaLog::_metaEnumCategory = QMetaEnum::fromType<QUa::LogCategory>();
QMetaEnum QUaLog::_metaEnumLevel = QMetaEnum::fromType<QUa::LogLevel>();

QUaLog::QUaLog() :
	level(QUaLogLevel::Info),
	category(QUaLogCategory::Application),
	timestamp(QDateTime::currentDateTimeUtc())
{
}

QUaLog::QUaLog(const QString& strMessage,
	const QUaLogLevel& logLevel,
	const QUaLogCategory& logCategory)
	: QUaLog(strMessage.toUtf8(), logLevel, logCategory)
{
}

QUaLog::QUaLog(const QByteArray& baMessage,
	const QUaLogLevel& logLevel,
	const QUaLogCategory& logCategory) :
	message(baMessage),
	level(logLevel),
	category(logCategory),
	timestamp(QDateTime::currentDateTimeUtc())
{
}

size_t QUa::qHash(const Status &key, size_t seed)
{
    Q_UNUSED(seed);
    return static_cast<size_t>(key);
}

size_t QUa::qHash(const LogLevel &key, size_t seed)
{
    Q_UNUSED(seed);
    return static_cast<size_t>(key);
}

size_t QUa::qHash(const LogCategory &key, size_t seed)
{
    Q_UNUSED(seed);
    return static_cast<size_t>(key);
}

size_t QUa::qHash(const ExclusiveLimitState &key, size_t seed)
{
    Q_UNUSED(seed);
    return static_cast<size_t>(key);
}

size_t QUa::qHash(const ExclusiveLimitTransition &key, size_t seed)
{
    Q_UNUSED(seed);
    return static_cast<size_t>(key);
}

size_t QUa::qHash(const ChangeVerb &key, size_t seed)
{
    Q_UNUSED(seed);
    return static_cast<size_t>(key);
}

#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
QUaOptionSet::QUaOptionSet() :
	QUaOptionSet(0, 0)
{
	Q_ASSERT(_value.size() == 8);
	Q_ASSERT(_validBits.size() == 8);
}

QUaOptionSet::QUaOptionSet(const QUaOptionSet& other)
{
	Q_ASSERT(other._value.size() == 8);
	Q_ASSERT(other._validBits.size() == 8);
	// use overloaded equality operator
	*this = other;
	Q_ASSERT(_value.size() == 8);
	Q_ASSERT(_validBits.size() == 8);
}

QUaOptionSet::QUaOptionSet(const quint64& values, const quint64& validBits)
{
	this->setValues(values);
	this->setValidBits(validBits);
}

QUaOptionSet::QUaOptionSet(const UA_OptionSet& uaOptionSet)
{
	// use overloaded equality operator
	*this = uaOptionSet;
	Q_ASSERT(_value.size() == 8);
	Q_ASSERT(_validBits.size() == 8);
}

QUaOptionSet::QUaOptionSet(const QString& strXmlOptionSet)
{
	// use overloaded equality operator
	*this = strXmlOptionSet;
	Q_ASSERT(_value.size() == 8);
	Q_ASSERT(_validBits.size() == 8);
}

QUaOptionSet::QUaOptionSet(const char* strXmlOptionSet)
{
	// use overloaded equality operator
	*this = QString(strXmlOptionSet);
	Q_ASSERT(_value.size() == 8);
	Q_ASSERT(_validBits.size() == 8);
}

QUaOptionSet::operator UA_OptionSet() const
{
	UA_OptionSet uaOptionSet;
	QUaTypesConverter::uaVariantFromQVariantScalar<UA_ByteString, QByteArray>(_value    , &uaOptionSet.value);
	QUaTypesConverter::uaVariantFromQVariantScalar<UA_ByteString, QByteArray>(_validBits, &uaOptionSet.validBits);
	return uaOptionSet;
}

QUaOptionSet::operator QString() const
{
	return QStringLiteral("bits=%1;valid=%2").arg(this->values()).arg(this->validBits());
}

void QUaOptionSet::operator=(const UA_OptionSet& uaOptionSet)
{
	_value     = QUaTypesConverter::uaVariantToQVariantScalar<QByteArray, UA_ByteString>(&uaOptionSet.value);
	_validBits = QUaTypesConverter::uaVariantToQVariantScalar<QByteArray, UA_ByteString>(&uaOptionSet.validBits);
	Q_ASSERT(_value.size() == 8);
	Q_ASSERT(_validBits.size() == 8);
}

void QUaOptionSet::operator=(const QString& strXmlOptionSet)
{
	quint64 values;
	quint64 validBits;
	auto components = QStringView(strXmlOptionSet).split(QLatin1Char(';'));
	// check if valid xml format
	if (components.size() != 2)
	{
		// if no valid xml format, assume is a number and the number is the values
		this->setValues   (strXmlOptionSet.toULongLong());
		this->setValidBits(0xFFFFFFFFFFFFFFFF);
		return;
	}
	auto firstComp   = components.at(0);
	auto firstParts  = firstComp.split(QLatin1Char('='));

	auto firstPart   = firstParts.count() > 1 ? firstParts.at(1) : firstParts.at(0);
	auto secondComp  = components.at(1);
	auto secondParts = secondComp.split(QLatin1Char('='));
	auto secondPart  = secondParts.count() > 1 ? secondParts.at(1) : secondParts.at(0);
	if (firstComp.contains(QLatin1String("valid")) && firstComp.contains(QLatin1String("bits")))
	{
		validBits = firstPart.toULongLong();
		values    = secondPart.toULongLong();
	}
	else
	{
		values    = firstPart.toULongLong();
		validBits = secondPart.toULongLong();
	}
	this->setValues(values);
	this->setValidBits(validBits);
}

void QUaOptionSet::operator=(const char* strXmlOptionSet)
{
	// use overloaded equality operator
	*this = QString(strXmlOptionSet);
}

void QUaOptionSet::operator=(const QUaOptionSet& other)
{
	_value     = other._value;
	_validBits = other._validBits;
}

bool QUaOptionSet::operator==(const QUaOptionSet& other) const
{
	return _value == other._value && _validBits == other._validBits;
}

bool QUaOptionSet::operator!=(const QUaOptionSet& other) const
{
	return _value != other._value || _validBits != other._validBits;
}

bool QUaOptionSet::operator<(const QUaOptionSet& other) const
{
	return this->values() < other.values();
}

quint64 QUaOptionSet::values() const
{
	quint64 values;
	Q_ASSERT(_value.size() == 8);
	QDataStream inStream(_value);
	inStream.setVersion(QDataStream::Qt_6_3);
	inStream.setByteOrder(QDataStream::LittleEndian);
	inStream >> values;
	return values;
}

void QUaOptionSet::setValues(const quint64& values)
{
	QDataStream valueStream(&_value, QIODeviceBase::OpenMode( QIODeviceBase::WriteOnly | QIODeviceBase::Truncate) );
	valueStream.setVersion(QDataStream::Qt_6_3);
	valueStream.setByteOrder(QDataStream::LittleEndian);
	valueStream << static_cast<quint64>(values);
	Q_ASSERT(_value.size() == 8);
}

quint64 QUaOptionSet::validBits() const
{
	quint64 validBits;
	Q_ASSERT(_validBits.size() == 8);
	QDataStream inStream(_validBits);
	inStream.setVersion(QDataStream::Qt_6_3);
	inStream.setByteOrder(QDataStream::LittleEndian);
	inStream >> validBits;
	return validBits;
}

void QUaOptionSet::setValidBits(const quint64& validBits)
{
	QDataStream validBitsStream(&_validBits, QIODeviceBase::OpenMode( QIODeviceBase::WriteOnly | QIODeviceBase::Truncate) );
	validBitsStream.setVersion(QDataStream::Qt_6_3);
	validBitsStream.setByteOrder(QDataStream::LittleEndian);
	validBitsStream << static_cast<quint64>(validBits);
	Q_ASSERT(_validBits.size() == 8);
}

// https://stackoverflow.com/questions/47981/how-do-you-set-clear-and-toggle-a-single-bit
bool QUaOptionSet::bitValue(const quint8& bit)
{
	return (this->values() >> bit) & 1ULL;
}

void QUaOptionSet::setBitValue(const quint8& bit, const bool& value)
{
	Q_ASSERT_X(bit < 64, "QUaOptionSet::setBitValue", "Only 64bit optionsets are supported");
	auto values = this->values();
	value ?
		values |=   1ULL << bit :
		values &= ~(1ULL << bit);
	this->setValues(values);
}

bool QUaOptionSet::bitValidity(const quint8& bit)
{
	return (this->validBits() >> bit) & 1ULL;
}

void QUaOptionSet::setBitValidity(const quint8& bit, const bool& validity)
{
	Q_ASSERT_X(bit < 64, "QUaOptionSet::setBitValidity", "Only 64bit optionsets are supported");
	auto validBits = this->validBits();
	validity ?
		validBits |=   1ULL << bit :
		validBits &= ~(1ULL << bit);
	this->setValidBits(validBits);
}

#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
