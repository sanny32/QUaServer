#include <QTest>
#include <QUuid>

#include <QUaServer>

class TestTypesConverter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void scalarRoundTrip_data();
    void scalarRoundTrip();
    void statusCodeIsReadAsNumber();
    void byteArrayMapsToByteString();
    void listRoundTrip();
    void arrayCanBeReadAsVector();
    void emptyVariantIsEmpty();
    void nodeIdStringConversions();
    void typeIndexOfBuiltinAndUnknownTypes();
    void arrayTypeHelpers();
};

///
/// \brief Registers the converters and container metatypes that QUaServer would otherwise register.
///
void TestTypesConverter::initTestCase()
{
    QUaTypesConverter::registerCustomTypes();
}

void TestTypesConverter::scalarRoundTrip_data()
{
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<int>("uaTypeIndex");

    const QDateTime time(QDate(2026, 10, 2), QTime(12, 30, 15, 250), QTimeZone::UTC);

    QTest::newRow("bool") << QVariant(true) << int(UA_TYPES_BOOLEAN);
    QTest::newRow("sbyte") << QVariant::fromValue(qint8(-5)) << int(UA_TYPES_SBYTE);
    QTest::newRow("byte") << QVariant::fromValue(quint8(200)) << int(UA_TYPES_BYTE);
    QTest::newRow("int16") << QVariant::fromValue(qint16(-1234)) << int(UA_TYPES_INT16);
    QTest::newRow("uint16") << QVariant::fromValue(quint16(65000)) << int(UA_TYPES_UINT16);
    QTest::newRow("int32") << QVariant(qint32(-123456)) << int(UA_TYPES_INT32);
    QTest::newRow("uint32") << QVariant(quint32(4000000000u)) << int(UA_TYPES_UINT32);
    QTest::newRow("int64") << QVariant(qint64(-9000000000LL)) << int(UA_TYPES_INT64);
    QTest::newRow("uint64") << QVariant(quint64(18000000000ULL)) << int(UA_TYPES_UINT64);
    QTest::newRow("float") << QVariant(1.5f) << int(UA_TYPES_FLOAT);
    QTest::newRow("double") << QVariant(3.25) << int(UA_TYPES_DOUBLE);
    QTest::newRow("string") << QVariant(QStringLiteral("Grüße")) << int(UA_TYPES_STRING);
    QTest::newRow("datetime") << QVariant(time) << int(UA_TYPES_DATETIME);
    QTest::newRow("guid") << QVariant(QUuid(QStringLiteral("{72962b91-fa75-4ae6-8d28-b404dc7daf63}")))
                          << int(UA_TYPES_GUID);
    QTest::newRow("nodeid") << QVariant::fromValue(QUaNodeId(2, QStringLiteral("x"))) << int(UA_TYPES_NODEID);
    QTest::newRow("qualifiedname") << QVariant::fromValue(QUaQualifiedName(1, QStringLiteral("Name")))
                                   << int(UA_TYPES_QUALIFIEDNAME);
    QTest::newRow("localizedtext") << QVariant::fromValue(QUaLocalizedText(QStringLiteral("en"), QStringLiteral("Text")))
                                   << int(UA_TYPES_LOCALIZEDTEXT);
}

///
/// \brief Each supported scalar gets the matching UA builtin type and converts back unchanged.
///
void TestTypesConverter::scalarRoundTrip()
{
    QFETCH(QVariant, value);
    QFETCH(int, uaTypeIndex);

    UA_Variant uaValue = QUaTypesConverter::uaVariantFromQVariant(value);
    const QVariant back = QUaTypesConverter::uaVariantToQVariant(uaValue);
    const bool isScalar = UA_Variant_isScalar(&uaValue);
    const UA_DataType *type = uaValue.type;
    UA_Variant_clear(&uaValue);

    QVERIFY(isScalar);
    QCOMPARE(type, &UA_TYPES[uaTypeIndex]);
    QCOMPARE(back.metaType(), value.metaType());
    QCOMPARE(back, value);
}

///
/// \brief Regression: a QUaStatusCode lost its value (became Good) when converted to a UA variant.
///
void TestTypesConverter::statusCodeIsReadAsNumber()
{
    UA_Variant uaValue = QUaTypesConverter::uaVariantFromQVariant(
        QVariant::fromValue(QUaStatusCode(UA_STATUSCODE_BADNOTFOUND)));
    const UA_DataType *type = uaValue.type;
    const QVariant back = QUaTypesConverter::uaVariantToQVariant(uaValue);
    UA_Variant_clear(&uaValue);

    QCOMPARE(type, &UA_TYPES[UA_TYPES_STATUSCODE]);
    QCOMPARE(back.toUInt(), UA_STATUSCODE_BADNOTFOUND);
}

