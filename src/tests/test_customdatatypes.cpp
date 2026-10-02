#include <QDataStream>
#include <QTest>
#include <QUuid>

#include <QUaServer>

class TestCustomDataTypes : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void nodeIdNumericFormatsAsXml();
    void nodeIdParsesEveryIdentifierType_data();
    void nodeIdParsesEveryIdentifierType();
    void nodeIdRejectsMalformedString();
    void nodeIdStringOutlivesSource();
    void nodeIdCopiesAreIndependent();
    void nodeIdStreamsRoundTrip();
    void nodeIdOrdersByNamespaceFirst();
    void qualifiedNameParsesXml();
    void qualifiedNameFallsBackToPlainName();
    void browsePathReducesAndExpands();
    void localizedTextParsesXml();
    void localizedTextWithoutLocaleIsPlainText();
    void statusCodeConvertsToAndFromName();
    void statusCodeOutsideEnumIsNamed();
    void dataTypeMapsQtTypesToUaTypes();
};

///
/// \brief Registers the converters and container metatypes that QUaServer would otherwise register.
///
void TestCustomDataTypes::initTestCase()
{
    QUaTypesConverter::registerCustomTypes();
}

///
/// \brief A numeric NodeId always spells out its namespace.
///
void TestCustomDataTypes::nodeIdNumericFormatsAsXml()
{
    const QUaNodeId nodeId(1, 42u);

    QCOMPARE(nodeId.toXmlString(), QStringLiteral("ns=1;i=42"));
    QCOMPARE(nodeId.type(), QUaNodeIdType::Numeric);
    QCOMPARE(nodeId.namespaceIndex(), quint16(1));
    QCOMPARE(nodeId.numericId(), quint32(42));
    QVERIFY(!nodeId.isNull());
}

void TestCustomDataTypes::nodeIdParsesEveryIdentifierType_data()
{
    QTest::addColumn<QUaNodeId>("nodeId");
    QTest::addColumn<QUaNodeIdType>("type");

    QTest::newRow("numeric") << QUaNodeId(0, 85u) << QUaNodeIdType::Numeric;
    QTest::newRow("string") << QUaNodeId(2, QStringLiteral("Plant.Line1")) << QUaNodeIdType::String;
    QTest::newRow("guid")
        << QUaNodeId(3, QUuid(QStringLiteral("{72962b91-fa75-4ae6-8d28-b404dc7daf63}")))
        << QUaNodeIdType::Guid;
    QTest::newRow("bytestring") << QUaNodeId(4, QByteArray("\x01\x02\xff", 3)) << QUaNodeIdType::ByteString;
}

///
/// \brief Every identifier type survives a trip through its XML string.
///
void TestCustomDataTypes::nodeIdParsesEveryIdentifierType()
{
    QFETCH(QUaNodeId, nodeId);
    QFETCH(QUaNodeIdType, type);

    const QUaNodeId parsed(nodeId.toXmlString());

    QCOMPARE(parsed.type(), type);
    QCOMPARE(parsed, nodeId);
}

///
/// \brief A string that is not a NodeId yields the null NodeId instead of a partial one.
///
void TestCustomDataTypes::nodeIdRejectsMalformedString()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Failed to split node id string")));

    const QUaNodeId nodeId(QStringLiteral("ns=1;s=a;i=2"));

    QVERIFY(nodeId.isNull());
    QVERIFY(QUaNodeId(QString()).isNull());
}

///
/// \brief Regression: a string NodeId used to keep pointing into the QString it was built from.
///
void TestCustomDataTypes::nodeIdStringOutlivesSource()
{
    QUaNodeId nodeId;
    {
        QString source = QStringLiteral("ns=1;s=Temporary.Variable");
        nodeId = source;
        source.fill(QLatin1Char('x'));
    }

    QCOMPARE(nodeId.stringId(), QStringLiteral("Temporary.Variable"));
}

///
/// \brief Changing a copy does not leak into the original.
///
void TestCustomDataTypes::nodeIdCopiesAreIndependent()
{
    const QUaNodeId original(1, QStringLiteral("original"));
    QUaNodeId copy(original);

    copy.setStringId(QStringLiteral("changed"));

    QCOMPARE(original.stringId(), QStringLiteral("original"));
    QCOMPARE(copy.stringId(), QStringLiteral("changed"));
    QVERIFY(copy != original);
}

///
/// \brief NodeIds serialize through QDataStream without losing type or identifier.
///
void TestCustomDataTypes::nodeIdStreamsRoundTrip()
{
    const QList<QUaNodeId> nodeIds = {
        QUaNodeId(1, 7u),
        QUaNodeId(2, QStringLiteral("text")),
        QUaNodeId(3, QUuid::createUuid()),
        QUaNodeId(4, QByteArray("bytes")),
    };
    QByteArray buffer;
    {
        QDataStream out(&buffer, QIODevice::WriteOnly);
        for (const QUaNodeId &nodeId : nodeIds)
        {
            out << nodeId;
        }
    }

    QDataStream in(buffer);
    for (const QUaNodeId &expected : nodeIds)
    {
        QUaNodeId actual;
        in >> actual;
        QCOMPARE(actual, expected);
    }
}

///
/// \brief Ordering compares the namespace before the identifier, so sorted containers group namespaces.
///
void TestCustomDataTypes::nodeIdOrdersByNamespaceFirst()
{
    QVERIFY(QUaNodeId(0, 1000u) < QUaNodeId(1, 1u));
    QVERIFY(!(QUaNodeId(1, 1u) < QUaNodeId(0, 1000u)));
    QVERIFY(QUaNodeId(1, 1u) < QUaNodeId(1, 2u));
}

