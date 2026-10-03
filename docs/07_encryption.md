# Encryption

In this section the *QUaServer* library is configured to encrypt communications. Before continuing, make sure to go through the [Server](05_server.md) section in detail and generate all the required certificates and keys.

To support encryption, *open62541* is built against the [OpenSSL](https://www.openssl.org/) 3 library when the `QUASERVER_ENCRYPTION` option is enabled:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6> -DQUASERVER_ENCRYPTION=ON
cmake --build build
```

* On **Windows**, the OpenSSL toolkit shipped with Qt is used. Install it with the *Qt Maintenance Tool* (*Developer and Designer Tools → OpenSSL 3.x Toolkit*); it is found automatically in `<Qt root>/Tools/OpenSSLv3`. The OpenSSL DLLs (`libcrypto-3-x64.dll`, `libssl-3-x64.dll`) are copied next to the built examples and tests. In your own project, call `quaserver_deploy_openssl_runtime(<target>)` to copy them next to your executable, and deploy them along with it.
* On **Linux**, the system OpenSSL is used (e.g. `sudo apt install libssl-dev`), the same one Qt itself relies on.

To use another OpenSSL installation, pass `-DOPENSSL_ROOT_DIR=<path to OpenSSL>`.

When changing options of an existing build directory, it is recommended to **rebuild** the complete project.

Now copy the server's **certificate** and **private key** to the path where your binary is (`server.crt.der` and `server.key.der` created in the [Server](05_server.md) section).

Finally load the *certificate* and *private key* in the C++ code and pass them to the server with `setCertificate()` and `setPrivateKey()` **before** starting it:

```c++
#include <QCoreApplication>
#include <QDebug>
#include <QFile>

#include <QUaServer>

QByteArray readFile(const QString &fileName)
{
	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning() << "Could not open" << fileName;
		return QByteArray();
	}
	return file.readAll();
}

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

	QUaServer server;
	server.setCertificate(readFile("server.crt.der"));
	server.setPrivateKey (readFile("server.key.der"));
	// must match the URI stored in the certificate, else the server fails to start
	server.setApplicationUri("urn:unconfigured:application");

	server.start();

	return a.exec(); 
}
```

The `setPrivateKey()` method is only available when the library is built with `QUASERVER_ENCRYPTION`. If only a certificate is set, the server offers just the unencrypted `None` security policy.

Now when the client browses for the server, there should be a new option to connect using **Sign & Encrypt** which encrypts the communications between clients and the server.

Once encryption is available, user names and passwords are only accepted over encrypted connections, so the login credentials (see the [Users](06_users.md) section) are never sent in plain text.

<p align="center">
  <img src="../res/img/07_encryption_02.jpg">
</p>

## Trusting Clients

On an encrypted channel the client presents its own certificate. By default the server accepts **any** client certificate. To accept only known clients, pass DER encoded certificates to the server before starting it:

```c++
// client certificates, or the CAs that signed them
server.setTrustedCertificates({ readFile("client.crt.der") });
// CAs needed to complete the chain of a trusted client certificate, not trusted themselves
server.setIssuerCertificates({ readFile("ca.crt.der") });
// revocation lists of the trusted and issuer CAs
server.setRevocationLists({ readFile("ca.crl.der") });
```

* Trusting a **CA** accepts every client certificate it signed, unless the certificate is listed in its revocation list.
* Trusting a **client certificate** that is signed by a CA also requires that CA in the issuer list, otherwise the chain is incomplete and the client is rejected.
* A CA certificate must have the `keyCertSign` and `cRLSign` key usages (see the [Server](05_server.md) section), otherwise the clients it signed are rejected.
* The issuer and revocation lists are ignored while the trusted list is empty.
* Every entry must be a valid DER (or PEM) certificate or CRL. An invalid entry stops the remaining entries of its list from being loaded.

Rejected clients are reported through the `logMessage` signal.

## Security Policies

The server publishes an endpoint for every combination of the available security policies and message security modes, including the unencrypted `None` endpoint. Restrict them before starting the server:

```c++
// only Basic256Sha256 and Aes256_Sha256_RsaPss, always signed and encrypted
server.setSecurityPolicies(QUaSecurityPolicy::Basic256Sha256 | QUaSecurityPolicy::Aes256Sha256RsaPss);
server.setSecurityModes(QUaMessageSecurityMode::SignAndEncrypt);
```

Clients still discover the endpoints over an unencrypted channel, but can only open a session on the published ones. If no endpoint is left, `start()` fails. The deprecated `Basic128Rsa15` and `Basic256` policies are only available when *open62541* is built with `UA_INCLUDE_INSECURE_POLICIES`; the ECC policies need an ECC certificate.

## Encryption Example

Build and run the [07_encryption](../examples/07_encryption/main.cpp) example to learn more.
