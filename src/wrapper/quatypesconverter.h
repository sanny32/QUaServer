#ifndef QUATYPESCONVERTER_H
#define QUATYPESCONVERTER_H

#include <QUaCustomDataTypes>

QT_BEGIN_NAMESPACE

namespace QUaTypesConverter {

	// common convertions
	UA_NodeId nodeIdFromQString  (const QString &name);
	QString   nodeIdToQString    (const UA_NodeId &id);
	
	bool      nodeIdStringSplit  (const QString &nodeIdString, quint16 *nsIndex, QString *identifier, char *identifierType);
	QString   nodeClassToQString (const UA_NodeClass &nclass);
	
	QString   uaStringToQString  (const UA_String &string);
	UA_String uaStringFromQString(const QString &uaString);

	// qt supported
	bool            isQTypeArray (const QMetaType::Type &type);
	bool            isQTypeArray (const QMetaType &metaType);
	bool            isQTypeArray (const QByteArray &typeName);
	QMetaType::Type getQArrayType(const QMetaType::Type &type);
	QMetaType::Type getQArrayType(const QMetaType &metaType);
	QMetaType::Type getQArrayType(const QByteArray &typeName);
	bool            isSupportedQType(const QMetaType::Type &type);
	bool            canConvertQVariantList(const QVariant &value);

	// ua from c++
	template<typename T>
	UA_NodeId uaTypeNodeIdFromCpp();
	// qt from c++
	template<typename T>
	QMetaType::Type qtTypeFromCpp();
	// ua from qt
	UA_NodeId          uaTypeNodeIdFromQType(const QMetaType::Type &type);
	const UA_DataType *uaTypeFromQType      (const QMetaType::Type &type);
	UA_Variant         uaVariantFromQVariant(
		const QVariant &var
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
#ifndef OPEN62541_ISSUE3934_RESOLVED
		, const UA_DataType * optDataType = nullptr
#endif // !OPEN62541_ISSUE3934_RESOLVED
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
	);
	// ua from qt : scalar
	template<typename TARGETTYPE, typename QTTYPE> // has specializations
	UA_Variant uaVariantFromQVariantScalar(const QVariant &var, const UA_DataType *type);
	template<> // TODO : implement better
	UA_Variant uaVariantFromQVariantScalar<UA_Variant, QVariant>(const QVariant & var, const UA_DataType * type);
	template<typename TARGETTYPE, typename QTTYPE> // has specializations
	void       uaVariantFromQVariantScalar(const QTTYPE &var, TARGETTYPE *ptr);
	// ua from qt : array
	UA_Variant uaVariantFromQVariantArray(const QVariant & var);
	template<typename TARGETTYPE, typename QTTYPE>
	UA_Variant uaVariantFromQVariantArray(const QVariant &var, const UA_DataType *type);
	template<> // TODO : implement better
	UA_Variant uaVariantFromQVariantArray<UA_Variant, QVariant>(const QVariant & var, const UA_DataType * type);

	// ua to qt : array
	enum class ArrayType
	{
		QList   = 0,
		QVector = 1,
		Invalid = 2
	};
	// ua to qt
	QMetaType::Type uaTypeNodeIdToQType(const UA_NodeId   *nodeId   );
	QMetaType::Type uaTypeToQType      (const UA_DataType *uaType   );
	// index of the type in UA_TYPES (UA_TYPES_COUNT if not a builtin type)
	UA_UInt32       uaTypeIndex        (const UA_DataType *uaType   );
	QVariant        uaVariantToQVariant(const UA_Variant  &uaVariant, const ArrayType& arrType = ArrayType::QList);
	// ua to qt : scalar
	template<typename TARGETTYPE, typename UATYPE> // has specializations
	QVariant   uaVariantToQVariantScalar(const UA_Variant &uaVariant, QMetaType::Type type);
	// TODO
	template<>
	QVariant uaVariantToQVariantScalar<QVariant, UA_Variant>(const UA_Variant & uaVariant, QMetaType::Type type);
	template<typename TARGETTYPE, typename UATYPE> // has specializations
	TARGETTYPE uaVariantToQVariantScalar(const UATYPE *data);
	QVariant uaVariantToQVariantArray (const UA_Variant &uaVariant, 
		                               const ArrayType  &arrType = ArrayType::QList);
	QVariant uaVariantToQVariantList  (const UA_Variant &uaVariant);
	QVariant uaVariantToQVariantVector(const UA_Variant &uaVariant);
    template <typename ARRAYTYPE, typename UATYPE>
	QVariant uaVariantToQVariantArray (const UA_Variant &var, QMetaType::Type type);