///
/// \brief A qualified name round-trips through its XML string.
///
void TestCustomDataTypes::qualifiedNameParsesXml()
{
    const QUaQualifiedName name(QStringLiteral("ns=3;s=Temperature"));

    QCOMPARE(name.namespaceIndex(), quint16(3));
    QCOMPARE(name.name(), QStringLiteral("Temperature"));
    QCOMPARE(name.toXmlString(), QStringLiteral("ns=3;s=Temperature"));
    QCOMPARE(QUaQualifiedName(name.toXmlString()), name);
}

///
/// \brief Strings that are not XML qualified names become a namespace 0 name.
///
void TestCustomDataTypes::qualifiedNameFallsBackToPlainName()
{
    const QUaQualifiedName plain(QStringLiteral("Pressure"));
    const QUaQualifiedName badNamespace(QStringLiteral("ns=x;s=Pressure"));

    QCOMPARE(plain.namespaceIndex(), quint16(0));
    QCOMPARE(plain.name(), QStringLiteral("Pressure"));
    QCOMPARE(badNamespace.namespaceIndex(), quint16(0));
    QCOMPARE(badNamespace.name(), QStringLiteral("ns=x;s=Pressure"));
    QVERIFY(QUaQualifiedName().isEmpty());
}

///
/// \brief A browse path joins into a single name and splits back into namespace 0 names.
///
void TestCustomDataTypes::browsePathReducesAndExpands()
{
    const QUaBrowsePath path = { QUaQualifiedName(0, QStringLiteral("EnabledState")),
                                 QUaQualifiedName(0, QStringLiteral("Id")) };

    QCOMPARE(QUaQualifiedName::reduceName(path), QStringLiteral("EnabledState/Id"));
    QCOMPARE(QUaQualifiedName::reduceName(path, QStringLiteral(".")), QStringLiteral("EnabledState.Id"));
    QCOMPARE(QUaQualifiedName::expandName(QStringLiteral("EnabledState/Id")), path);
    QCOMPARE(QUaQualifiedName::reduceName({ path.first() }), QStringLiteral("EnabledState"));
}

///
/// \brief A localized text keeps locale and text through its XML string.
///
void TestCustomDataTypes::localizedTextParsesXml()
{
    const QUaLocalizedText text(QStringLiteral("l=en-US;t=Hello"));

    QCOMPARE(text.locale(), QStringLiteral("en-US"));
    QCOMPARE(text.text(), QStringLiteral("Hello"));
    QCOMPARE(text.toXmlString(), QStringLiteral("l=en-US;t=Hello"));
    QCOMPARE(QUaLocalizedText(text.toXmlString()), text);
}

///
/// \brief Without a locale the text is used verbatim, in both directions.
///
void TestCustomDataTypes::localizedTextWithoutLocaleIsPlainText()
{
    const QUaLocalizedText text(QStringLiteral("Just a text"));

    QVERIFY(text.locale().isEmpty());
    QCOMPARE(text.text(), QStringLiteral("Just a text"));
    QCOMPARE(text.toXmlString(), QStringLiteral("Just a text"));
}

///
/// \brief Status codes convert between their numeric value and their symbolic name.
///
void TestCustomDataTypes::statusCodeConvertsToAndFromName()
{
    const QUaStatusCode fromCode(UA_STATUSCODE_BADOUTOFSERVICE);
    const QUaStatusCode fromName(QStringLiteral("BadOutOfService"));

    QCOMPARE(static_cast<QString>(fromCode), QStringLiteral("BadOutOfService"));
    QCOMPARE(static_cast<UA_StatusCode>(fromName), UA_STATUSCODE_BADOUTOFSERVICE);
    QVERIFY(fromName == QUaStatus::BadOutOfService);
    QVERIFY(!QUaStatusCode::longDescription(fromCode).isEmpty());
}

///
/// \brief Codes missing from QUa::Status are still named, using the open62541 name table.
///
void TestCustomDataTypes::statusCodeOutsideEnumIsNamed()
{
    const QUaStatusCode status(UA_STATUSCODE_BADTYPEMISMATCH);

    QCOMPARE(static_cast<QString>(status), QStringLiteral("BadTypeMismatch"));
    QCOMPARE(static_cast<UA_StatusCode>(status), UA_STATUSCODE_BADTYPEMISMATCH);
}

///
/// \brief Supported Qt types map to the matching namespace 0 data types and back.
///
void TestCustomDataTypes::dataTypeMapsQtTypesToUaTypes()
{
    QVERIFY(QUaDataType::isSupportedQType(QMetaType::Int));
    QVERIFY(QUaDataType::isSupportedQType(QMetaType::QString));
    QVERIFY(!QUaDataType::isSupportedQType(QMetaType::QRect));

    QCOMPARE(QUaNodeId(QUaDataType::nodeIdByQType(QMetaType::Int)), QUaNodeId(0, quint32(UA_NS0ID_INT32)));
    QCOMPARE(QUaDataType::qTypeByNodeId(UA_NODEID_NUMERIC(0, UA_NS0ID_DOUBLE)), QMetaType::Double);
    QCOMPARE(QUaDataType::dataTypeByQType(QMetaType::QString), &UA_TYPES[UA_TYPES_STRING]);
    QCOMPARE(QUaDataType::qTypeByTypeIndex(UA_TYPES_BOOLEAN), QMetaType::Bool);
}

QTEST_GUILESS_MAIN(TestCustomDataTypes)

#include "test_customdatatypes.moc"
