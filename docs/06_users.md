# Users

By default, `QUaServer` instances allow *anonymous* login. To disable it use the `setAnonymousLoginAllowed()` method as follows:

```c++
server.setAnonymousLoginAllowed(false);
```

But now it is necessary to create at least one user account to access the server. This can be done using the `addUser()` method:

```c++
server.addUser("juan", "pass123");
server.addUser("john", "qwerty");
```

The first argument is the **username** and the second is the **password**. Calling `addUser()` for an existing user updates the password. Users can be managed with the following methods:

```c++
void        removeUser(const QString &strUserName);
QString     userKey   (const QString &strUserName) const;
bool        userExists(const QString &strUserName) const;
QStringList userNames () const;
int         userCount ();
```

By default, the password sent by the client is compared with the stored one. To avoid storing passwords in plain text, store a hash instead and provide a validation callback that checks the password against it:

```c++
server.setUserValidationCallback(
[&server](const QString &strUserName, const QString &strPassword) {
	if (!server.userExists(strUserName))
	{
		return false;
	}
	// compare against a stored hash instead of a plain text password
	return server.userKey(strUserName) == hashPassword(strPassword);
});
```

Now when trying to connect to the server application without credentials, an error might appear in the client's log:

```
Error 'BadIdentityTokenInvalid' was returned during ActivateSession
```

To connect to the server it is necessary now to provide credentials. For example, with the *UA Expert* client right click the server and select `Properties ...`:

<p align="center">
  <img src="../res/img/06_users_01.jpg">
</p>

Then in *Authentication Settings* select *Username Password* and introduce the username, and click `OK`:

<p align="center">
  <img src="../res/img/06_users_02.jpg">
</p>

Now when connecting, the password will be requested. It is likely that the client will issue a warning:

<p align="center">
  <img src="../res/img/06_users_03.jpg">
</p>