	template<typename T>
	UA_NodeId uaTypeNodeIdFromCpp()
	{
		if constexpr (std::is_same_v<T, QVariant>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE);
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_BOOLEAN);
		}
		else if constexpr (std::is_same_v<T, char>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_SBYTE);
		}
		else if constexpr (std::is_same_v<T, uchar>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_BYTE);
		}
		else if constexpr (std::is_same_v<T, qint16>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_INT16);
		}
		else if constexpr (std::is_same_v<T, quint16>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_UINT16);
		}
		else if constexpr (std::is_same_v<T, int>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_INT32);
		}
		else if constexpr (std::is_same_v<T, qint32>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_INT32);
		}
		else if constexpr (std::is_same_v<T, quint32>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_UINT32);
		}
		else if constexpr (std::is_same_v<T, int64_t>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_INT64);
		}
		else if constexpr (std::is_same_v<T, uint64_t>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_UINT64);
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_FLOAT);
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_DOUBLE);
		}
		else if constexpr (std::is_same_v<T, QString> || std::is_same_v<T, const char *>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_STRING);
		}
		else if constexpr (std::is_same_v<T, QDateTime>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_DATETIME);
		}
		else if constexpr (std::is_same_v<T, QUuid>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_GUID);
		}
		else if constexpr (std::is_same_v<T, QByteArray>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_BYTESTRING);
		}
		else if constexpr (std::is_same_v<T, QUaNodeId>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_NODEID);
		}
		else if constexpr (std::is_same_v<T, QUaStatusCode>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_STATUSCODE);
		}
		else if constexpr (std::is_same_v<T, QUaQualifiedName>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_QUALIFIEDNAME);
		}
		else if constexpr (std::is_same_v<T, QUaLocalizedText>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_LOCALIZEDTEXT);
		}
		// TODO : image
		//else if constexpr (std::is_same_v<T, QImage>)
		//{
		//	return UA_NODEID_NUMERIC(0, UA_NS0ID_IMAGE);
		//}
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
		else if constexpr (std::is_same_v<T, QUaOptionSet>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_OPTIONSET);
		}
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
		else if constexpr (std::is_same_v<T, QTimeZone>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_TIMEZONEDATATYPE);
		}
		else if constexpr (std::is_same_v<T, QUaChangeStructureDataType>)
		{
			return UA_NODEID_NUMERIC(0, UA_NS0ID_MODELCHANGESTRUCTUREDATATYPE);
		}
#endif
		else
		{
			Q_ASSERT_X(false, "uaTypeNodeIdFromCpp", "Unsupported type");
			return UA_NodeId();
		}
	}

	template<typename T>
	QMetaType::Type qtTypeFromCpp()
	{
		if constexpr (std::is_same_v<T, QVariant>)
		{
			return QMetaType::UnknownType;
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			return QMetaType::Bool;
		}
		else if constexpr (std::is_same_v<T, char>)
		{
			return QMetaType::Char;
		}
		else if constexpr (std::is_same_v<T, uchar>)
		{
			return QMetaType::UChar;
		}
		else if constexpr (std::is_same_v<T, qint16>)
		{
			return QMetaType::Short;
		}
		else if constexpr (std::is_same_v<T, quint16>)
		{
			return QMetaType::UShort;
		}
		else if constexpr (std::is_same_v<T, int>)
		{
			return QMetaType::Int;
		}
		else if constexpr (std::is_same_v<T, qint32>)
		{
			return QMetaType::Int;
		}
		else if constexpr (std::is_same_v<T, quint32>)
		{
			return QMetaType::UInt;
		}
		else if constexpr (std::is_same_v<T, int64_t>)
		{
			return QMetaType::LongLong;
		}
		else if constexpr (std::is_same_v<T, uint64_t>)
		{
			return QMetaType::ULongLong;
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			return QMetaType::Float;
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			return QMetaType::Double;
		}
		else if constexpr (std::is_same_v<T, QString>)
		{
			return QMetaType::QString;
		}
		else if constexpr (std::is_same_v<T, QDateTime>)
		{
			return QMetaType::QDateTime;
		}
		else if constexpr (std::is_same_v<T, QUuid>)
		{
			return QMetaType::QUuid;
		}
		else if constexpr (std::is_same_v<T, QByteArray>)
		{
			return QMetaType::QByteArray;
		}
		else if constexpr (std::is_same_v<T, QVariantList>)
		{
			return QMetaType::QVariantList;
		}
		else if constexpr (std::is_same_v<T, QUaNodeId>)
		{
			return QMetaType_NodeId;
		}
		else if constexpr (std::is_same_v<T, QUaStatusCode>)
		{
			return QMetaType_StatusCode;
		}
		else if constexpr (std::is_same_v<T, QUaQualifiedName>)
		{
			return QMetaType_QualifiedName;
		}
		else if constexpr (std::is_same_v<T, QUaLocalizedText>)
		{
			return QMetaType_LocalizedText;
		}
		// TODO : image
		//else if constexpr (std::is_same_v<T, QImage>)
		//{
		//	return QMetaType_Image;
		//}
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
		else if constexpr (std::is_same_v<T, QUaOptionSet>)
		{
			return QMetaType_OptionSet;
		}
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
		else if constexpr (std::is_same_v<T, QTimeZone>)
		{
			return QMetaType_TimeZone;
		}
		else if constexpr (std::is_same_v<T, QUaChangeStructureDataType>)
		{
			return QMetaType_ChangeStructureDataType;
		}
#endif
		else
		{
			Q_ASSERT_X(false, "qtTypeFromCpp", "Unsupported type");
			return QMetaType::UnknownType;
		}
	}

	void registerCustomTypes();
}

QT_END_NAMESPACE

#endif // QUATYPESCONVERTER_H