///
/// \brief Regression: with the full namespace zero QByteArray was mapped to ImagePNG instead of ByteString.
///
void TestTypesConverter::byteArrayMapsToByteString()
{
    const QByteArray bytes("\x00\x10\x20", 3);

    UA_Variant uaValue = QUaTypesConverter::uaVariantFromQVariant(bytes);
    const UA_DataType *type = uaValue.type;
    const QVariant back = QUaTypesConverter::uaVariantToQVariant(uaValue);
    UA_Variant_clear(&uaValue);

    QCOMPARE(type, &UA_TYPES[UA_TYPES_BYTESTRING]);
    QCOMPARE(QUaTypesConverter::uaTypeFromQType(QMetaType::QByteArray), &UA_TYPES[UA_TYPES_BYTESTRING]);
    QCOMPARE(back.toByteArray(), bytes);
}

///
/// \brief A list becomes a one-dimensional UA array of the element type.
///
void TestTypesConverter::listRoundTrip()
{
    const QList<int> list = { 1, -2, 3 };

    UA_Variant uaValue = QUaTypesConverter::uaVariantFromQVariant(QVariant::fromValue(list));
    const size_t length = uaValue.arrayLength;
    const UA_DataType *type = uaValue.type;
    const QVariant back = QUaTypesConverter::uaVariantToQVariant(uaValue);
    UA_Variant_clear(&uaValue);

    QCOMPARE(length, size_t(3));
    QCOMPARE(type, &UA_TYPES[UA_TYPES_INT32]);
    QCOMPARE(back.value<QList<int>>(), list);
}

///
/// \brief The array type of the result is chosen by the caller.
///
void TestTypesConverter::arrayCanBeReadAsVector()
{
    const QList<double> list = { 0.5, 1.5 };
    UA_Variant uaValue = QUaTypesConverter::uaVariantFromQVariant(QVariant::fromValue(list));

    const QVariant asVector = QUaTypesConverter::uaVariantToQVariant(uaValue, QUaTypesConverter::ArrayType::QVector);
    UA_Variant_clear(&uaValue);

    QVERIFY(QUaTypesConverter::isQTypeArray(asVector.metaType()));
    QCOMPARE(asVector.value<QVector<double>>(), QVector<double>({ 0.5, 1.5 }));
}

///
/// \brief A null QVariant is an empty UA variant and converts back to an invalid QVariant.
///
void TestTypesConverter::emptyVariantIsEmpty()
{
    UA_Variant uaValue = QUaTypesConverter::uaVariantFromQVariant(QVariant());
    const bool empty = UA_Variant_isEmpty(&uaValue);
    const QVariant back = QUaTypesConverter::uaVariantToQVariant(uaValue);
    UA_Variant_clear(&uaValue);

    QVERIFY(empty);
    QVERIFY(!back.isValid());
}

///
/// \brief NodeId strings parse into open62541 NodeIds and format back identically.
///
void TestTypesConverter::nodeIdStringConversions()
{
    UA_NodeId numeric = QUaTypesConverter::nodeIdFromQString(QStringLiteral("ns=1;i=1001"));
    UA_NodeId text = QUaTypesConverter::nodeIdFromQString(QStringLiteral("ns=2;s=Plant/Line"));
    const UA_NodeId empty = QUaTypesConverter::nodeIdFromQString(QString());

    QCOMPARE(numeric.namespaceIndex, UA_UInt16(1));
    QCOMPARE(numeric.identifier.numeric, UA_UInt32(1001));
    QCOMPARE(QUaTypesConverter::nodeIdToQString(text), QStringLiteral("ns=2;s=Plant/Line"));
    QVERIFY(UA_NodeId_isNull(&empty));

    UA_NodeId_clear(&numeric);
    UA_NodeId_clear(&text);
}

///
/// \brief Builtin types report their UA_TYPES index; unknown ones report UA_TYPES_COUNT.
///
void TestTypesConverter::typeIndexOfBuiltinAndUnknownTypes()
{
    QCOMPARE(QUaTypesConverter::uaTypeIndex(&UA_TYPES[UA_TYPES_DOUBLE]), UA_UInt32(UA_TYPES_DOUBLE));
    QCOMPARE(QUaTypesConverter::uaTypeIndex(nullptr), UA_UInt32(UA_TYPES_COUNT));

    UA_DataType customEnum = UA_TYPES[UA_TYPES_INT32];
    customEnum.typeKind = UA_DATATYPEKIND_ENUM;
    QCOMPARE(QUaTypesConverter::uaTypeIndex(&customEnum), UA_UInt32(UA_TYPES_INT32));
    QCOMPARE(QUaTypesConverter::uaTypeToQType(&customEnum), QMetaType::Int);
}

///
/// \brief List metatypes are recognised as arrays and resolve to their element type.
///
void TestTypesConverter::arrayTypeHelpers()
{
    QVERIFY(QUaTypesConverter::isQTypeArray(QMetaType::fromType<QList<quint16>>()));
    QVERIFY(!QUaTypesConverter::isQTypeArray(QMetaType::fromType<quint16>()));
    QCOMPARE(QUaTypesConverter::getQArrayType(QMetaType::fromType<QList<quint16>>()), QMetaType::UShort);
    QVERIFY(QUaTypesConverter::canConvertQVariantList(QVariantList({ 1, 2 })));
}

QTEST_GUILESS_MAIN(TestTypesConverter)

#include "test_typesconverter.moc"