The reason is that communications are **not yet encrypted**, therefore the username and password will be sent in **plain text**. So any application that monitors the network (such as [Wireshark](https://www.wireshark.org/)) can read such messages and read the login credentials.

For the moment ignore the warning by clicking `Ignore` to connect to the server. In the [Encryption](07_encryption.md) section it is detailed how to encrypt communications such that the login credentials are protected from [eavesdropping](https://en.wikipedia.org/wiki/Eavesdropping) applications.

Having a list of login credentials does not only limit access to the server, but it is also possible to limit access to individual resources on a **per-user** basis using the `setUserAccessLevelCallback()` available in the `QUaNode` API:

```c++
// Create some variables
QUaFolderObject * objsFolder = server.objectsFolder();
// NOTE : the variables need to be overall writable
//        user-level access is defined later
auto var1 = objsFolder->addProperty("var1");
var1->setWriteAccess(true);
var1->setValue(123);
auto var2 = objsFolder->addProperty("var2");
var2->setWriteAccess(true);
var2->setValue(1.23);

// Give access control to individual variables
var1->setUserAccessLevelCallback([](const QString &strUserName) {
	QUaAccessLevel access;
	// Read Access to all
	access.bits.bRead = true;
	// Write Access only to juan
	if (strUserName.compare("juan", Qt::CaseSensitive) == 0)
	{
		access.bits.bWrite = true;
	}
	else
	{
		access.bits.bWrite = false;
	}
	return access;
});

var2->setUserAccessLevelCallback([](const QString &strUserName) {
	QUaAccessLevel access;
	// Read Access to all
	access.bits.bRead = true;
	// Write Access only to john
	if (strUserName.compare("john", Qt::CaseSensitive) == 0)
	{
		access.bits.bWrite = true;
	}
	else
	{
		access.bits.bWrite = false;
	}
	return access;
});
```

Note the example above uses *C++ Lambdas*, but traditional callbacks can be used.

Now if *john* tries to write `var1`, the client might log an error like:

```
Write to node 'NS0|Numeric|762789430' failed [ret = BadUserAccessDenied]
```

The user-level access control is implemented in a **cascading** fashion, meaning that if a variable does not have a specific *UserAccessLevelCallback* defined, then it looks if the parent has one and so on. If no node has a *UserAccessLevelCallback* defined then all access is granted. For example:

```c++
auto * obj = objsFolder->addBaseObject("obj");

auto * subobj = obj->addBaseObject("subobj");

auto subsubvar = subobj->addProperty("subsubvar");
subsubvar->setWriteAccess(true);
subsubvar->setValue("hola");

// Define access on top level object, 
// since no specific access is defined on 'subsubvar',
// it inherits the grandparent's
obj->setUserAccessLevelCallback([](const QString &strUserName){
	QUaAccessLevel access;
	// Read Access to all
	access.bits.bRead = true;
	// Write Access only to juan
	if (strUserName.compare("juan", Qt::CaseSensitive) == 0)
	{
		access.bits.bWrite = true;
	}
	else
	{
		access.bits.bWrite = false;
	}
	return access;
});
```

When creating *custom types* it is possible to define a *default* custom access level by *reimplementing* the `userAccessLevel()` virtual method. For example:

In `customvar.h`:

```c++
#include <QUaBaseDataVariable>
#include <QUaProperty>

class CustomVar : public QUaBaseDataVariable
{
	Q_OBJECT

	Q_PROPERTY(QUaProperty         * myProp READ myProp)
	Q_PROPERTY(QUaBaseDataVariable * varFoo READ varFoo)
	Q_PROPERTY(QUaBaseDataVariable * varBar READ varBar)

public:
	Q_INVOKABLE explicit CustomVar(QUaServer *server);

	QUaProperty         * myProp();
	QUaBaseDataVariable * varFoo();
	QUaBaseDataVariable * varBar();

	// Reimplement virtual method to define default user access
	// for all instances of this type
	QUaAccessLevel userAccessLevel(const QString &strUserName) override;
};
```

In `customvar.cpp`:

```c++
#include "customvar.h"

CustomVar::CustomVar(QUaServer *server)
	: QUaBaseDataVariable(server)
{
	this->myProp()->setValue("xxx");
	this->varFoo()->setValue(true);
	this->varBar()->setValue(69);
	this->myProp()->setWriteAccess(true);
	this->varFoo()->setWriteAccess(true);
	this->varBar()->setWriteAccess(true);
}

QUaProperty * CustomVar::myProp()
{
	return this->findChild<QUaProperty*>("myProp");	
}

QUaBaseDataVariable * CustomVar::varFoo()
{
	return this->findChild<QUaBaseDataVariable*>("varFoo");
}

QUaBaseDataVariable * CustomVar::varBar()
{
	return this->findChild<QUaBaseDataVariable*>("varBar");
}

QUaAccessLevel CustomVar::userAccessLevel(const QString & strUserName)
{
	QUaAccessLevel access;
	// Read Access to all
	access.bits.bRead = true;
	// Write Access only to john
	if (strUserName.compare("john", Qt::CaseSensitive) == 0)
	{
		access.bits.bWrite = true;
	}
	else
	{
		access.bits.bWrite = false;
	}
	return access;
}
```

So any instance of `CustomVar` will use the reimplemented method by default, *unless* there exists a more **specific** callback:

```c++
QUaAccessLevel juanCanWrite(const QString &strUserName) 
{
	QUaAccessLevel access;
	// Read Access to all
	access.bits.bRead = true;
	// Write Access only to juan
	if (strUserName.compare("juan", Qt::CaseSensitive) == 0)
	{
		access.bits.bWrite = true;
	}
	else
	{
		access.bits.bWrite = false;
	}
	return access;
}

// ...

auto custom1 = objsFolder->addChild<CustomVar>("custom1");
auto custom2 = objsFolder->addChild<CustomVar>("custom2");

// Set specific callbacks
custom1->varFoo()->setUserAccessLevelCallback(&juanCanWrite);
custom2->setUserAccessLevelCallback(&juanCanWrite);
```

In the example above, all children variables (`myProp`, `varFoo` and `varBar`) inherit the reimplemented access level defined in the `CustomVar` class, which allows only *john* to write. But, the `varFoo` child of the `custom1` instance has a specific callback that will overrule the parent's permission.

The instance `custom2` also inherits by default the reimplemented access level defined in the `CustomVar` class, but the specific callback overrules the inherited permission.

The same mechanism is available for the `WriteMask` of any node (`userWriteMask()` / `setUserWriteMaskCallback()`) and for the execution of methods (`userExecutable()` / `setUserExecutableCallback()`).

## Sessions

The sessions currently open on the server are returned by the `sessions()` method. Each `QUaSession` provides the session id, the user name, the client application name and URI, the client address and port, and the time the session was created. The server notifies when clients connect and disconnect:

```c++
QObject::connect(&server, &QUaServer::clientConnected, [](const QUaSession *session) {
	qDebug() << "Connected" << session->userName() << session->address();
});
QObject::connect(&server, &QUaServer::clientDisconnected, [](const QUaSession *session) {
	qDebug() << "Disconnected" << session->userName();
});
```

## Users Example

Build and run the [06_users](../examples/06_users/main.cpp) example to learn more.
