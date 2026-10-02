#ifndef TESTCLIENT_H
#define TESTCLIENT_H

#include <functional>

#include <QList>
#include <QVariant>

#include <QUaServer>

#include <open62541/client.h>

///
/// \brief Blocking open62541 client for talking to an in-process QUaServer.
///
/// QUaServer iterates on the Qt event loop of the test thread, so every client
/// request runs on a worker thread while the test thread keeps processing events.
///
class TestClient
{
public:
    ///
    /// \brief Creates a client that logs only warnings and errors.
    ///
    TestClient();
    ~TestClient();

    TestClient(const TestClient &) = delete;
    TestClient &operator=(const TestClient &) = delete;

    ///
    /// \brief Opens an anonymous session on \a url.
    ///
    UA_StatusCode connect(const QString &url);

    ///
    /// \brief Opens a session on \a url authenticated with a user name and password.
    ///
    UA_StatusCode connectUsername(const QString &url, const QString &userName, const QString &password);

    ///
    /// \brief Closes the session and the secure channel, if any.
    ///
    UA_StatusCode disconnect();

    ///
    /// \brief Returns the user token types offered by the endpoints of \a url.
    ///
    QList<UA_UserTokenType> endpointUserTokenTypes(const QString &url);

    ///
    /// \brief Reads the Value attribute of \a nodeId into \a value.
    ///
    UA_StatusCode readValue(const QUaNodeId &nodeId, QVariant &value);

    ///
    /// \brief Reads the DataType attribute of the variable \a nodeId into \a dataTypeId.
    ///
    UA_StatusCode readValueDataType(const QUaNodeId &nodeId, QUaNodeId &dataTypeId);

    ///
    /// \brief Writes \a value to the Value attribute of \a nodeId.
    ///
    UA_StatusCode writeValue(const QUaNodeId &nodeId, const QVariant &value);

    ///
    /// \brief Calls the method \a methodId of \a objectId with \a inputs and stores its results in \a outputs.
    ///
    UA_StatusCode call(const QUaNodeId &objectId,
                       const QUaNodeId &methodId,
                       const QVariantList &inputs,
                       QVariantList &outputs);

    ///
    /// \brief Creates a subscription with a data change monitored item on the Value of \a nodeId.
    ///
    UA_StatusCode monitorValue(const QUaNodeId &nodeId);

    ///
    /// \brief Creates a subscription with an event monitored item on \a emitterId.
    /// \param selectClauses Browse paths, relative to BaseEventType, of the event fields to receive.
    ///
    UA_StatusCode subscribeEvents(const QUaNodeId &emitterId, const QList<QUaBrowsePath> &selectClauses);

    ///
    /// \brief Returns the id of the subscription holding the monitored items, 0 when there is none.
    ///
    UA_UInt32 subscriptionId() const;

    ///
    /// \brief Processes publish responses until \a count events arrived or \a timeoutMs elapsed.
    /// \return The received events, oldest first, each as the list of its selected fields.
    ///
    QList<QVariantList> waitForEvents(int count, int timeoutMs = 5000);

#ifdef UA_ENABLE_HISTORIZING
    ///
    /// \brief Reads the first response page of raw history of \a nodeId.
    /// \param numValuesPerNode Requested page size; 0 lets the server decide.
    ///
    UA_StatusCode readHistoryRaw(const QUaNodeId &nodeId, quint32 numValuesPerNode, QVariantList &values);

    ///
    /// \brief Reads the first response page of event history of \a nodeId.
    /// \param numValuesPerNode Requested page size; 0 lets the server decide.
    ///
    UA_StatusCode readHistoryEvents(const QUaNodeId &nodeId,
                                    const QList<QUaBrowsePath> &selectClauses,
                                    quint32 numValuesPerNode,
                                    QList<QVariantList> &events);
#endif // UA_ENABLE_HISTORIZING

private:
    UA_Client *m_client;
    UA_UInt32 m_subscriptionId;
    QList<QVariantList> m_events;

    UA_StatusCode ensureSubscription();
    void runInWorker(const std::function<void()> &job);
    static UA_EventFilter eventFilter(const QList<QUaBrowsePath> &selectClauses);
    static QVariantList eventFieldsToList(size_t nEventFields, UA_Variant *eventFields);
    static void eventNotification(UA_Client *client,
                                  UA_UInt32 subId,
                                  void *subContext,
                                  UA_UInt32 monId,
                                  void *monContext,
                                  const UA_KeyValueMap eventFields);
#ifdef UA_ENABLE_HISTORIZING
    static UA_Boolean historyRawPage(UA_Client *client,
                                     const UA_NodeId *nodeId,
                                     UA_Boolean moreDataAvailable,
                                     const UA_ExtensionObject *data,
                                     void *callbackContext);
    static UA_Boolean historyEventsPage(UA_Client *client,
                                        const UA_NodeId *nodeId,
                                        UA_Boolean moreDataAvailable,
                                        const UA_ExtensionObject *data,
                                        void *callbackContext);
#endif // UA_ENABLE_HISTORIZING
};

#endif // TESTCLIENT_H
